#include "search.hpp"

// Global state
std::vector<MemorySearchResult> g_searchResults;
int g_searchDepth = 0;
MemorySearchOptions g_currentOptions = {0};

// Helper: Get size of value type
int GetTypeSize(int type) {
    switch(type) {
    case 0: return 1;  // byte
    case 1: return 2;  // word
    case 2: return 4;  // dword
    case 3: return 8;  // qword
    case 4: return 4;  // float
    case 5: return 8;  // double
    case 6: // string
        return (int)strlen(g_currentOptions.stringValue);
    default: return 4;
    }
}

// Helper: Check if value matches
bool ValueMatches(const uint8_t* data, int type, const MemorySearchOptions& options) {
    switch(type) {
    case 0: // byte
        return *reinterpret_cast<const uint8_t*>(data) == options.value.byteVal;
    case 1: // word
        return *reinterpret_cast<const uint16_t*>(data) == options.value.wordVal;
    case 2: // dword
        return *reinterpret_cast<const uint32_t*>(data) == options.value.dwordVal;
    case 3: // qword
        return *reinterpret_cast<const uint64_t*>(data) == options.value.qwordVal;
    case 4: // float
        return fabs(*reinterpret_cast<const float*>(data) - options.value.floatVal) < 0.0001f;
    case 5: // double
        return fabs(*reinterpret_cast<const double*>(data) - options.value.doubleVal) < 0.0000001;
    case 6: // string
        return memcmp(data, options.stringValue, options.value.dwordVal) == 0;
    }
    return false;
}

void MemorySearch_FirstScan(DWORD pid) {
    if (!pid) {
        printf("[!] Invalid PID for first scan\n");
        return;
    }

    // Full coverage: pull every readable VAD region for the process instead
    // of assuming everything lives in one contiguous module/image range.
    // This includes all modules, heaps, stacks, private allocations, and TEBs.
    std::vector<MemoryRegion> regions;
    if (!GetReadableMemoryRegions(pid, regions) || regions.empty()) {
        printf("[!] No readable memory regions found for PID %d\n", pid);
        return;
    }

    uint64_t totalSize = 0;
    for (const auto& r : regions) {
        totalSize += r.size;
    }

    printf("[>] Starting full-coverage scan: %zu regions, %.2f MB total\n",
           regions.size(), totalSize / (1024.0 * 1024.0));

    g_searchResults.clear();
    int typeSize = GetTypeSize(g_currentOptions.type);

    if (typeSize <= 0) {
        printf("[!] Invalid type size, aborting scan\n");
        return;
    }

    // Read in chunks
    const DWORD CHUNK_SIZE = 0x10000; // 64KB chunks
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    uint64_t totalScanned = 0;
    size_t resultsFound = 0;
    uint64_t chunksFailed = 0;
    int lastProgress = 0;

    for (const auto& region : regions) {
        for (uint64_t offset = 0; offset < region.size; offset += CHUNK_SIZE) {
            uint64_t currentAddr = region.base + offset;
            DWORD bytesToRead = CHUNK_SIZE;

            if (offset + bytesToRead > region.size) {
                bytesToRead = (DWORD)(region.size - offset);
            }

            // Align to type size
            bytesToRead = (bytesToRead / typeSize) * typeSize;

            if (bytesToRead == 0) {
                totalScanned += (region.size - offset);
                break;
            }

            if (vmmdll_read(currentAddr, buffer.data(), bytesToRead)) {
                // Scan each position
                start_mutex_lock();

                for (DWORD i = 0; i + (DWORD)typeSize <= bytesToRead; i += g_currentOptions.alignment) {
                    if (ValueMatches(&buffer[i], g_currentOptions.type, g_currentOptions)) {
                        MemorySearchResult result;
                        result.address = currentAddr + i;
                        result.currentValue.resize(typeSize);
                        memcpy(result.currentValue.data(), &buffer[i], typeSize);
                        result.previousValue = result.currentValue;
                        result.watched = false;
                        memset(result.description, 0, sizeof(result.description));
                        g_searchResults.push_back(result);
                        resultsFound++;
                    }
                }
                
                end_mutex_lock();
            } else {
                // Chunk failed to read (unmapped/guarded mid-region, transient
                // DMA glitch, etc.) — skip it rather than aborting the whole scan.
                chunksFailed++;
            }

            totalScanned += bytesToRead;

            // Print progress every 5%
            if (totalSize > 0) {
                int progress = (int)((totalScanned * 100) / totalSize);
                if (progress >= lastProgress + 5) {
                    printf("[>] Scan progress: %d%% (%zu results found)\r", progress, resultsFound);
                    lastProgress = progress;
                }
            }
        }
    }

    g_searchDepth = 1;
    state1_s.g_isFirstScan = false;

    // Snapshot the readable-region layout now so NextScan can validate
    // addresses against it without re-fetching the VAD map per address.
    RegionCache_Refresh(pid);

    printf("\n[+] First scan complete! Found %zu results across %zu regions (%llu chunk reads failed)\n",
           g_searchResults.size(), regions.size(), (unsigned long long)chunksFailed);
}

void MemorySearch_NextScan(DWORD pid) {
    if (g_searchResults.empty()) {
        printf("[!] No results to scan\n");
        return;
    }

    printf("[>] Starting next scan on %zu results...\n", g_searchResults.size());

    // Refresh the readable-region cache once for this whole pass, rather than
    // re-fetching the VAD map from the VMM for every single address.
    RegionCache_Refresh(pid);

    int typeSize = GetTypeSize(g_currentOptions.type);
    if (typeSize <= 0) {
        printf("[!] Invalid type size, aborting scan\n");
        return;
    }

    std::vector<MemorySearchResult> newResults;
    newResults.reserve(g_searchResults.size());

    size_t droppedRegion = 0;
    size_t failedReads = 0;

    for (size_t i = 0; i < g_searchResults.size(); i++) {
        auto& result = g_searchResults[i];

        // If the address no longer falls inside a readable region (memory
        // freed/decommitted/remapped since the last scan), drop it instead of
        // attempting a doomed read.
        if (!IsAddressReadable(result.address)) {
            droppedRegion++;
            continue;
        }

        // Read current value
        std::vector<uint8_t> currentValue(typeSize);
        if (vmmdll_read(result.address, currentValue.data(), typeSize)) {
            // Store previous value
            result.previousValue = result.currentValue;
            result.currentValue = currentValue;

            // For now, just do exact match on next scan
            if (ValueMatches(currentValue.data(), g_currentOptions.type, g_currentOptions)) {
                newResults.push_back(result);
            }
        } else {
            failedReads++;
        }

        // Progress indicator
        if (i % 1000 == 0) {
            printf("[>] Scanning... %zu/%zu\r", i, g_searchResults.size());
        }
    }

    g_searchResults = std::move(newResults);
    g_searchDepth++;

    printf("\n[+] Next scan complete: %zu results found (depth: %d, %zu dropped: region no longer readable, %zu read failures)\n",
           g_searchResults.size(), g_searchDepth, droppedRegion, failedReads);
}

void MemorySearch_Reset() {
    g_searchResults.clear();
    g_searchDepth = 0;
    state1_s.g_isFirstScan = true;
    printf("[+] Search reset\n");
}

void MemorySearch_AddWatch(int index) {
    if (index >= 0 && index < (int)g_searchResults.size()) {
        g_searchResults[index].watched = true;
        printf("[+] Added watch at index %d (0x%llX)\n",
               index, g_searchResults[index].address);
    }
}

void MemorySearch_RemoveWatch(int index) {
    if (index >= 0 && index < (int)g_searchResults.size()) {
        g_searchResults[index].watched = false;
        printf("[+] Removed watch at index %d\n", index);
    }
}

bool MemorySearch_WriteValue(uint64_t address, uint64_t newValue, int type) {
    bool success = false;

    switch(type) {
    case 0: { // byte
        uint8_t val = (uint8_t)newValue;
        success = vmmdll_write(address, &val, 1);
        break;
    }
    case 1: { // word
        uint16_t val = (uint16_t)newValue;
        success = vmmdll_write(address, &val, 2);
        break;
    }
    case 2: { // dword
        uint32_t val = (uint32_t)newValue;
        success = vmmdll_write(address, &val, 4);
        break;
    }
    case 3: // qword
    case 5: // double (same size)
        success = vmmdll_write(address, &newValue, 8);
        break;
    case 4: { // float
        float val = (float)newValue;
        success = vmmdll_write(address, &val, 4);
        break;
    }
    }

    if (success) {
        printf("[+] Successfully wrote to 0x%llX\n", address);

        // Update the result if it exists
        for (auto& result : g_searchResults) {
            if (result.address == address) {
                int typeSize = GetTypeSize(type);
                result.currentValue.resize(typeSize);
                memcpy(result.currentValue.data(), &newValue, typeSize);
                break;
            }
        }
    } else {
        printf("[!] Failed to write to 0x%llX\n", address);
    }

    return success;
}
