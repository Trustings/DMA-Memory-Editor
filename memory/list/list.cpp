#include "list.hpp"

namespace {

// Guards both cached lists. Held only for the duration of a copy, never
// across a DMA read or an ImGui call.
std::mutex                 g_listMutex;
std::vector<ProcessEntry>  g_processes;
std::vector<ModuleEntry>   g_modules;

} // namespace

bool list_all_processes(VMM_HANDLE hVMM) {
    if (!hVMM) {
        printf("[!] list_all_processes: no VMM handle\n");
        return false;
    }

    PVMMDLL_PROCESS_INFORMATION pProcesses = NULL;
    DWORD count = 0;

    if (!VMMDLL_ProcessGetInformationAll(hVMM, &pProcesses, &count)) {
        printf("[!] Failed to get processes\n");
        return false;
    }

    std::vector<ProcessEntry> refreshed;
    refreshed.reserve(count);

    printf("PIDs and Names:\n");
    for (DWORD i = 0; i < count; i++) {
        printf("%lu: %s\n", (unsigned long)pProcesses[i].dwPID, pProcesses[i].szNameLong);

        ProcessEntry entry;
        entry.pid  = pProcesses[i].dwPID;
        entry.name = pProcesses[i].szNameLong;
        refreshed.push_back(std::move(entry));
    }

    // The VMM owns this buffer; copying out and freeing here is what stops
    // the old "leak a fresh list on every refresh click" behaviour.
    VMMDLL_MemFree(pProcesses);

    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        g_processes = std::move(refreshed);
    }

    return true;
}

bool list_dlls_for_process(VMM_HANDLE hVMM) {
    if (!hVMM || !process_id) {
        printf("[!] list_dlls_for_process: no VMM handle or no attached process\n");
        return false;
    }

    PVMMDLL_MAP_MODULE pModuleMap = NULL;

    if (!VMMDLL_Map_GetModuleU(hVMM, process_id, &pModuleMap, 0)) {
        printf("[!] Failed to get module list\n");
        return false;
    }

    std::vector<ModuleEntry> refreshed;
    refreshed.reserve(pModuleMap->cMap);

    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        const VMMDLL_MAP_MODULEENTRY& currentModule = pModuleMap->pMap[i];

        printf("[%03lu] Base: 0x%-14llX | Size: 0x%08X | Name: %s\n",
               (unsigned long)i,
               (unsigned long long)currentModule.vaBase,
               currentModule.cbImageSize,
               currentModule.uszText);

        ModuleEntry entry;
        entry.base = currentModule.vaBase;
        entry.size = currentModule.cbImageSize;
        entry.name = currentModule.uszText ? currentModule.uszText : "";
        refreshed.push_back(std::move(entry));
    }

    VMMDLL_MemFree(pModuleMap);

    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        g_modules = std::move(refreshed);
    }

    return true;
}

size_t ProcessList_Snapshot(std::vector<ProcessEntry>& out) {
    std::lock_guard<std::mutex> lock(g_listMutex);
    out = g_processes;
    return out.size();
}

bool ProcessList_Get(size_t index, ProcessEntry& out) {
    std::lock_guard<std::mutex> lock(g_listMutex);
    if (index >= g_processes.size()) {
        return false;
    }
    out = g_processes[index];
    return true;
}

size_t ProcessList_Count() {
    std::lock_guard<std::mutex> lock(g_listMutex);
    return g_processes.size();
}

size_t ModuleList_Snapshot(std::vector<ModuleEntry>& out) {
    std::lock_guard<std::mutex> lock(g_listMutex);
    out = g_modules;
    return out.size();
}
