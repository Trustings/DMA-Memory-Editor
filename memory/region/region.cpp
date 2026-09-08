#include "region.hpp"

#include <algorithm>

// Windows page protection constants, defined locally on Linux where there is
// no Windows.h to provide them. These describe the *guest*, so they are the
// Windows values on both hosts.
namespace {

const uint32_t PROT_NOACCESS          = 0x001;
const uint32_t PROT_READONLY          = 0x002;
const uint32_t PROT_READWRITE         = 0x004;
const uint32_t PROT_WRITECOPY         = 0x008;
const uint32_t PROT_EXECUTE           = 0x010;
const uint32_t PROT_EXECUTE_READ      = 0x020;
const uint32_t PROT_EXECUTE_READWRITE = 0x040;
const uint32_t PROT_EXECUTE_WRITECOPY = 0x080;
const uint32_t PROT_GUARD             = 0x100;
const uint32_t PROT_NOCACHE           = 0x200;
const uint32_t PROT_WRITECOMBINE      = 0x400;

} // namespace

std::string ProtectToString(uint32_t protect) {
    std::string result;

    if (protect & PROT_NOACCESS)          result += "NOACCESS ";
    if (protect & PROT_READONLY)          result += "R ";
    if (protect & PROT_READWRITE)         result += "RW ";
    if (protect & PROT_WRITECOPY)         result += "RW (copy) ";
    if (protect & PROT_EXECUTE)           result += "X ";
    if (protect & PROT_EXECUTE_READ)      result += "RX ";
    if (protect & PROT_EXECUTE_READWRITE) result += "RWX ";
    if (protect & PROT_EXECUTE_WRITECOPY) result += "RWX (copy) ";
    if (protect & PROT_GUARD)             result += "GUARD ";
    if (protect & PROT_NOCACHE)           result += "NOCACHE ";
    if (protect & PROT_WRITECOMBINE)      result += "WRITECOMBINE ";

    return result.empty() ? "UNKNOWN" : result;
}

bool GetProcessMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions, bool quiet) {
    regions.clear();

    if (!hVMM || !pid) {
        printf("[!] Invalid VMM handle or PID\n");
        return false;
    }

    PVMMDLL_MAP_VAD pVadMap = NULL;

    // Get VAD map with module identification enabled.
    if (!VMMDLL_Map_GetVadU(hVMM, pid, 1, &pVadMap)) {
        printf("[!] Failed to get VAD map for PID %lu\n", (unsigned long)pid);
        return false;
    }

    if (!quiet) {
        printf("[+] Got VAD map with %lu entries\n", (unsigned long)pVadMap->cMap);
    }

    regions.reserve(pVadMap->cMap);

    for (DWORD i = 0; i < pVadMap->cMap; i++) {
        const PVMMDLL_MAP_VADENTRY pEntry = &pVadMap->pMap[i];

        MemoryRegion region;
        region.base    = pEntry->vaStart;
        region.size    = (pEntry->vaEnd > pEntry->vaStart) ? (pEntry->vaEnd - pEntry->vaStart) : 0;
        region.protect = pEntry->Protection;

        // PAGE_GUARD is a modifier OR'd onto the base protection, so a guard
        // page can report PAGE_READWRITE underneath it and still pass a naive
        // readable check. Exclude guard pages explicitly.
        const bool hasGuard = (pEntry->Protection & PROT_GUARD) != 0;

        region.isReadable = !hasGuard &&
                            ((pEntry->Protection & PROT_READONLY) ||
                             (pEntry->Protection & PROT_READWRITE) ||
                             (pEntry->Protection & PROT_EXECUTE_READ) ||
                             (pEntry->Protection & PROT_EXECUTE_READWRITE));

        region.isWriteable = (pEntry->Protection & PROT_READWRITE) ||
                             (pEntry->Protection & PROT_EXECUTE_READWRITE);

        region.isExecutable = (pEntry->Protection & PROT_EXECUTE) ||
                              (pEntry->Protection & PROT_EXECUTE_READ) ||
                              (pEntry->Protection & PROT_EXECUTE_READWRITE) ||
                              (pEntry->Protection & PROT_EXECUTE_WRITECOPY);

        if (pEntry->fImage) {
            region.type = "Image";
            if (pEntry->uszText) region.name = pEntry->uszText;
        } else if (pEntry->fFile) {
            region.type = "File";
            if (pEntry->uszText) region.name = pEntry->uszText;
        } else if (pEntry->fHeap) {
            region.type = "Heap";
            char heapName[32];
            snprintf(heapName, sizeof(heapName), "Heap_%lu", (unsigned long)pEntry->HeapNum);
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

        regions.push_back(std::move(region));
    }

    VMMDLL_MemFree(pVadMap);

    if (!quiet) {
        printf("[+] Found %zu memory regions\n", regions.size());
    }
    return true;
}

bool GetReadableMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions, bool quiet) {
    std::vector<MemoryRegion> allRegions;

    if (!GetProcessMemoryRegions(pid, allRegions, quiet)) {
        return false;
    }

    regions.clear();

    for (auto& region : allRegions) {
        if (!region.isReadable || region.size == 0 || region.size >= 0x7FFFFFFF) {
            continue;
        }
        if (region.base == 0 || region.base == (uint64_t)-1) {
            continue;
        }
        regions.push_back(std::move(region));
    }

    if (!quiet) {
        printf("[+] Found %zu readable memory regions\n", regions.size());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Readable-region cache
// ---------------------------------------------------------------------------
// Sorted by base address so lookups can binary search. Owned by the scanning
// thread.

namespace {

std::vector<MemoryRegion> g_regionCache;
bool                      g_regionCacheValid = false;

bool RegionContains(const MemoryRegion& r, uint64_t address) {
    return address >= r.base && address < r.base + r.size;
}

} // namespace

void RegionCache_Refresh(DWORD pid) {
    std::vector<MemoryRegion> regions;
    if (!GetReadableMemoryRegions(pid, regions, /*quiet=*/true)) {
        g_regionCacheValid = false;
        g_regionCache.clear();
        return;
    }

    std::sort(regions.begin(), regions.end(),
              [](const MemoryRegion& a, const MemoryRegion& b) { return a.base < b.base; });

    g_regionCache      = std::move(regions);
    g_regionCacheValid = true;
}

bool IsAddressReadable(uint64_t address) {
    if (!g_regionCacheValid) {
        // No cache populated yet -- fall back to a fresh (much slower) fetch
        // so this still returns the right answer if a caller forgets to
        // refresh.
        std::vector<MemoryRegion> regions;
        if (!GetReadableMemoryRegions(process_id, regions, /*quiet=*/true)) {
            return false;
        }
        for (const auto& region : regions) {
            if (RegionContains(region, address)) {
                return true;
            }
        }
        return false;
    }

    // First region whose base is greater than the address; the candidate is
    // the one before it.
    const auto it = std::upper_bound(
        g_regionCache.begin(), g_regionCache.end(), address,
        [](uint64_t value, const MemoryRegion& r) { return value < r.base; });

    if (it == g_regionCache.begin()) {
        return false;
    }
    return RegionContains(*(it - 1), address);
}

bool GetRegionInfo(uint64_t address, MemoryRegion& region) {
    std::vector<MemoryRegion> regions;

    if (!GetProcessMemoryRegions(process_id, regions, /*quiet=*/true)) {
        return false;
    }

    for (const auto& r : regions) {
        if (RegionContains(r, address)) {
            region = r;
            return true;
        }
    }

    return false;
}

void PrintMemoryRegions(DWORD pid) {
    std::vector<MemoryRegion> regions;

    if (!GetProcessMemoryRegions(pid, regions)) {
        printf("[!] Failed to get memory regions\n");
        return;
    }

    printf("\n=== Memory Regions for PID %lu ===\n", (unsigned long)pid);
    printf("%-18s %-12s %-10s %-6s %-6s %-6s %s\n",
           "Base", "Size", "Type", "Read", "Write", "Exec", "Name");
    printf("----------------------------------------------------------------\n");

    for (const auto& region : regions) {
        printf("0x%016llX 0x%08llX %-10s %-6s %-6s %-6s %s\n",
               (unsigned long long)region.base,
               (unsigned long long)region.size,
               region.type.c_str(),
               region.isReadable   ? "Yes" : "No",
               region.isWriteable  ? "Yes" : "No",
               region.isExecutable ? "Yes" : "No",
               region.name.c_str());
    }
    printf("Total regions: %zu\n\n", regions.size());
}
