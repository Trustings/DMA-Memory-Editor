#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "region.hpp"
#include "memory.hpp"
#include "core.hpp"

// Search result structure
struct MemorySearchResult {
    uint64_t address;
    std::vector<uint8_t> currentValue;
    std::vector<uint8_t> previousValue;
    bool watched;
    char description[256];
};


// Search options
struct MemorySearchOptions {
    int type;           // 0=byte,1=word,2=dword,3=qword,4=float,5=double,6=string
    int compareType;    // 0=exact,1=increased,2=decreased,3=unchanged,4=changed,
        // 5=increased by,6=decreased by,7=increased by %,8=decreased by %
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

    char stringValue[256];

    // For increased/decreased by
    float increasedBy;
    float decreasedBy;
    float increasedByPercent;
    float decreasedByPercent;
};

// Global search state
extern std::vector<MemorySearchResult> g_searchResults;
extern int g_searchDepth;
extern MemorySearchOptions g_currentOptions;

// Function declarations
int GetTypeSize(int type);
void MemorySearch_FirstScan(DWORD pid);
void MemorySearch_NextScan(DWORD pid);
void MemorySearch_Reset();
void MemorySearch_AddWatch(int index);
void MemorySearch_RemoveWatch(int index);
bool MemorySearch_WriteValue(uint64_t address, uint64_t newValue, int type);
