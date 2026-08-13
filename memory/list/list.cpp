#include "list.hpp"

PVMMDLL_PROCESS_INFORMATION processes = NULL;
PVMMDLL_MAP_MODULE pModuleMap = NULL;
DWORD process_count = 0;
DWORD pModuleMapCount = 0;

void list_all_processes(VMM_HANDLE hVMM) {

    if (!VMMDLL_ProcessGetInformationAll(hVMM, &processes, &process_count)) {
        printf("Failed to get processes\n");
        return;
    }

    printf("PIDs and Names:\n");
    for (DWORD i = 0; i < process_count; i++) {
        printf("%lu: %s\n", processes[i].dwPID, processes[i].szNameLong);
    }

}

void list_dlls_for_process(VMM_HANDLE hVMM) {

    if (!VMMDLL_Map_GetModuleU(hVMM, process_id, &pModuleMap, NULL)) {
        printf("[!] Failed to get module list\n");
        return;
    }

    pModuleMapCount = pModuleMap->cMap;

    for (DWORD i = 0; i < pModuleMapCount; i++) {

        VMMDLL_MAP_MODULEENTRY currentModule = pModuleMap->pMap[i];

        printf("[%03d] Base: 0x%-14llX | Size: 0x%08X | Name: %s\n", i, currentModule.vaBase, currentModule.cbImageSize, currentModule.uszText);

    }
}

