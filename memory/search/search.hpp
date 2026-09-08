#pragma once
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "region.hpp"
#include "memory.hpp"

// Value types, in the order the UI combo presents them.
enum MemoryValueType {
    MVT_BYTE   = 0,
    MVT_WORD   = 1,
    MVT_DWORD  = 2,
    MVT_QWORD  = 3,
    MVT_FLOAT  = 4,
    MVT_DOUBLE = 5,
    MVT_STRING = 6,
};

// Comparison modes, in the order the UI combo presents them.
enum MemoryCompareType {
    MCT_EXACT              = 0,
    MCT_INCREASED          = 1,
    MCT_DECREASED          = 2,
    MCT_UNCHANGED          = 3,
    MCT_CHANGED            = 4,
    MCT_INCREASED_BY       = 5,
    MCT_DECREASED_BY       = 6,
    MCT_INCREASED_BY_PCT   = 7,
    MCT_DECREASED_BY_PCT   = 8,
};

// Search result structure.
//
// Kept deliberately small: a baseline scan can hold millions of these, and the
// struct used to carry a 256-byte description field that nothing ever read.
struct MemorySearchResult {
    uint64_t             address;
    std::vector<uint8_t> currentValue;
    std::vector<uint8_t> previousValue;
    bool                 watched;
};

// Upper bound on results kept by a baseline (non-exact-value) first scan.
// Such a scan matches *every* aligned address in every readable region, so
// without a cap a multi-GB process would try to allocate hundreds of
// gigabytes of result structs.
const size_t MEMORYSEARCH_MAX_BASELINE_RESULTS = 5000000;

// Search options
struct MemorySearchOptions {
    int type;           // MemoryValueType
    int compareType;    // MemoryCompareType
    int alignment;      // 1,2,4,8

    // Value storage
    union {
        uint8_t  byteVal;
        uint16_t wordVal;
        uint32_t dwordVal;
        uint64_t qwordVal;
        float    floatVal;
        double   doubleVal;
    } value;

    char   stringValue[256];
    // Length of stringValue, kept separately. It used to be stashed in
    // value.dwordVal, which is a union member that overlaps the numeric
    // needle -- searching for a string after searching for a DWORD then
    // compared against a garbage length.
    size_t stringLength;

    // For increased/decreased by
    float increasedBy;
    float decreasedBy;
    float increasedByPercent;
    float decreasedByPercent;
};

// Global search state.
extern MemorySearchOptions g_currentOptions;

// Progress/state published by the scanning thread for the UI to display.
// Reading these never blocks the render thread.
extern std::atomic<bool> g_searchRunning;
extern std::atomic<int>  g_searchProgress;   // 0-100
extern std::atomic<bool> g_searchCancel;     // set to abort an in-flight scan
extern std::atomic<int>  g_resultsType;      // MemoryValueType the results hold

// Function declarations
int  GetTypeSize(int type);
void MemorySearch_FirstScan(DWORD pid);
void MemorySearch_NextScan(DWORD pid);
void MemorySearch_Reset();
void MemorySearch_SetWatch(size_t index, bool watched);
bool MemorySearch_WriteValueFromString(uint64_t address, const char* text, int type);
bool MemorySearch_RefreshValue(size_t index);

// --- Thread-safe accessors for the UI -------------------------------------
// The results vector is owned by the scanning thread. The render thread must
// never hold a reference into it across ImGui calls, because a scan in
// progress reallocates as it appends. These copy out under a short lock.

size_t MemorySearch_ResultCount();
int    MemorySearch_Depth();

// Copy up to `count` results starting at `start`. Returns the number copied.
size_t MemorySearch_SnapshotRange(size_t start, size_t count,
                                  std::vector<MemorySearchResult>& out);

// Copy a single result. Returns false if the index is out of range.
bool MemorySearch_SnapshotOne(size_t index, MemorySearchResult& out);

// Format a value buffer for display using the type it was scanned with.
// Never reads past the end of the buffer, whatever the UI type combo says.
std::string MemorySearch_FormatValue(const std::vector<uint8_t>& value, int type);
