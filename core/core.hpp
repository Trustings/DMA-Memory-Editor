#pragma once

#include "memory.hpp"

#ifdef __linux__
#include "debugger.hpp"
#endif

#include <atomic>
#include <cstdint>
#include <vector>

// Requests raised by the UI thread and serviced by the background worker.
// Setting one of these should be followed by Core_Notify() so the worker wakes
// immediately instead of waiting out its poll interval.

struct state0 {
    // Process tab
    std::atomic<bool> firstTimeInTab{false};
    std::atomic<bool> firstTimeInTab_Completed{false};
    std::atomic<bool> ButtonRefreshProcessClicked{false};
    std::atomic<bool> AttachProcessButtonClicked{false};
    std::atomic<bool> State0End{false};
};

struct state1 {
    // Memory search tab
    std::atomic<bool>     State1End{false};
    std::atomic<int>      currentPage{0};
    std::atomic<bool>     FirstMemorySearch{false};
    std::atomic<bool>     NextMemorySearch{false};
    std::atomic<bool>     g_isFirstScan{true};
    std::atomic<bool>     g_SearchResultReset{false};
    std::atomic<bool>     FindAccessesesClicked{false};
    std::atomic<uint64_t> watchAddress{0};
    std::atomic<bool>     wp_loop_completed{false};
};

extern struct state0 state0_s;
extern struct state1 state1_s;

// Index of the process selected in the UI, or -1.
extern std::atomic<int> selectedProcessIndex;

// ---------------------------------------------------------------------------
// Worker lifecycle
// ---------------------------------------------------------------------------

// Start the background worker. Long operations (process enumeration, memory
// scans, watchpoint loops) run here so the render thread never blocks.
void Core_Start();

// Ask the worker to finish and join it.
void Core_Stop();

// Wake the worker after raising a request flag.
void Core_Notify();

// ---------------------------------------------------------------------------
// Watchpoint hit list
// ---------------------------------------------------------------------------
// Written by the worker, read by the UI, so it goes through a snapshot rather
// than exposing the raw array.

size_t Watchpoints_Snapshot(std::vector<uint64_t>& out);
void   Watchpoints_Clear();
