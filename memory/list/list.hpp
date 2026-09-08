#pragma once
#include "memory.hpp"
#include <mutex>
#include <string>
#include <vector>

// A process as shown in the UI. The VMMDLL-owned buffer that these are built
// from is freed immediately after copying, so these entries stay valid for as
// long as the caller holds them -- unlike the raw VMMDLL pointer, which was
// previously leaked on every refresh and read from two threads at once.
struct ProcessEntry {
    uint32_t    pid;
    std::string name;
};

// A module/DLL of the attached process.
struct ModuleEntry {
    uint64_t    base;
    uint32_t    size;
    std::string name;
};

// Refresh the cached process list from the VMM. Safe to call from the worker
// thread while the UI thread reads the list through the snapshot helpers.
bool list_all_processes(VMM_HANDLE hVMM);

// Refresh the cached module list for the currently attached process.
bool list_dlls_for_process(VMM_HANDLE hVMM);

// Copy the whole process list out for rendering. Returns the entry count.
size_t ProcessList_Snapshot(std::vector<ProcessEntry>& out);

// Copy a single entry by index. Returns false if the index is out of range,
// which can happen when a refresh shrinks the list between frames.
bool ProcessList_Get(size_t index, ProcessEntry& out);

// Current number of known processes.
size_t ProcessList_Count();

// Copy the module list out for rendering.
size_t ModuleList_Snapshot(std::vector<ModuleEntry>& out);
