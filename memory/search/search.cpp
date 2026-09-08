#include "search.hpp"
#include <mutex>

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
// g_searchResults is written by the scanning thread and read by the render
// thread. It is private to this translation unit so that no caller can hold a
// reference into it across an ImGui call -- an in-flight scan reallocates the
// vector as it appends, which previously dangled the UI's iterators.

namespace {

std::mutex                      g_resultsMutex;
std::vector<MemorySearchResult> g_searchResults;
int                             g_searchDepth = 0;

} // namespace

MemorySearchOptions g_currentOptions = {};

std::atomic<bool> g_searchRunning(false);
std::atomic<int>  g_searchProgress(0);
std::atomic<bool> g_searchCancel(false);
std::atomic<int>  g_resultsType(MVT_DWORD);

// ---------------------------------------------------------------------------
// Typed value helpers
// ---------------------------------------------------------------------------
// All of these memcpy rather than reinterpret_cast: scan hits are not
// guaranteed to be naturally aligned, and casting an arbitrary byte pointer to
// uint32_t* and dereferencing it is undefined behaviour even on x86.

int GetTypeSize(int type) {
    switch (type) {
    case MVT_BYTE:   return 1;
    case MVT_WORD:   return 2;
    case MVT_DWORD:  return 4;
    case MVT_QWORD:  return 8;
    case MVT_FLOAT:  return 4;
    case MVT_DOUBLE: return 8;
    case MVT_STRING: return (int)g_currentOptions.stringLength;
    default:         return 4;
    }
}

namespace {

template <typename T>
T LoadAs(const uint8_t* data) {
    T v{};
    memcpy(&v, data, sizeof(T));
    return v;
}

// Numeric value as a double, for the "by amount" / "by percent" modes.
double ValueAsDouble(const uint8_t* data, int type) {
    switch (type) {
    case MVT_BYTE:   return (double)LoadAs<uint8_t>(data);
    case MVT_WORD:   return (double)LoadAs<uint16_t>(data);
    case MVT_DWORD:  return (double)LoadAs<uint32_t>(data);
    case MVT_QWORD:  return (double)LoadAs<uint64_t>(data);
    case MVT_FLOAT:  return (double)LoadAs<float>(data);
    case MVT_DOUBLE: return LoadAs<double>(data);
    default:         return 0.0;
    }
}

// Exact three-way compare in the value's own type, so a QWORD above 2^53 is
// still ordered correctly (converting through double would lose bits).
int CompareTyped(const uint8_t* a, const uint8_t* b, int type) {
    switch (type) {
    case MVT_BYTE: {
        uint8_t x = LoadAs<uint8_t>(a), y = LoadAs<uint8_t>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    case MVT_WORD: {
        uint16_t x = LoadAs<uint16_t>(a), y = LoadAs<uint16_t>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    case MVT_DWORD: {
        uint32_t x = LoadAs<uint32_t>(a), y = LoadAs<uint32_t>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    case MVT_QWORD: {
        uint64_t x = LoadAs<uint64_t>(a), y = LoadAs<uint64_t>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    case MVT_FLOAT: {
        float x = LoadAs<float>(a), y = LoadAs<float>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    case MVT_DOUBLE: {
        double x = LoadAs<double>(a), y = LoadAs<double>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    default:
        return 0;
    }
}

// Tolerance for float/double comparisons. Integers compare exactly.
bool NearlyEqual(double a, double b, int type) {
    if (type == MVT_FLOAT)  return fabs(a - b) < 1e-4;
    if (type == MVT_DOUBLE) return fabs(a - b) < 1e-7;
    return a == b;
}

// Does `data` match the search needle? (exact-value mode)
bool MatchesNeedle(const uint8_t* data, int type, const MemorySearchOptions& options) {
    switch (type) {
    case MVT_BYTE:   return LoadAs<uint8_t>(data)  == options.value.byteVal;
    case MVT_WORD:   return LoadAs<uint16_t>(data) == options.value.wordVal;
    case MVT_DWORD:  return LoadAs<uint32_t>(data) == options.value.dwordVal;
    case MVT_QWORD:  return LoadAs<uint64_t>(data) == options.value.qwordVal;
    case MVT_FLOAT:  return fabs((double)LoadAs<float>(data)  - (double)options.value.floatVal) < 1e-4;
    case MVT_DOUBLE: return fabs(LoadAs<double>(data) - options.value.doubleVal) < 1e-7;
    case MVT_STRING:
        // Length comes from stringLength, not from the value union.
        if (options.stringLength == 0) return false;
        return memcmp(data, options.stringValue, options.stringLength) == 0;
    }
    return false;
}

// Full comparison for a scan pass. `prev` is null on a first scan, where the
// only meaningful filter is an exact value -- every other mode captures a
// baseline to compare against on the next pass.
bool CompareValues(const uint8_t* cur, const uint8_t* prev, int type,
                   const MemorySearchOptions& options) {
    if (options.compareType == MCT_EXACT) {
        return MatchesNeedle(cur, type, options);
    }

    if (!prev) {
        // Baseline capture: keep everything so the next pass has something to
        // compare against. This is what makes Increased/Decreased usable
        // without knowing the initial value.
        return true;
    }

    const int size = GetTypeSize(type);
    if (size <= 0) {
        return false;
    }

    // Strings only support changed/unchanged meaningfully.
    if (type == MVT_STRING) {
        const bool same = memcmp(cur, prev, (size_t)size) == 0;
        if (options.compareType == MCT_UNCHANGED) return same;
        if (options.compareType == MCT_CHANGED)   return !same;
        return false;
    }

    switch (options.compareType) {
    case MCT_INCREASED:
        return CompareTyped(cur, prev, type) > 0;
    case MCT_DECREASED:
        return CompareTyped(cur, prev, type) < 0;
    case MCT_UNCHANGED:
        // Exact bit comparison -- "unchanged" means the bytes did not move.
        return memcmp(cur, prev, (size_t)size) == 0;
    case MCT_CHANGED:
        return memcmp(cur, prev, (size_t)size) != 0;
    case MCT_INCREASED_BY: {
        const double c = ValueAsDouble(cur, type);
        const double p = ValueAsDouble(prev, type);
        return NearlyEqual(c - p, (double)options.increasedBy, type);
    }
    case MCT_DECREASED_BY: {
        const double c = ValueAsDouble(cur, type);
        const double p = ValueAsDouble(prev, type);
        return NearlyEqual(p - c, (double)options.decreasedBy, type);
    }
    case MCT_INCREASED_BY_PCT: {
        const double c = ValueAsDouble(cur, type);
        const double p = ValueAsDouble(prev, type);
        if (p == 0.0) return false;
        const double pct = ((c - p) / fabs(p)) * 100.0;
        return fabs(pct - (double)options.increasedByPercent) < 0.5;
    }
    case MCT_DECREASED_BY_PCT: {
        const double c = ValueAsDouble(cur, type);
        const double p = ValueAsDouble(prev, type);
        if (p == 0.0) return false;
        const double pct = ((p - c) / fabs(p)) * 100.0;
        return fabs(pct - (double)options.decreasedByPercent) < 0.5;
    }
    default:
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// First scan
// ---------------------------------------------------------------------------

void MemorySearch_FirstScan(DWORD pid) {
    if (!pid) {
        printf("[!] Invalid PID for first scan\n");
        return;
    }

    const int typeSize = GetTypeSize(g_currentOptions.type);
    if (typeSize <= 0) {
        printf("[!] Invalid type size (empty search string?), aborting scan\n");
        return;
    }

    int alignment = g_currentOptions.alignment;
    if (alignment <= 0) {
        // A zero step would spin forever on the first matching address.
        printf("[!] Invalid alignment %d, defaulting to 1\n", alignment);
        alignment = 1;
    }

    // Full coverage: pull every readable VAD region for the process instead
    // of assuming everything lives in one contiguous module/image range.
    // This includes all modules, heaps, stacks, private allocations, and TEBs.
    std::vector<MemoryRegion> regions;
    if (!GetReadableMemoryRegions(pid, regions) || regions.empty()) {
        printf("[!] No readable memory regions found for PID %lu\n", (unsigned long)pid);
        return;
    }

    g_searchRunning  = true;
    g_searchCancel   = false;
    g_searchProgress = 0;

    uint64_t totalSize = 0;
    for (const auto& r : regions) {
        totalSize += r.size;
    }

    printf("[>] Starting full-coverage scan: %zu regions, %.2f MB total\n",
           regions.size(), totalSize / (1024.0 * 1024.0));

    // Reads are issued through the scatter API in batches. Over a real DMA
    // link one scatter round-trip carrying several MB is dramatically cheaper
    // than dozens of individual 64 KB reads.
    const DWORD  CHUNK_SIZE   = 0x10000;   // 64 KB
    const size_t BATCH_CHUNKS = 64;        // 4 MB per batch
    // Chunks overlap by typeSize-1 bytes so a value straddling a chunk
    // boundary is still found, and the scan window stops at CHUNK_SIZE so it
    // is found exactly once.
    const size_t OVERLAP   = (size_t)typeSize - 1;
    const size_t SLOT_SIZE = (size_t)CHUNK_SIZE + OVERLAP;

    struct Chunk {
        uint64_t address;
        DWORD    size;
    };

    std::vector<Chunk> chunks;
    for (const auto& region : regions) {
        for (uint64_t offset = 0; offset < region.size; offset += CHUNK_SIZE) {
            const uint64_t remaining = region.size - offset;
            const uint64_t want =
                (remaining < (uint64_t)SLOT_SIZE) ? remaining : (uint64_t)SLOT_SIZE;
            if (want < (uint64_t)typeSize) {
                continue;   // too small to hold a single value
            }
            Chunk c;
            c.address = region.base + offset;
            c.size    = (DWORD)want;
            chunks.push_back(c);
        }
    }

    if (chunks.empty()) {
        printf("[!] Nothing to scan\n");
        g_searchRunning = false;
        return;
    }

    // Buffers handed to VMMDLL_Scatter_PrepareEx must stay alive until the
    // handle is closed, so this is allocated once for the whole scan and
    // reused across batches via VMMDLL_Scatter_Clear.
    std::vector<uint8_t> batchBuffer(BATCH_CHUNKS * SLOT_SIZE);
    std::vector<uint32_t> bytesRead(BATCH_CHUNKS, 0);

    std::vector<MemorySearchResult> found;

    VMMDLL_SCATTER_HANDLE hScatter = CreateScatterHandle(pid);
    if (!hScatter) {
        printf("[!] Failed to create scatter handle, falling back to plain reads\n");
    }

    uint64_t totalScanned = 0;
    uint64_t chunksFailed = 0;
    int      lastProgress = 0;
    bool     cancelled    = false;

    // A non-exact first scan has nothing to compare against yet, so it keeps
    // every address as a baseline for the next pass. That is what makes
    // Increased/Decreased usable, and also what makes the result count
    // enormous, hence the cap below.
    const bool baseline = (g_currentOptions.compareType != MCT_EXACT);
    if (baseline) {
        printf("[>] Baseline pass: capturing every aligned address (max %zu)\n",
               MEMORYSEARCH_MAX_BASELINE_RESULTS);
    }

    for (size_t batchStart = 0; batchStart < chunks.size() && !cancelled;
         batchStart += BATCH_CHUNKS) {

        const size_t batchCount = (std::min)(BATCH_CHUNKS, chunks.size() - batchStart);

        // Zero the batch so a page that fails to read cannot be mistaken for
        // stale data left behind by the previous batch.
        memset(batchBuffer.data(), 0, batchCount * SLOT_SIZE);
        std::fill(bytesRead.begin(), bytesRead.end(), 0);

        if (hScatter) {
            for (size_t i = 0; i < batchCount; i++) {
                const Chunk& c = chunks[batchStart + i];
                AddScatterRead(hScatter, c.address, batchBuffer.data() + i * SLOT_SIZE,
                               c.size, &bytesRead[i]);
            }
            ExecuteScatterRead(hScatter, pid);
        } else {
            for (size_t i = 0; i < batchCount; i++) {
                const Chunk& c = chunks[batchStart + i];
                if (vmmdll_read(c.address, batchBuffer.data() + i * SLOT_SIZE, c.size)) {
                    bytesRead[i] = c.size;
                }
            }
        }

        for (size_t i = 0; i < batchCount && !cancelled; i++) {
            const Chunk&   c   = chunks[batchStart + i];
            const uint8_t* buf = batchBuffer.data() + i * SLOT_SIZE;

            totalScanned += c.size;

            if (bytesRead[i] == 0) {
                // Unmapped/guarded mid-region, or a transient DMA glitch --
                // skip it rather than aborting the whole scan.
                chunksFailed++;
                continue;
            }

            // Only scan bytes we actually got back, and only up to
            // CHUNK_SIZE so the overlap region belongs to the next chunk.
            const size_t got = (std::min)((size_t)bytesRead[i], (size_t)c.size);
            if (got < (size_t)typeSize) {
                continue;
            }
            const size_t limit =
                (std::min)(got - (size_t)typeSize + 1, (size_t)CHUNK_SIZE);

            for (size_t off = 0; off < limit; off += (size_t)alignment) {
                if (CompareValues(buf + off, nullptr, g_currentOptions.type,
                                  g_currentOptions)) {
                    MemorySearchResult hit;
                    hit.address = c.address + off;
                    hit.currentValue.resize((size_t)typeSize);
                    memcpy(hit.currentValue.data(), buf + off, (size_t)typeSize);
                    hit.previousValue = hit.currentValue;
                    hit.watched       = false;
                    found.push_back(std::move(hit));

                    // A baseline scan keeps every aligned address, so cap it
                    // rather than trying to allocate a result per address of
                    // a multi-gigabyte address space.
                    if (baseline && found.size() >= MEMORYSEARCH_MAX_BASELINE_RESULTS) {
                        printf("\n[!] Baseline capped at %zu results. Narrow the search"
                               " (use an exact value, or a larger alignment) for full"
                               " coverage.\n",
                               MEMORYSEARCH_MAX_BASELINE_RESULTS);
                        cancelled = true;
                        break;
                    }
                }
            }
        }

        if (totalSize > 0) {
            const int progress = (int)((totalScanned * 100) / totalSize);
            g_searchProgress = (progress > 100) ? 100 : progress;
            if (progress >= lastProgress + 5) {
                printf("[>] Scan progress: %d%% (%zu results found)\n",
                       progress, found.size());
                lastProgress = progress;
            }
        }

        if (g_searchCancel) {
            cancelled = true;
        }
    }

    if (hScatter) {
        CloseScatterHandle(hScatter);
    }

    size_t resultCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        g_searchResults = std::move(found);
        g_searchDepth   = 1;
        resultCount     = g_searchResults.size();
    }

    g_resultsType    = g_currentOptions.type;
    g_searchProgress = 100;
    g_searchRunning  = false;

    // Snapshot the readable-region layout now so NextScan can validate
    // addresses against it without re-fetching the VAD map per address.
    RegionCache_Refresh(pid);

    printf("\n[+] First scan %s: %zu results across %zu regions (%llu chunk reads failed)\n",
           cancelled ? "cancelled" : "complete",
           resultCount, regions.size(), (unsigned long long)chunksFailed);
}

// ---------------------------------------------------------------------------
// Next scan
// ---------------------------------------------------------------------------

void MemorySearch_NextScan(DWORD pid) {
    std::vector<MemorySearchResult> working;
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        working = g_searchResults;
    }

    if (working.empty()) {
        printf("[!] No results to scan\n");
        return;
    }

    const int type     = g_resultsType.load();
    const int typeSize = GetTypeSize(type);
    if (typeSize <= 0) {
        printf("[!] Invalid type size, aborting scan\n");
        return;
    }

    printf("[>] Starting next scan on %zu results...\n", working.size());

    g_searchRunning  = true;
    g_searchCancel   = false;
    g_searchProgress = 0;

    // Refresh the readable-region cache once for this whole pass, rather than
    // re-fetching the VAD map from the VMM for every single address.
    RegionCache_Refresh(pid);

    std::vector<MemorySearchResult> newResults;
    newResults.reserve(working.size());

    size_t droppedRegion = 0;
    size_t failedReads   = 0;
    bool   cancelled     = false;

    // Re-read the candidates through the scatter API in batches instead of
    // one round-trip per address.
    const size_t BATCH = 4096;
    std::vector<uint8_t> values((size_t)BATCH * (size_t)typeSize);
    std::vector<uint32_t> bytesRead(BATCH, 0);
    std::vector<size_t>  batchIndex;
    batchIndex.reserve(BATCH);

    VMMDLL_SCATTER_HANDLE hScatter = CreateScatterHandle(pid);

    for (size_t base = 0; base < working.size() && !cancelled; base += BATCH) {
        const size_t count = (std::min)(BATCH, working.size() - base);

        batchIndex.clear();
        memset(values.data(), 0, count * (size_t)typeSize);
        std::fill(bytesRead.begin(), bytesRead.end(), 0);

        for (size_t i = 0; i < count; i++) {
            const MemorySearchResult& result = working[base + i];

            // If the address no longer falls inside a readable region (memory
            // freed/decommitted/remapped since the last scan), drop it instead
            // of attempting a doomed read.
            if (!IsAddressReadable(result.address)) {
                droppedRegion++;
                continue;
            }

            const size_t slot = batchIndex.size();
            batchIndex.push_back(base + i);

            uint8_t* dst = values.data() + slot * (size_t)typeSize;
            if (hScatter) {
                AddScatterRead(hScatter, result.address, dst, (uint32_t)typeSize,
                               &bytesRead[slot]);
            } else if (vmmdll_read(result.address, dst, (size_t)typeSize)) {
                bytesRead[slot] = (uint32_t)typeSize;
            }
        }

        if (hScatter && !batchIndex.empty()) {
            ExecuteScatterRead(hScatter, pid);
        }

        for (size_t slot = 0; slot < batchIndex.size(); slot++) {
            if (bytesRead[slot] < (uint32_t)typeSize) {
                failedReads++;
                continue;
            }

            MemorySearchResult result = working[batchIndex[slot]];
            const uint8_t* cur = values.data() + slot * (size_t)typeSize;

            const uint8_t* prev =
                (result.currentValue.size() >= (size_t)typeSize)
                    ? result.currentValue.data()
                    : nullptr;

            if (CompareValues(cur, prev, type, g_currentOptions)) {
                result.previousValue = result.currentValue;
                result.currentValue.assign(cur, cur + typeSize);
                newResults.push_back(std::move(result));
            }
        }

        g_searchProgress = (int)(((base + count) * 100) / working.size());

        if (g_searchCancel) {
            cancelled = true;
        }
    }

    if (hScatter) {
        CloseScatterHandle(hScatter);
    }

    size_t remaining = 0;
    int    depth     = 0;
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        if (!cancelled) {
            g_searchResults = std::move(newResults);
            g_searchDepth++;
        }
        remaining = g_searchResults.size();
        depth     = g_searchDepth;
    }

    g_searchProgress = 100;
    g_searchRunning  = false;

    printf("\n[+] Next scan %s: %zu results (depth: %d, %zu dropped: region no longer readable, %zu read failures)\n",
           cancelled ? "cancelled (results kept)" : "complete",
           remaining, depth, droppedRegion, failedReads);
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void MemorySearch_Reset() {
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        g_searchResults.clear();
        g_searchDepth = 0;
    }
    g_searchProgress = 0;
    printf("[+] Search reset\n");
}

void MemorySearch_SetWatch(size_t index, bool watched) {
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    if (index < g_searchResults.size()) {
        g_searchResults[index].watched = watched;
    }
}

bool MemorySearch_WriteValueFromString(uint64_t address, const char* text, int type) {
    if (!text) {
        return false;
    }

    // Parse into the destination type directly. The old signature took a
    // uint64_t, so "1.5" written to a float landed as 1.
    uint8_t buffer[8] = { 0 };
    size_t  size      = 0;
    char*   end       = nullptr;

    switch (type) {
    case MVT_BYTE: {
        const uint8_t v = (uint8_t)strtoull(text, &end, 0);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    case MVT_WORD: {
        const uint16_t v = (uint16_t)strtoull(text, &end, 0);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    case MVT_DWORD: {
        const uint32_t v = (uint32_t)strtoull(text, &end, 0);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    case MVT_QWORD: {
        const uint64_t v = strtoull(text, &end, 0);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    case MVT_FLOAT: {
        const float v = strtof(text, &end);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    case MVT_DOUBLE: {
        const double v = strtod(text, &end);
        if (end == text) return false;
        memcpy(buffer, &v, sizeof(v)); size = sizeof(v);
        break;
    }
    default:
        printf("[!] Writing values of this type is not supported\n");
        return false;
    }

    if (!vmmdll_write(address, buffer, size)) {
        printf("[!] Failed to write to 0x%llX\n", (unsigned long long)address);
        return false;
    }

    printf("[+] Successfully wrote to 0x%llX\n", (unsigned long long)address);

    std::lock_guard<std::mutex> lock(g_resultsMutex);
    for (auto& result : g_searchResults) {
        if (result.address == address) {
            result.previousValue = result.currentValue;
            result.currentValue.assign(buffer, buffer + size);
            break;
        }
    }
    return true;
}

bool MemorySearch_RefreshValue(size_t index) {
    const int type     = g_resultsType.load();
    const int typeSize = GetTypeSize(type);
    if (typeSize <= 0) {
        return false;
    }

    uint64_t address = 0;
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        if (index >= g_searchResults.size()) {
            return false;
        }
        address = g_searchResults[index].address;
    }

    // The DMA read happens outside the lock so a slow link cannot stall the
    // render thread.
    std::vector<uint8_t> value((size_t)typeSize);
    if (!vmmdll_read(address, value.data(), (size_t)typeSize)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_resultsMutex);
    if (index >= g_searchResults.size() || g_searchResults[index].address != address) {
        return false;   // list changed underneath us
    }
    g_searchResults[index].previousValue = g_searchResults[index].currentValue;
    g_searchResults[index].currentValue  = std::move(value);
    return true;
}

// ---------------------------------------------------------------------------
// Thread-safe accessors
// ---------------------------------------------------------------------------

size_t MemorySearch_ResultCount() {
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    return g_searchResults.size();
}

int MemorySearch_Depth() {
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    return g_searchDepth;
}

size_t MemorySearch_SnapshotRange(size_t start, size_t count,
                                  std::vector<MemorySearchResult>& out) {
    out.clear();
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    if (start >= g_searchResults.size()) {
        return 0;
    }
    const size_t end = (std::min)(start + count, g_searchResults.size());
    out.assign(g_searchResults.begin() + (long)start, g_searchResults.begin() + (long)end);
    return out.size();
}

bool MemorySearch_SnapshotOne(size_t index, MemorySearchResult& out) {
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    if (index >= g_searchResults.size()) {
        return false;
    }
    out = g_searchResults[index];
    return true;
}

std::string MemorySearch_FormatValue(const std::vector<uint8_t>& value, int type) {
    const int size = GetTypeSize(type);

    // Guard on the buffer we actually hold, not on the type the UI combo
    // happens to be showing: switching the combo to QWORD after a byte scan
    // used to read 8 bytes out of a 1-byte vector.
    if (value.empty() || size <= 0 || value.size() < (size_t)size) {
        return "---";
    }

    char text[288];
    switch (type) {
    case MVT_BYTE:
        snprintf(text, sizeof(text), "%u", (unsigned)LoadAs<uint8_t>(value.data()));
        break;
    case MVT_WORD:
        snprintf(text, sizeof(text), "%u", (unsigned)LoadAs<uint16_t>(value.data()));
        break;
    case MVT_DWORD:
        snprintf(text, sizeof(text), "%lu", (unsigned long)LoadAs<uint32_t>(value.data()));
        break;
    case MVT_QWORD:
        snprintf(text, sizeof(text), "%llu", (unsigned long long)LoadAs<uint64_t>(value.data()));
        break;
    case MVT_FLOAT:
        snprintf(text, sizeof(text), "%.3f", (double)LoadAs<float>(value.data()));
        break;
    case MVT_DOUBLE:
        snprintf(text, sizeof(text), "%.6f", LoadAs<double>(value.data()));
        break;
    case MVT_STRING: {
        const size_t n = (std::min)(value.size(), sizeof(text) - 1);
        size_t w = 0;
        for (size_t i = 0; i < n; i++) {
            const uint8_t c = value[i];
            text[w++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        }
        text[w] = '\0';
        break;
    }
    default:
        return "---";
    }
    return std::string(text);
}
