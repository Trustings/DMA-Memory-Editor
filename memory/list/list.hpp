#pragma once
#include "memory.hpp"

extern PVMMDLL_PROCESS_INFORMATION processes;
extern PVMMDLL_MAP_MODULE pModuleMap;
extern DWORD process_count;
extern DWORD pModuleMapCount;

void list_all_processes(VMM_HANDLE hVMM);

void list_dlls_for_process(VMM_HANDLE hVMM);
