#include "region.hpp"

// Convert VMM protection flags to string
std::string ProtectToString(uint32_t protect) {
    std::string result;

    // Memory protection constants from Windows
    #ifdef __linux__
    const uint32_t PAGE_NOACCESS = 0x01;
    const uint32_t PAGE_READONLY = 0x02;
    const uint32_t PAGE_READWRITE = 0x04;
    const uint32_t PAGE_WRITECOPY = 0x08;
    const uint32_t PAGE_EXECUTE = 0x10;
    const uint32_t PAGE_EXECUTE_READ = 0x20;
    const uint32_t PAGE_EXECUTE_READWRITE = 0x40;
    const uint32_t PAGE_EXECUTE_WRITECOPY = 0x80;
    const uint32_t PAGE_GUARD = 0x100;
    const uint32_t PAGE_NOCACHE = 0x200;
    const uint32_t PAGE_WRITECOMBINE = 0x400;
    #endif

    if (protect & PAGE_NOACCESS) result += "NOACCESS ";
    if (protect & PAGE_READONLY) result += "R ";
    if (protect & PAGE_READWRITE) result += "RW ";
    if (protect & PAGE_WRITECOPY) result += "RW (copy) ";
    if (protect & PAGE_EXECUTE) result += "X ";
    if (protect & PAGE_EXECUTE_READ) result += "RX ";
    if (protect & PAGE_EXECUTE_READWRITE) result += "RWX ";
    if (protect & PAGE_EXECUTE_WRITECOPY) result += "RWX (copy) ";
    if (protect & PAGE_GUARD) result += "GUARD ";
    if (protect & PAGE_NOCACHE) result += "NOCACHE ";
    if (protect & PAGE_WRITECOMBINE) result += "WRITECOMBINE ";

    return result.empty() ? "UNKNOWN" : result;
}

// Get all memory regions using VAD (Virtual Address Descriptor)
bool GetProcessMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions) {
    regions.clear();

    if (!hVMM || !pid) {
        printf("[!] Invalid VMM handle or PID\n");
        return false;
    }

    PVMMDLL_MAP_VAD pVadMap = NULL;

    // Get VAD map with module identification enabled
    if (!VMMDLL_Map_GetVadU(hVMM, pid, true, &pVadMap)) {
        printf("[!] Failed to get VAD map for PID %d\n", pid);
        return false;
    }

    printf("[+] Got VAD map with %d entries\n", pVadMap->cMap);

    for (DWORD i = 0; i < pVadMap->cMap; i++) {
        PVMMDLL_MAP_VADENTRY pEntry = &pVadMap->pMap[i];

        MemoryRegion region;
        region.base = pEntry->vaStart;
        region.size = pEntry->vaEnd - pEntry->vaStart;
        region.protect = pEntry->Protection;

        // Set permission flags
        // PAGE_GUARD (0x100) is a modifier OR'd onto the base protection, so a
        // guard page can report PAGE_READWRITE underneath it and still pass a
        // naive readable check. Exclude guard pages explicitly.
        bool hasGuard = (pEntry->Protection & 0x100) != 0;

        region.isReadable = !hasGuard &&
                            ((pEntry->Protection & 0x02) ||  // PAGE_READONLY
                             (pEntry->Protection & 0x04) ||  // PAGE_READWRITE
                             (pEntry->Protection & 0x20) ||  // PAGE_EXECUTE_READ
                             (pEntry->Protection & 0x40));   // PAGE_EXECUTE_READWRITE

        region.isWriteable = (pEntry->Protection & 0x04) ||  // PAGE_READWRITE
                             (pEntry->Protection & 0x40);    // PAGE_EXECUTE_READWRITE

        region.isExecutable = (pEntry->Protection & 0x10) ||  // PAGE_EXECUTE
                              (pEntry->Protection & 0x20) ||  // PAGE_EXECUTE_READ
                              (pEntry->Protection & 0x40) ||  // PAGE_EXECUTE_READWRITE
                              (pEntry->Protection & 0x80);    // PAGE_EXECUTE_WRITECOPY

        // Determine region type
        if (pEntry->fImage) {
            region.type = "Image";
            if (pEntry->uszText) {
                region.name = pEntry->uszText;
            }
        } else if (pEntry->fFile) {
            region.type = "File";
            if (pEntry->uszText) {
                region.name = pEntry->uszText;
            }
        } else if (pEntry->fHeap) {
            region.type = "Heap";
            char heapName[32];
            snprintf(heapName, sizeof(heapName), "Heap_%d", pEntry->HeapNum);
            region.name = heapName;
        } else if (pEntry->fStack) {
            region.type = "Stack";
            region.name = "Stack";
        } else if (pEntry->fPrivateMemory) {
            region.type = "Private";
            region.name = "Private";
        } else if (pEntry->fTeb) {
            region.type = "TEB";
            region.name = "TEB";
        } else {
            region.type = "Unknown";
        }

        regions.push_back(region);
    }

    VMMDLL_MemFree(pVadMap);

    printf("[+] Found %zu memory regions\n", regions.size());
    return true;
}

// Get only readable memory regions (for scanning)
bool GetReadableMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions) {
    std::vector<MemoryRegion> allRegions;

    if (!GetProcessMemoryRegions(pid, allRegions)) {
        return false;
    }

    regions.clear();

    for (const auto& region : allRegions) {
        // Only include readable regions with reasonable size
        if (region.isReadable && region.size > 0 && region.size < 0x7FFFFFFF) {
            // Skip obviously bad regions
            if (region.base == 0 || region.base == (uint64_t)-1) {
                continue;
            }
            regions.push_back(region);
        }
    }

    printf("[+] Found %zu readable memory regions\n", regions.size());
    return true;
}

// Cache of readable regions, populated by RegionCache_Refresh().
// IsAddressReadable() uses this so a bulk validation pass (e.g. one call per
// search result in NextScan) doesn't refetch the whole VAD map from the VMM
// on every single address.
static std::vector<MemoryRegion> g_regionCache;
static bool g_regionCacheValid = false;

void RegionCache_Refresh(DWORD pid) {
    std::vector<MemoryRegion> regions;
    if (GetReadableMemoryRegions(pid, regions)) {
        g_regionCache = std::move(regions);
        g_regionCacheValid = true;
    } else {
        g_regionCacheValid = false;
    }
}

// Check if an address is in a readable region
bool IsAddressReadable(uint64_t address) {
    if (g_regionCacheValid) {
        for (const auto& region : g_regionCache) {
            if (address >= region.base && address < region.base + region.size) {
                return true;
            }
        }
        return false;
    }

    // No cache populated yet — fall back to a fresh (slower) fetch so this
    // function still works correctly even if a caller forgets to refresh.
    std::vector<MemoryRegion> regions;
    if (!GetReadableMemoryRegions(process_id, regions)) {
        return false;
    }

    for (const auto& region : regions) {
        if (address >= region.base && address < region.base + region.size) {
            return true;
        }
    }

    return false;
}

// Get region info for a specific address
bool GetRegionInfo(uint64_t address, MemoryRegion& region) {
    std::vector<MemoryRegion> regions;

    if (!GetProcessMemoryRegions(process_id, regions)) {
        return false;
    }

    for (const auto& r : regions) {
        if (address >= r.base && address < r.base + r.size) {
            region = r;
            return true;
        }
    }

    return false;
}

// Print regions for debugging
void PrintMemoryRegions(DWORD pid) {
    std::vector<MemoryRegion> regions;

    if (!GetProcessMemoryRegions(pid, regions)) {
        printf("[!] Failed to get memory regions\n");
        return;
    }

    printf("\n=== Memory Regions for PID %d ===\n", pid);
    printf("%-16s %-16s %-10s %-8s %-8s %-8s %s\n",
           "Base", "Size", "Type", "Read", "Write", "Exec", "Name");
    printf("----------------------------------------------------------------\n");

    for (const auto& region : regions) {
        printf("0x%012llX 0x%08X %-10s %-8s %-8s %-8s %s\n",
               region.base,
               (DWORD)region.size,
               region.type.c_str(),
               region.isReadable ? "Yes" : "No",
               region.isWriteable ? "Yes" : "No",
               region.isExecutable ? "Yes" : "No",
               region.name.c_str());
    }
    printf("Total regions: %zu\n\n", regions.size());
}
