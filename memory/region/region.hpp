#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "memory.hpp"

struct MemoryRegion {
    uint64_t    base;
    uint64_t    size;
    uint32_t    protect;
    std::string type;
    std::string name;
    bool        isReadable;
    bool        isWriteable;
    bool        isExecutable;
};

// Get all memory regions for a process using VAD.
bool GetProcessMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions, bool quiet = false);

// Get only readable memory regions (for scanning).
bool GetReadableMemoryRegions(DWORD pid, std::vector<MemoryRegion>& regions, bool quiet = false);

// Refresh the internal readable-region cache for a process. Call this once
// before a batch of IsAddressReadable() checks (e.g. at the top of a scan
// pass) so each check doesn't re-fetch the VAD map over DMA.
//
// The cache is owned by whichever thread runs the scan; IsAddressReadable()
// must be called from that same thread.
void RegionCache_Refresh(DWORD pid);

// Check if an address is in a readable region. O(log n) against the cache
// populated by RegionCache_Refresh() -- this is called once per candidate on
// every next-scan, so a linear walk here dominated the whole pass.
bool IsAddressReadable(uint64_t address);

// Get region info for a specific address.
bool GetRegionInfo(uint64_t address, MemoryRegion& region);

// Print regions for debugging.
void PrintMemoryRegions(DWORD pid);

// Convert VMM protection flags to readable strings.
std::string ProtectToString(uint32_t protect);
