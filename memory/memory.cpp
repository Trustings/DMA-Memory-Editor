#include "memory.hpp"
#include "config.hpp"

#include <cctype>
#include <cstdlib>

#ifdef __linux__
#include <signal.h>
#include <fcntl.h>
#endif

VMM_HANDLE  hVMM                 = nullptr;
std::string process_name;
std::string DLL_Name;
uint32_t    process_id           = 0;
HANDLE      process_handle       = nullptr;
ULONG64     process_base_address = 0;
ULONG64     DLL_base_address     = 0;
DWORD       process_size         = 0;
DWORD       DLL_size             = 0;

static uint64_t cbSize = 0x80000;

// ---------------------------------------------------------------------------
// Linux helpers
// ---------------------------------------------------------------------------
#ifdef __linux__

// Enumerate PIDs whose /proc/<pid>/cmdline contains `processName`.
//
// The previous implementation returned -1 whenever errno happened to be set
// after the readdir loop -- and errno is routinely set by the fopen() calls
// inside that loop, for processes that exit mid-scan or that this user cannot
// read. The result was a spurious "No qemu-system-x86 processes found" on a
// machine that clearly had one running.
static std::vector<pid_t> GetPidsByName(const std::string& processName) {
    std::vector<pid_t> pids;

    DIR* proc = opendir("/proc");
    if (!proc) {
        fprintf(stderr, "[!] Failed to open /proc: %s\n", strerror(errno));
        return pids;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(proc)) != nullptr) {
        bool isPid = entry->d_name[0] != '\0';
        for (int i = 0; entry->d_name[i] != '\0'; i++) {
            if (!isdigit((unsigned char)entry->d_name[i])) {
                isPid = false;
                break;
            }
        }
        if (!isPid) {
            continue;
        }

        const pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);

        FILE* file = fopen(path, "rb");
        if (!file) {
            continue;   // process gone, or not ours to read -- not an error
        }

        char cmdline[4096];
        const size_t got = fread(cmdline, 1, sizeof(cmdline) - 1, file);
        fclose(file);

        if (got == 0) {
            continue;
        }
        cmdline[got] = '\0';

        // cmdline is NUL-separated; flatten it so a substring search sees the
        // whole command line rather than just argv[0].
        for (size_t i = 0; i < got; i++) {
            if (cmdline[i] == '\0') {
                cmdline[i] = ' ';
            }
        }

        if (strstr(cmdline, processName.c_str()) != nullptr) {
            pids.push_back(pid);
        }
    }

    closedir(proc);
    return pids;
}

// Run a command without going through a shell. Everything here used to be
// built by string concatenation and handed to system(), which meant any path
// containing a space or a shell metacharacter would misbehave.
static int RunCommand(const std::vector<std::string>& args, bool quiet = true) {
    if (args.empty()) {
        return -1;
    }

    const pid_t child = fork();
    if (child < 0) {
        return -1;
    }

    if (child == 0) {
        if (quiet) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void ForceUnmount(const std::string& mountPoint) {
    // fusermount first: it is the only one of these that works without root,
    // which matters because MemProcFS is mounted through FUSE.
    if (RunCommand({ "fusermount", "-uz", mountPoint }) == 0) {
        printf("[+] Unmounted %s with fusermount\n", mountPoint.c_str());
        return;
    }

    if (umount(mountPoint.c_str()) == 0) {
        printf("[+] Unmounted %s\n", mountPoint.c_str());
        return;
    }

    if (umount2(mountPoint.c_str(), MNT_DETACH) == 0) {
        printf("[+] Lazy unmounted %s\n", mountPoint.c_str());
        return;
    }

    printf("[+] Nothing to unmount at %s (%s)\n", mountPoint.c_str(), strerror(errno));
}

static pid_t g_memprocPid = -1;

static void KillExistingMemProcFs() {
    RunCommand({ "pkill", "-f", g_config.memprocfsPath + ".*-mount " + g_config.mountPoint });
    ForceUnmount(g_config.mountPoint);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// Launch memprocfs so that /mnt/memproc/misc/procinfo is available. Only used
// as a fallback when a process DTB needs recovering.
static bool InitMemProcFs() {
    KillExistingMemProcFs();

    const std::vector<pid_t> pids = GetPidsByName(g_config.qemuProcessName);
    if (pids.empty()) {
        fprintf(stderr, "[!] No %s process found\n", g_config.qemuProcessName.c_str());
        return false;
    }

    const std::string url = "qemu://hugepage-pid=" + std::to_string((int)pids[0]) +
                            ",qmp=" + g_config.memprocfsQmp;

    printf("[>] Launching: %s -device %s -mount %s\n",
           g_config.memprocfsPath.c_str(), url.c_str(), g_config.mountPoint.c_str());

    const pid_t child = fork();
    if (child == -1) {
        fprintf(stderr, "[!] Failed to fork\n");
        return false;
    }

    if (child == 0) {
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }

        execlp(g_config.memprocfsPath.c_str(),
               "memprocfs",
               "-device", url.c_str(),
               "-mount",  g_config.mountPoint.c_str(),
               "-v",
               (char*)NULL);
        _exit(127);
    }

    g_memprocPid = child;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    int status = 0;
    if (waitpid(child, &status, WNOHANG) == child) {
        fprintf(stderr, "[!] memprocfs terminated during startup "
                        "(is '%s' present and executable?)\n",
                g_config.memprocfsPath.c_str());
        g_memprocPid = -1;
        return false;
    }

    printf("[+] memprocfs launched with PID %d\n", (int)child);
    return true;
}

static void TerminateMemProcFs() {
    if (g_memprocPid <= 0) {
        return;
    }

    printf("[>] Terminating memprocfs (PID %d)\n", (int)g_memprocPid);
    ForceUnmount(g_config.mountPoint);

    if (kill(g_memprocPid, SIGTERM) == 0) {
        for (int i = 0; i < 15; i++) {
            int status = 0;
            if (waitpid(g_memprocPid, &status, WNOHANG) == g_memprocPid) {
                g_memprocPid = -1;
                printf("[+] memprocfs terminated\n");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        printf("[!] memprocfs ignored SIGTERM, killing\n");
        if (kill(g_memprocPid, SIGKILL) == 0) {
            int status = 0;
            waitpid(g_memprocPid, &status, 0);
        }
    }

    ForceUnmount(g_config.mountPoint);
    g_memprocPid = -1;
}

#endif // __linux__

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

// Build the LeechCore device string, either from config or by autodetecting
// the local QEMU instance.
static bool ResolveDeviceString(std::string& device) {
    if (!g_config.device.empty()) {
        device = g_config.device;
        return true;
    }

#ifdef __linux__
    const std::vector<pid_t> pids = GetPidsByName(g_config.qemuProcessName);
    if (pids.empty()) {
        fprintf(stderr,
                "[!] No '%s' process found.\n"
                "    Start the VM first, or pass --device explicitly\n"
                "    (e.g. --device fpga).\n",
                g_config.qemuProcessName.c_str());
        return false;
    }

    printf("[+] Found %s with PID %d\n", g_config.qemuProcessName.c_str(), (int)pids[0]);
    device = "qemu://hugepage-pid=" + std::to_string((int)pids[0]) +
             ",qmp=" + g_config.qmpSocket;
    return true;
#else
    device = "fpga";
    return true;
#endif
}

bool Initialize() {
    if (hVMM) {
        return true;
    }

    std::string device;
    if (!ResolveDeviceString(device)) {
        return false;
    }

    printf("[+] Using device: %s\n", device.c_str());

    std::vector<const char*> parameters;
    parameters.push_back("");
    parameters.push_back("-device");
    parameters.push_back(device.c_str());
    if (g_config.verbose) {
        parameters.push_back("-v");
    }

    hVMM = VMMDLL_Initialize((DWORD)parameters.size(),
                             const_cast<LPCSTR*>(parameters.data()));
    if (!hVMM) {
        fprintf(stderr,
                "[!] VMMDLL_Initialize failed for device '%s'.\n"
                "    Check that the backend is reachable and that this process\n"
                "    has the privileges it needs (hugepages/FPGA access).\n",
                device.c_str());
        return false;
    }

    printf("[+] Successfully initialized VMM\n");

    if (!VMMDLL_InitializePlugins(hVMM)) {
        // Not fatal: plugins are only needed for the VFS-based DTB recovery
        // path, and plain reads and writes work without them.
        printf("[-] VMMDLL_InitializePlugins failed; DTB recovery may not work\n");
    }

    return true;
}

void Shutdown() {
#ifdef __linux__
    TerminateMemProcFs();
#endif
    if (hVMM) {
        VMMDLL_Close(hVMM);
        hVMM = nullptr;
        printf("[+] VMM closed\n");
    }
}

bool InitializeDLL(const std::string& processName, const std::string& dllName) {
    printf("[+] Process id: %lu\n", (unsigned long)process_id);

    if (!process_id) {
        // These used to be printf("%s", std::string) -- passing a std::string
        // through a varargs %s is undefined behaviour and crashed on exactly
        // the error path that was supposed to explain what went wrong.
        printf("[!] No attached process (wanted %s)\n", processName.c_str());
        return false;
    }

    if (!GetDLLModuleBase(process_id, dllName)) {
        printf("[!] Failed to get base address/size of %s\n", dllName.c_str());
        return false;
    }

    printf("[+] Base address: 0x%llX\n", (unsigned long long)DLL_base_address);
    printf("[+] Image size:   0x%lX\n", (unsigned long)DLL_size);
    return true;
}

VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb,
               _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo)
{
    if (strcmp(uszName, "dtb.txt") == 0) {
        cbSize = cb;
    }
}

// ---------------------------------------------------------------------------
// Reads and writes
// ---------------------------------------------------------------------------

bool vmmdll_read(uint64_t address, void* buffer, size_t size) {
    if (!hVMM) {
        return false;
    }
    if (!VMMDLL_MemRead(hVMM, (DWORD)process_id, (ULONG64)address, (PBYTE)buffer, (DWORD)size)) {
        // No error code here on purpose: VMMDLL does not set errno or
        // SetLastError, so the number that used to be printed was whatever
        // unrelated call happened to fail last.
        printf("[!] VMMDLL_MemRead failed at 0x%llX (%zu bytes)\n",
               (unsigned long long)address, size);
        return false;
    }
    return true;
}

bool vmmdll_write(uint64_t address, const void* buffer, size_t size) {
    if (!hVMM) {
        return false;
    }
    if (!VMMDLL_MemWrite(hVMM, (DWORD)process_id, (ULONG64)address,
                         (PBYTE)const_cast<void*>(buffer), (DWORD)size)) {
        printf("[!] VMMDLL_MemWrite failed at 0x%llX (%zu bytes)\n",
               (unsigned long long)address, size);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Scatter reads
// ---------------------------------------------------------------------------

VMMDLL_SCATTER_HANDLE CreateScatterHandle(uint32_t pid) {
    if (!hVMM) {
        return nullptr;
    }
    return VMMDLL_Scatter_Initialize(hVMM, pid, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
}

void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle) {
    if (handle) {
        VMMDLL_Scatter_CloseHandle(handle);
    }
}

void AddScatterRead(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer,
                    uint32_t size, uint32_t* bytesRead) {
    if (!handle) {
        return;
    }
    VMMDLL_Scatter_PrepareEx(handle, address, size, static_cast<PBYTE>(buffer),
                             reinterpret_cast<PDWORD>(bytesRead));
}

bool ExecuteScatterRead(VMMDLL_SCATTER_HANDLE handle, uint32_t pid) {
    if (!handle) {
        return false;
    }
    const bool ok = VMMDLL_Scatter_ExecuteRead(handle) ? true : false;
    // Clear for reuse. The buffers stay registered until the handle is closed,
    // which is why callers must keep them alive for the whole scan.
    VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return ok;
}

// ---------------------------------------------------------------------------
// DTB recovery
// ---------------------------------------------------------------------------

#ifdef _WIN32
static bool FixCr3()
{
    PVMMDLL_MAP_MODULEENTRY module_entry = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, (LPSTR)process_name.c_str(),
                                      &module_entry, 0)) {
        VMMDLL_MemFree(module_entry);
        return true;   // nothing to patch
    }

    if (!VMMDLL_InitializePlugins(hVMM)) {
        printf("[-] Failed VMMDLL_InitializePlugins call\n");
        return false;
    }

    // Give the plugin a moment before reading its virtual files.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    for (int wait = 0; wait < 100; wait++) {
        BYTE bytes[4] = { 0 };
        DWORD read = 0;
        const NTSTATUS nt = VMMDLL_VfsReadW(hVMM, (LPWSTR)L"\\misc\\procinfo\\progress_percent.txt",
                                            bytes, 3, &read, 0);
        if (nt == VMMDLL_STATUS_SUCCESS && atoi((LPSTR)bytes) >= 100) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    VMMDLL_VFS_FILELIST2 fileList;
    fileList.dwVersion       = VMMDLL_VFS_FILELIST_VERSION;
    fileList.h               = 0;
    fileList.pfnAddDirectory = 0;
    fileList.pfnAddFile      = cbAddFile;

    if (!VMMDLL_VfsListU(hVMM, (LPSTR)"\\misc\\procinfo\\", &fileList)) {
        return false;
    }

    const size_t bufferSize = (size_t)cbSize;
    std::unique_ptr<BYTE[]> bytes(new BYTE[bufferSize]);
    memset(bytes.get(), 0, bufferSize);

    DWORD read = 0;
    if (VMMDLL_VfsReadW(hVMM, (LPWSTR)L"\\misc\\procinfo\\dtb.txt", bytes.get(),
                        (DWORD)(bufferSize - 1), &read, 0) != VMMDLL_STATUS_SUCCESS) {
        return false;
    }

    std::vector<uint64_t> possible_dtbs;
    std::istringstream iss(std::string(reinterpret_cast<char*>(bytes.get()), read));
    std::string line;

    while (std::getline(iss, line)) {
        Info info = {};
        std::istringstream info_ss(line);
        if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id
                    >> std::hex >> info.dtb >> info.kernelAddr >> info.name) {
            if (info.process_id == 0) {
                possible_dtbs.push_back(info.dtb);
            }
            if (process_name.find(info.name) != std::string::npos) {
                possible_dtbs.push_back(info.dtb);
            }
        }
    }

    for (size_t i = 0; i < possible_dtbs.size(); i++) {
        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_PROCESS_DTB | process_id, possible_dtbs[i]);
        if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, (LPSTR)process_name.c_str(),
                                          &module_entry, 0)) {
            VMMDLL_MemFree(module_entry);
            printf("[+] Patched DTB: 0x%llX\n", (unsigned long long)possible_dtbs[i]);
            return true;
        }
    }

    printf("[-] Failed to patch DTB\n");
    return false;
}
#endif // _WIN32

#ifdef __linux__
static bool FixCr3()
{
    // First try direct lookup -- no mount needed if the DTB is already right.
    PVMMDLL_MAP_MODULEENTRY module_entry = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id,
                                      const_cast<char*>(process_name.c_str()),
                                      &module_entry, 0)) {
        printf("[+] DTB already correct\n");
        VMMDLL_MemFree(module_entry);
        return true;
    }

    if (!g_config.mountEnabled) {
        printf("[-] DTB looks wrong and MemProcFS mounting is disabled (--no-mount)\n");
        return false;
    }

    if (!InitMemProcFs()) {
        return false;
    }

    if (!VMMDLL_InitializePlugins(hVMM)) {
        printf("[-] Failed VMMDLL_InitializePlugins call\n");
        TerminateMemProcFs();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const std::string progressPath = g_config.mountPoint + "/misc/procinfo/progress_percent.txt";
    const std::string dtbPath      = g_config.mountPoint + "/misc/procinfo/dtb.txt";

    for (int wait = 0; wait < 100; wait++) {
        FILE* progress = fopen(progressPath.c_str(), "r");
        if (progress) {
            char bytes[16] = { 0 };
            const size_t got = fread(bytes, 1, sizeof(bytes) - 1, progress);
            fclose(progress);
            if (got > 0 && atoi(bytes) >= 100) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    FILE* dtbFile = fopen(dtbPath.c_str(), "r");
    if (!dtbFile) {
        printf("[!] Failed to open %s\n", dtbPath.c_str());
        TerminateMemProcFs();
        return false;
    }

    std::vector<uint64_t> possible_dtbs;
    char line[512];
    printf("[>] Parsing dtb.txt for PID %lu and suspect DTBs...\n", (unsigned long)process_id);

    while (fgets(line, sizeof(line), dtbFile)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        Info info = {};
        char nameBuf[256] = { 0 };

        if (sscanf(line, "%x %u %llx %llx %255[^\n]",
                   &info.index, &info.process_id,
                   (unsigned long long*)&info.dtb,
                   (unsigned long long*)&info.kernelAddr,
                   nameBuf) >= 4) {

            strncpy(info.name, nameBuf, sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';

            if (info.process_id == 0) {
                possible_dtbs.push_back(info.dtb);
            }
            if (nameBuf[0] != '\0' &&
                (process_name.find(nameBuf) != std::string::npos ||
                 strcasestr(nameBuf, process_name.c_str()) != nullptr)) {
                possible_dtbs.push_back(info.dtb);
            }
            if (info.process_id == process_id) {
                possible_dtbs.push_back(info.dtb);
            }
        }
    }
    fclose(dtbFile);

    printf("[>] Found %zu possible DTBs to try\n", possible_dtbs.size());

    for (size_t i = 0; i < possible_dtbs.size(); i++) {
        const ULONG64 dtb = possible_dtbs[i];
        printf("[>] Trying DTB 0x%llX...\n", (unsigned long long)dtb);

        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_PROCESS_DTB | process_id, dtb);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_ALL, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id,
                                          const_cast<char*>(process_name.c_str()),
                                          &module_entry, 0)) {
            printf("[+] Patched DTB 0x%llX - found %s\n",
                   (unsigned long long)dtb, process_name.c_str());
            VMMDLL_MemFree(module_entry);
            TerminateMemProcFs();
            return true;
        }
    }

    printf("[-] Failed to patch DTB\n");
    TerminateMemProcFs();
    return false;
}
#endif // __linux__

// ---------------------------------------------------------------------------
// Process / module lookup
// ---------------------------------------------------------------------------

uint32_t get_process_id(const std::string& name)
{
    if (!hVMM) {
        return 0;
    }

    DWORD dwPID = 0;
    if (!VMMDLL_PidGetFromName(hVMM, const_cast<char*>(name.c_str()), &dwPID)) {
        printf("[!] VMMDLL_PidGetFromName failed for %s\n", name.c_str());
        return 0;
    }
    return dwPID;
}

bool get_process_base_address(const std::string& name, const uint32_t& pid)
{
    if (!hVMM) {
        return false;
    }

    PVMMDLL_MAP_MODULEENTRY entry = nullptr;

    if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, const_cast<char*>(name.c_str()),
                                      &entry, 0)) {
        process_size         = entry->cbImageSize;
        process_base_address = entry->vaBase;
        VMMDLL_MemFree(entry);
        return true;
    }

    // If not found, fix the DTB and try again.
    if (!FixCr3()) {
        return false;
    }

    if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, const_cast<char*>(name.c_str()),
                                      &entry, 0)) {
        process_size         = entry->cbImageSize;
        process_base_address = entry->vaBase;
        VMMDLL_MemFree(entry);
        return true;
    }

    return false;
}

bool GetDLLModuleBase(const uint32_t& pid, const std::string& dllName)
{
    if (!hVMM) {
        return false;
    }

    PVMMDLL_MAP_MODULEENTRY entry = nullptr;

    if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, const_cast<char*>(dllName.c_str()),
                                      &entry, VMMDLL_MODULE_FLAG_NORMAL)) {
        DLL_base_address = entry->vaBase;
        DLL_size         = entry->cbImageSize;
        VMMDLL_MemFree(entry);
        printf("[+] DLL %s: Base=0x%llX, Size=0x%lX\n",
               dllName.c_str(), (unsigned long long)DLL_base_address,
               (unsigned long)DLL_size);
        return true;
    }

    if (!FixCr3()) {
        return false;
    }

    if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, const_cast<char*>(dllName.c_str()),
                                      &entry, VMMDLL_MODULE_FLAG_NORMAL)) {
        DLL_base_address = entry->vaBase;
        DLL_size         = entry->cbImageSize;
        VMMDLL_MemFree(entry);
        printf("[+] DLL %s: Base=0x%llX, Size=0x%lX\n",
               dllName.c_str(), (unsigned long long)DLL_base_address,
               (unsigned long)DLL_size);
        return true;
    }

    printf("[!] Failed to find %s\n", dllName.c_str());
    return false;
}

std::vector<int> GetPidListFromName(const std::string& name)
{
    std::vector<int> list;

    if (!hVMM) {
        return list;
    }

    PVMMDLL_PROCESS_INFORMATION info = nullptr;
    DWORD total = 0;

    if (!VMMDLL_ProcessGetInformationAll(hVMM, &info, &total)) {
        printf("[!] Failed to get process list\n");
        return list;
    }

    for (DWORD i = 0; i < total; i++) {
        if (strstr(info[i].szNameLong, name.c_str())) {
            list.push_back((int)info[i].dwPID);
        }
    }

    // The VMM owns this buffer and it was never released here.
    VMMDLL_MemFree(info);
    return list;
}

void DebugAllModules()
{
    if (!hVMM) {
        return;
    }

    PVMMDLL_MAP_MODULE pModuleMap = nullptr;
    if (!VMMDLL_Map_GetModuleU(hVMM, process_id, &pModuleMap, 0)) {
        printf("[!] Failed to get module list\n");
        return;
    }

    printf("\n[>] Modules in process %lu:\n", (unsigned long)process_id);
    printf("=================================================================\n");

    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        const PVMMDLL_MAP_MODULEENTRY entry = pModuleMap->pMap + i;
        printf("[%3lu] 0x%-14llX 0x%-12lX %s\n",
               (unsigned long)i,
               (unsigned long long)entry->vaBase,
               (unsigned long)entry->cbImageSize,
               entry->uszText);
    }

    printf("[+] Total modules: %lu\n", (unsigned long)pModuleMap->cMap);

    // Large mapped images, which are usually the main executable. This used to
    // hardcode the module names of one specific game.
    printf("\n[>] Largest mapped images:\n");
    printf("=================================================================\n");

    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        const PVMMDLL_MAP_MODULEENTRY entry = pModuleMap->pMap + i;
        if (entry->cbImageSize > 0x1000000) {   // > 16 MB
            printf("  -> 0x%-14llX 0x%-12lX (%llu MB) %s\n",
                   (unsigned long long)entry->vaBase,
                   (unsigned long)entry->cbImageSize,
                   (unsigned long long)(entry->cbImageSize / (1024 * 1024)),
                   entry->uszText);
        }
    }

    VMMDLL_MemFree(pModuleMap);
}

// ---------------------------------------------------------------------------
// Pattern scanning
// ---------------------------------------------------------------------------

namespace {

// "48 8B ?? 89" -> { 0x48, 0x8B, -1, 0x89 }
std::vector<int> PatternToBytes(const char* pattern) {
    std::vector<int> bytes;
    if (!pattern) {
        return bytes;
    }

    const char* current = pattern;
    while (*current) {
        if (*current == ' ') {
            current++;
            continue;
        }
        if (*current == '?') {
            current++;
            if (*current == '?') {
                current++;
            }
            bytes.push_back(-1);
            continue;
        }

        char* end = nullptr;
        const long value = strtol(current, &end, 16);
        if (end == current) {
            break;   // not parseable, stop rather than loop forever
        }
        bytes.push_back((int)(value & 0xFF));
        current = end;
    }
    return bytes;
}

} // namespace

#ifdef _WIN32
uintptr_t PatternScan1(void* module, const char* signature, const char* sectionName, int skip)
{
    if (!module) {
        return 0;
    }

    const std::vector<int> patternBytes = PatternToBytes(signature);
    const size_t patternSize = patternBytes.size();
    if (patternSize == 0) {
        return 0;
    }

    const int* pattern = patternBytes.data();
    int currentSkip = 0;

    auto dosHeader = (PIMAGE_DOS_HEADER)module;
    auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);

    if (!sectionName) {
        const size_t sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        // Unsigned underflow guard: sizeOfImage - patternSize wraps to a huge
        // value when the pattern is longer than the image.
        if (sizeOfImage < patternSize) {
            return 0;
        }
        auto scanBytes = reinterpret_cast<std::uint8_t*>(module);

        for (size_t i = 0; i <= sizeOfImage - patternSize; ++i) {
            bool found = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (pattern[j] != -1 && scanBytes[i + j] != (std::uint8_t)pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                if (currentSkip < skip) {
                    currentSkip++;
                    continue;
                }
                return (uintptr_t)&scanBytes[i];
            }
        }
        return 0;
    }

    auto sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++sectionHeader) {
        if (strncmp(reinterpret_cast<const char*>(sectionHeader->Name), sectionName,
                    IMAGE_SIZEOF_SHORT_NAME) != 0) {
            continue;
        }

        auto sectionStart = reinterpret_cast<std::uint8_t*>(module) + sectionHeader->VirtualAddress;
        const size_t sectionSize = sectionHeader->Misc.VirtualSize;
        if (sectionSize < patternSize) {
            return 0;
        }

        for (size_t j = 0; j <= sectionSize - patternSize; ++j) {
            bool found = true;
            for (size_t k = 0; k < patternSize; ++k) {
                if (pattern[k] != -1 && sectionStart[j + k] != (std::uint8_t)pattern[k]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                if (currentSkip < skip) {
                    currentSkip++;
                    continue;
                }
                return (uintptr_t)&sectionStart[j];
            }
        }
        break;
    }
    return 0;
}
#endif // _WIN32

uintptr_t PatternScan(const char* signature)
{
    const uintptr_t moduleBase = (uintptr_t)DLL_base_address;
    const size_t    moduleSize = (size_t)DLL_size;

    if (!moduleBase || moduleSize == 0) {
        printf("[!] PatternScan: no DLL selected (call GetDLLModuleBase first)\n");
        return 0;
    }

    const std::vector<int> patternBytes = PatternToBytes(signature);
    const size_t patternSize = patternBytes.size();

    if (patternSize == 0) {
        printf("[!] PatternScan: empty or malformed signature\n");
        return 0;
    }
    // Guard the unsigned subtraction below.
    if (moduleSize < patternSize) {
        printf("[!] PatternScan: signature is longer than the module\n");
        return 0;
    }

    std::vector<uint8_t> moduleBuffer(moduleSize);
    if (!VMMDLL_MemReadEx(hVMM, process_id, moduleBase, moduleBuffer.data(),
                          (DWORD)moduleSize, NULL, VMMDLL_FLAG_NOCACHE)) {
        printf("[!] PatternScan: failed to read module memory\n");
        return 0;
    }

    printf("[>] Scanning pattern in 0x%zX bytes...\n", moduleSize);

    const int* pattern = patternBytes.data();

    for (size_t i = 0; i <= moduleSize - patternSize; ++i) {
        bool found = true;
        for (size_t j = 0; j < patternSize; ++j) {
            if (pattern[j] != -1 && moduleBuffer[i + j] != (uint8_t)pattern[j]) {
                found = false;
                break;
            }
        }

        if (found) {
            const uintptr_t address = moduleBase + i;
            printf("[+] Pattern found at 0x%llX (module + 0x%zX)\n",
                   (unsigned long long)address, i);
            return address;
        }
    }

    printf("[-] Pattern not found in module\n");
    return 0;
}
