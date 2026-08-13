#include "memory.hpp"

uint32_t pid = 0;

uint64_t gafAsyncKeyStateExport = 0;
uint8_t state_bitmap[64]{ };
uint8_t previous_state_bitmap[256 / 8]{ };
uint64_t win32kbase = 0;

int win_logon_pid = 0;

std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();


VMM_HANDLE hVMM = nullptr;
std::string process_name;
std::string DLL_Name;
uint32_t process_id = 0;
HANDLE process_handle = nullptr;
ULONG64 process_base_address = 0;
ULONG64 DLL_base_address = 0;
DWORD process_size = 0;
DWORD DLL_size = 0;

uint64_t cbSize = 0x80000;

#ifdef LINUX

DWORD GetLastError() {
    return errno;
}

std::vector<pid_t> getPidsByName(const std::string& processName) {
    std::vector<pid_t> pids;

    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (entry.is_directory()) {
            try {
                pid_t pid = std::stoi(entry.path().filename());
                std::ifstream cmdlineFile(entry.path() / "cmdline");
                std::string cmdline;
                if (std::getline(cmdlineFile, cmdline)) {
                    if (cmdline.find(processName) != std::string::npos) {
                        pids.push_back(pid);
                    }
                }
            } catch (...) {
                continue;
            }
        }
    }
    return pids;
}

int get_pids_by_name(const char *processName, pid_t **pids) {
    DIR *proc_dir;
    struct dirent *entry;
    int pid_count = 0;
    int capacity = 10;

    if (!processName || !pids) {
        return -1;
    }

    *pids = NULL;

    proc_dir = opendir("/proc");
    if (!proc_dir) {
        fprintf(stderr, "Failed to open /proc: %s\n", strerror(errno));
        return -1;
    }

    pid_t *found_pids = (pid_t*)malloc(capacity * sizeof(pid_t));
    if (!found_pids) {
        closedir(proc_dir);
        return -1;
    }

    while ((entry = readdir(proc_dir)) != NULL) {
        bool is_pid = true;
        for (int i = 0; entry->d_name[i] != '\0'; i++) {
            if (!isdigit(entry->d_name[i])) {
                is_pid = false;
                break;
            }
        }

        if (!is_pid) {
            continue;
        }

        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0) {
            continue;
        }

        char cmdline_path[256];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

        FILE *cmdline_file = fopen(cmdline_path, "r");
        if (!cmdline_file) {
            continue;
        }

        char cmdline[4096];
        if (fgets(cmdline, sizeof(cmdline), cmdline_file)) {
            if (strstr(cmdline, processName) != NULL) {
                if (pid_count >= capacity) {
                    capacity *= 2;
                    pid_t *temp = (pid_t*)realloc(found_pids, capacity * sizeof(pid_t));
                    if (!temp) {
                        free(found_pids);
                        fclose(cmdline_file);
                        closedir(proc_dir);
                        return -1;
                    }
                    found_pids = temp;
                }
                found_pids[pid_count++] = pid;
            }
        }

        fclose(cmdline_file);
    }

    closedir(proc_dir);

    if (errno != 0) {
        free(found_pids);
        return -1;
    }

    if (pid_count == 0) {
        free(found_pids);
        return 0;
    }

    // Resize to exact size
    pid_t *temp = (pid_t*)realloc(found_pids, pid_count * sizeof(pid_t));
    if (temp) {
        found_pids = temp;
    }

    *pids = found_pids;
    return pid_count;
}

/**
 * Initialize VMM for memory access
 */
bool Initialize() {

    // Get PIDs by name
    pid_t *pids = NULL;
    int pid_count = get_pids_by_name("qemu-system-x86", &pids);

    if (pid_count <= 0) {
        printf("[!] No qemu-system-x86 processes found\n");
        if (pids) free(pids);
        return false;
    }

    // Use the first PID found
    pid_t pid = pids[0];
    printf("[+] Found PID: %d\n", pid);

    // Build URL string
    char url[256];
    snprintf(url, sizeof(url), "qemu://hugepage-pid=%d,qmp=/tmp/qmp-win10.sock", pid);

    printf("[+] Using URL: %s\n", url);

    // Initialize VMM parameters
    const char *Parameters[] = {
        "",
        "-device",
        url,
        "-mount",
        "/mnt/memproc",
        "-v",
        NULL
    };

    LPSTR mount_point = const_cast<LPSTR>("/mnt/memproc");

    int param_count = 6;  // Number of parameters before NULL

    // Initialize VMM
    hVMM = VMMDLL_Initialize(param_count, Parameters);

    if (!hVMM) {
        printf("[!] Failed to initialize memory process file system in call to VMMDLL_Initialize\n");
        free(pids);
        return false;
    }

    printf("[+] Successfully initialized VMM\n");

    if (!VMMDLL_InitializePlugins(hVMM))
    {
        printf("[-] Failed VMMDLL_InitializePlugins call\n");

        return false;
    }

    // Clean up pids array
    free(pids);

#ifdef MEMPROCFS
    memprocfs(&mount_point);
#endif

    return true;
}

bool InitializeDLL(const std::string process_name, const std::string DLL_Name)
{

    printf("[+] Process id: %d\n", process_id);

    if (!process_id)
    {
        printf("[!] Failed to get process id of %s\n", process_name);

    }

    if (!GetDLLModuleBase(process_id, DLL_Name))
    {
        printf("[+] Failed to get base address/size of process 0x%lX (Error: %d)\n", DLL_base_address, GetLastError());

    }

    printf("[+] Base address: 0x%llX\n", DLL_base_address);
    printf("[+] Image size: 0x%llX\n", DLL_size);

    return true;

}

#endif

#ifdef _WIN32
bool Initialize()
{

    LPCSTR Parameters[] = { "", "-device", "fpga" };

    hVMM = VMMDLL_Initialize(3, Parameters);
    DWORD error_code;

    if (!hVMM) {
        printf("[!] Failed to initialize memory process file system in call to vmm.dll!VMMDLL_Initialize (Error: %d)\n", GetLastError());
        return false;
    }

    printf("[>] Init handle VMM success\n");

    return true;
}


bool InitializeDLL(const std::string process_name, const std::string DLL_Name)
{

    printf("[+] Process id: %d\n", process_id);

    if (!process_id)
    {
        printf("[!] Failed to get process id of %s\n", process_name);

    }

    if (!GetDLLModuleBase(process_id, DLL_Name))
    {
        printf("[+] Failed to get base address/size of process 0x%lX (Error: %d)\n", DLL_base_address, GetLastError());

    }

    printf("[+] Base address: 0x%llX\n", DLL_base_address);
    printf("[+] Image size: 0x%llX\n", DLL_size);

    return true;

}

#endif

VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo)
{
    if (strcmp(uszName, "dtb.txt") == 0)
        cbSize = cb;
}

bool vmmdll_read(uint64_t address, void* buffer, size_t size) {
    if (!VMMDLL_MemRead(hVMM, (DWORD)process_id, (ULONG64)address, (PBYTE)buffer, size)) {
        DWORD error_code = GetLastError();
        printf("[!] VMMDLL_MemRead failed at address 0x%llX with size %zu (Error: %d)\n", address, size, error_code);
        return false;
    }
    return true;
}

#ifdef _WIN32

bool FixCr3_1()
{
    PVMMDLL_MAP_MODULEENTRY module_entry;
    bool result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, (LPSTR)process_name.c_str(), &module_entry, NULL);
    if (result)
        return true; //Doesn't need to be patched lol

    if (!VMMDLL_InitializePlugins(hVMM))
    {
        ERROR("[-] Failed VMMDLL_InitializePlugins call");
        return false;
    }

    //have to sleep a little or we try reading the file before the plugin initializes fully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (true)
    {
        BYTE bytes[4] = { 0 };
        DWORD i = 0;
        auto nt = VMMDLL_VfsReadW(hVMM, (LPWSTR)L"\\misc\\procinfo\\progress_percent.txt", bytes, 3, &i, 0);
        if (nt == VMMDLL_STATUS_SUCCESS && atoi((LPSTR)bytes) == 100)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    VMMDLL_VFS_FILELIST2 VfsFileList;
    VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
    VfsFileList.h = 0;
    VfsFileList.pfnAddDirectory = 0;
    VfsFileList.pfnAddFile = cbAddFile; //dumb af callback who made this system

    result = VMMDLL_VfsListU(hVMM, (LPSTR)"\\misc\\procinfo\\", &VfsFileList);
    if (!result)
        return false;

    //read the data from the txt and parse it
    const size_t buffer_size = cbSize;
    std::unique_ptr<BYTE[]> bytes(new BYTE[buffer_size]);
    DWORD j = 0;
    auto nt = VMMDLL_VfsReadW(hVMM, (LPWSTR)L"\\misc\\procinfo\\dtb.txt", bytes.get(), buffer_size - 1, &j, 0);
    if (nt != VMMDLL_STATUS_SUCCESS)
        return false;

    std::vector<uint64_t> possible_dtbs;
    std::string lines(reinterpret_cast<char*>(bytes.get()));
    std::istringstream iss(lines);
    std::string line;

    while (std::getline(iss, line))
    {
        Info info = { };

        std::istringstream info_ss(line);
        if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id >> std::hex >> info.dtb >> info.kernelAddr >> info.name)
        {
            if (info.process_id == 0) //parts that lack a name or have a NULL pid are suspects
                possible_dtbs.push_back(info.dtb);
            if (process_name.find(info.name) != std::string::npos)
                possible_dtbs.push_back(info.dtb);
        }
    }

    //loop over possible dtbs and set the config to use it til we find the correct one
    for (size_t i = 0; i < possible_dtbs.size(); i++)
    {
        auto dtb = possible_dtbs[i];
        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_PROCESS_DTB | process_id, dtb);
        result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, (LPSTR)process_name.c_str(), &module_entry, NULL);
        if (result)
        {
            printf("Patched DTB");
            return true;
        }
    }

    ERROR("[-] Failed to patch module");
    return false;
}

#endif

VMMDLL_SCATTER_HANDLE CreateScatterHandle()
{
    return VMMDLL_Scatter_Initialize(hVMM, process_id, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
}

void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
    VMMDLL_Scatter_CloseHandle(handle);
}

void AddScatterRead(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
    VMMDLL_Scatter_PrepareEx(handle, address, size, static_cast<PBYTE>(buffer), NULL);
}

void ExecuteScatterRead(VMMDLL_SCATTER_HANDLE handle)
{
    VMMDLL_Scatter_ExecuteRead(handle);
    VMMDLL_Scatter_Clear(handle, process_id, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
}

bool vmmdll_write(uint64_t address, void* buffer, size_t size) {
    if (!VMMDLL_MemWrite(hVMM, (DWORD)process_id, (ULONG64)address, (PBYTE)buffer, size)) {
        DWORD error_code = GetLastError();
        printf("[!] VMMDLL_MemWrite failed at address 0x%llX with size %zu (Error: %d)\n", address, size, error_code);
        return false;
    }

    return true;
}

uint32_t get_process_id(const std::string process_name)
{
    DWORD dwPID;
    bool result = VMMDLL_PidGetFromName(hVMM, const_cast<char*>(process_name.c_str()), &dwPID);
    if (!result) {
        printf("[!] VMMDLL_PidGetFromName failed (Error: %d)\n", GetLastError());
        return 0;
    }
    return dwPID;
}

#ifdef LINUX
static void force_unmount(const std::string& mountPoint = "/mnt/memproc") {
    // Try umount first
    int result = umount(mountPoint.c_str());
    if (result == 0) {
        std::cout << "Successfully unmounted " << mountPoint << std::endl;
        return;
    }

    // If umount fails, try lazy umount
    result = umount2(mountPoint.c_str(), MNT_DETACH);
    if (result == 0) {
        std::cout << "Lazy unmounted " << mountPoint << std::endl;
        return;
    }

    // If still failing, try force umount with fusermount
    std::string cmd = "fusermount -uz " + mountPoint + " 2>/dev/null";
    if (system(cmd.c_str()) == 0) {
        std::cout << "Force unmounted " << mountPoint << " with fusermount" << std::endl;
        return;
    }

    // Last resort: try to kill any remaining fuse processes
    std::string killCmd = "pkill -f 'memprocfs.*" + mountPoint + "' 2>/dev/null";
    system(killCmd.c_str());

    std::cout << "[+] Attempted to clean up " << mountPoint << std::endl;
}

// Kill any existing memprocfs processes
static void kill_existing_memprocfs() {
    // Kill any existing memprocfs processes
    std::string killCmd = "pkill -f 'memprocfs.*-mount /mnt/memproc' 2>/dev/null";
    system(killCmd.c_str());

    // Force unmount
    force_unmount("/mnt/memproc");

    // Wait a moment for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

static pid_t g_memprocPid = -1;
// Launch memprocfs with suppressed output
static bool init_memprocfs(const std::string& qmpSocket = "/tmp/qmp-win10-1.sock") {
    // First, clean up any old mount and kill existing instances
    kill_existing_memprocfs();

    // Get QEMU process IDs
    auto pids = getPidsByName("qemu-system-x86");

    if (pids.empty()) {
        std::cerr << "No QEMU process found" << std::endl;
        return false;
    }

    pid_t qemuPid = pids[0];
    std::cout << "[+] Found QEMU PID: " << qemuPid << std::endl;

    // Build the URL
    std::string url = "qemu://hugepage-pid=" + std::to_string(qemuPid) + ",qmp=" + qmpSocket;
    std::cout << "[>] Launching: ./memprocfs -device " << url << " -mount /mnt/memproc -v" << std::endl;

    // Fork and execute
    pid_t childPid = fork();

    if (childPid == -1) {
        std::cerr << "Failed to fork process" << std::endl;
        return false;
    }

    if (childPid == 0) {
        // Child process - execute memprocfs with output suppressed
        // Redirect stdout and stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        static char char_buff[PATH_MAX];
        static char dir_buffer[PATH_MAX];
        static char* active_build_directory;

        getcwd(char_buff, sizeof(char_buff));
        sprintf(dir_buffer, "%s/memprocfs", char_buff);

        active_build_directory = dir_buffer;

        execlp(active_build_directory,
            "memprocfs",
            "-device",
            url.c_str(),
            "-mount",
            "/mnt/memproc",
            "-v",
            NULL);

        // If we get here, execlp failed
        std::cerr << "Failed to execute memprocfs: " << strerror(errno) << std::endl;
        exit(1);
    }

    // Store the PID globally
    g_memprocPid = childPid;

    // Wait 3 seconds for it to initialize
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Check if process is still running
    if (kill(childPid, 0) == 0) {
        std::cout << "[+] memprocfs launched successfully with PID: " << childPid << std::endl;
        return true;
    }
    else {
        std::cerr << "[!] memprocfs terminated during startup" << std::endl;
        g_memprocPid = -1;
        return false;
    }
}

// Terminate memprocfs with proper cleanup
static void terminate_memprocfs() {
    if (g_memprocPid <= 0) {
        std::cout << "memprocfs not running or already terminated" << std::endl;
        return;
    }

    std::cout << "[>] Terminating memprocfs (PID: " << g_memprocPid << ")" << std::endl;

    // First, try to unmount cleanly
    std::cout << "[>] Unmounting /mnt/memproc..." << std::endl;
    int umountResult = umount("/mnt/memproc");
    if (umountResult != 0) {
        // Try lazy unmount
        umount2("/mnt/memproc", MNT_DETACH);
        std::cout << "[+] Used lazy unmount" << std::endl;
    }
    else {
        std::cout << "[+] Unmounted successfully" << std::endl;
    }

    // Try graceful termination
    if (kill(g_memprocPid, SIGTERM) == 0) {
        // Wait up to 3 seconds for graceful termination
        int status;
        for (int i = 0; i < 15; i++) {
            pid_t result = waitpid(g_memprocPid, &status, WNOHANG);
            if (result == g_memprocPid) {
                std::cout << "[+] memprocfs terminated gracefully" << std::endl;
                g_memprocPid = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // If still running, force kill
        std::cout << "[!] memprocfs didn't respond to SIGTERM, forcing kill..." << std::endl;
        if (kill(g_memprocPid, SIGKILL) == 0) {
            waitpid(g_memprocPid, &status, 0);
            std::cout << "[+] memprocfs force killed" << std::endl;
        }
    }
    else {
        // Process doesn't exist anymore
        std::cerr << "[+] memprocfs already terminated" << std::endl;
    }

    // Final cleanup - force unmount if still mounted
    force_unmount("/mnt/memproc");

    g_memprocPid = -1;
}

bool FixCr3_1()
{
    init_memprocfs();

    // First try direct lookup
    PVMMDLL_MAP_MODULEENTRY module_entry;
    if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(process_name.c_str()), &module_entry, NULL))
    {
        printf("[+] DTB already correct\n");
        VMMDLL_MemFree(module_entry);

        terminate_memprocfs();
        return true;
    }

    if (!VMMDLL_InitializePlugins(hVMM))
    {
        printf("[-] Failed VMMDLL_InitializePlugins call\n");

        terminate_memprocfs();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Wait for progress
    for (int wait = 0; wait < 100; wait++)
    {
        FILE* progress_file = fopen("/mnt/memproc/misc/procinfo/progress_percent.txt", "r");
        if (progress_file)
        {
            char bytes[16] = { 0 };
            if (fread(bytes, 1, 15, progress_file) > 0 && atoi(bytes) >= 100)
            {
                fclose(progress_file);
                break;
            }
            fclose(progress_file);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Read dtb.txt and collect possible DTBs
    std::vector<uint64_t> possible_dtbs;

    FILE* dtb_file = fopen("/mnt/memproc/misc/procinfo/dtb.txt", "r");
    if (!dtb_file)
    {
        printf("[!] Failed to open dtb.txt\n");

        terminate_memprocfs();
        return false;
    }

    char line[512];
    printf("[>] Parsing dtb.txt for PID %d and suspect DTBs...\n", process_id);

    while (fgets(line, sizeof(line), dtb_file))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        Info info = {};
        char name_buf[256] = { 0 };

        if (sscanf(line, "%x %d %llx %llx %255[^\n]",
            &info.index, &info.process_id,
            &info.dtb, &info.kernelAddr,
            name_buf) >= 4)
        {
            strncpy(info.name, name_buf, sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';

            if (info.process_id == 0)
            {
                possible_dtbs.push_back(info.dtb);
                printf("[DBG] Suspect DTB (PID 0): 0x%llX\n", info.dtb);
            }

            // Check if name matches our process
            if (strlen(name_buf) > 0 &&
                (process_name.find(name_buf) != std::string::npos ||
                    strcasestr(name_buf, process_name.c_str()) != NULL))
            {
                possible_dtbs.push_back(info.dtb);
                printf("[DBG] Name match DTB (%s): 0x%llX\n", name_buf, info.dtb);
            }

            // Also check if this is our PID
            if (info.process_id == (uint32_t)process_id)
            {
                possible_dtbs.push_back(info.dtb);
                printf("[DBG] PID match DTB: 0x%llX\n", info.dtb);
            }
        }
    }
    fclose(dtb_file);

    printf("[>] Found %zu possible DTBs to try\n", possible_dtbs.size());

    // Try each DTB
    for (size_t i = 0; i < possible_dtbs.size(); i++)
    {
        ULONG64 dtb = possible_dtbs[i];
        printf("[>] Trying DTB 0x%llX...\n", dtb);

        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_PROCESS_DTB | process_id, dtb);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_ALL, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(process_name.c_str()), &module_entry, NULL))
        {
            printf("[+] Patched DTB: 0x%llX - Found %s!\n", dtb, process_name.c_str());
            VMMDLL_MemFree(module_entry);

            terminate_memprocfs();
            return true;
        }
    }

    printf("[-] Failed to patch DTB\n");

    terminate_memprocfs();
    return false;
}
#endif

bool get_process_base_address(const std::string process_name, const uint32_t& process_id)
{
    PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;

    bool result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(process_name.c_str()), &pModuleEntryExplorer, NULL);

    if (result) {
        process_size = pModuleEntryExplorer->cbImageSize;
        process_base_address = pModuleEntryExplorer->vaBase;
        VMMDLL_MemFree(pModuleEntryExplorer);
        return true;
    }

    // If not found, fix DTB and try again
    if (!FixCr3_1())
        return false;

    result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(process_name.c_str()), &pModuleEntryExplorer, NULL);

    if (result) {
        process_size = pModuleEntryExplorer->cbImageSize;
        process_base_address = pModuleEntryExplorer->vaBase;
        VMMDLL_MemFree(pModuleEntryExplorer);
        return true;
    }

    return false;
}

bool GetDLLModuleBase(const uint32_t& process_id, const std::string DLL_Name)
{
    PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;

    bool result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(DLL_Name.c_str()), &pModuleEntryExplorer, VMMDLL_MODULE_FLAG_NORMAL);

    if (result) {
        DLL_base_address = pModuleEntryExplorer->vaBase;
        DLL_size = pModuleEntryExplorer->cbImageSize;
        VMMDLL_MemFree(pModuleEntryExplorer);

        printf("[+] DLL %s: Base=0x%llX, Size=0x%llX\n", DLL_Name.c_str(), DLL_base_address, DLL_size);
        return true;
    }

    // If not found, fix DTB and try again
    if (!FixCr3_1())
        return false;

    result = VMMDLL_Map_GetModuleFromNameU(hVMM, process_id, const_cast<char*>(DLL_Name.c_str()), &pModuleEntryExplorer, VMMDLL_MODULE_FLAG_NORMAL);

    if (result) {
        DLL_base_address = pModuleEntryExplorer->vaBase;
        DLL_size = pModuleEntryExplorer->cbImageSize;
        VMMDLL_MemFree(pModuleEntryExplorer);

        printf("[+] DLL %s: Base=0x%llX, Size=0x%llX\n", DLL_Name.c_str(), DLL_base_address, DLL_size);
        return true;
    }

    printf("[!] Failed to find %s\n", DLL_Name.c_str());
    return false;
}

#ifdef _WIN32
uintptr_t PatternScan1(void* module, const char* signature, const char* sectionName, int skip)
{
    static auto pattern_to_byte = [](const char* pattern) {
        auto bytes = std::vector<int>{};
        auto start = const_cast<char*>(pattern);
        auto end = const_cast<char*>(pattern) + strlen(pattern);

        for (auto current = start; current < end; ++current) {
            if (*current == '?') {
                ++current;
                if (*current == '?')
                    ++current;
                bytes.push_back(-1);
            }
            else {
                bytes.push_back(strtoul(current, &current, 16));
            }
        }
        return bytes;
    };

    auto dosHeader = (PIMAGE_DOS_HEADER)module;
    auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);
    auto patternBytes = pattern_to_byte(signature);
    auto s = patternBytes.size();
    auto d = patternBytes.data();
    int currentskip = 0;

    if (!sectionName) {
        auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        auto scanBytes = reinterpret_cast<std::uint8_t*>(module);

        for (auto i = 0ul; i < sizeOfImage - s; ++i) {
            if (currentskip < skip) {
                currentskip++;
                continue;
            }

            bool found = true;
            for (auto j = 0ul; j < s; ++j) {
                if (scanBytes[i + j] != d[j] && d[j] != -1) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return (uintptr_t)&scanBytes[i];
            }
        }
    }
    else {
        auto sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
        for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++sectionHeader) {
            if (strncmp(reinterpret_cast<const char*>(sectionHeader->Name), sectionName, IMAGE_SIZEOF_SHORT_NAME) == 0) {
                auto sectionStart = reinterpret_cast<std::uint8_t*>(module) + sectionHeader->VirtualAddress;
                auto sectionSize = sectionHeader->Misc.VirtualSize;

                for (auto j = 0ul; j < sectionSize - s; ++j) {
                    bool found = true;
                    for (auto k = 0ul; k < s; ++k) {
                        if (sectionStart[j + k] != d[k] && d[k] != -1) {
                            found = false;
                            break;
                        }
                    }
                    if (found) {
                        if (currentskip < skip) {
                            currentskip++;
                            continue;
                        }
                        return (uintptr_t)&sectionStart[j];
                    }
                }
                break; // Stop searching if section is found
            }
        }
    }
    return (uintptr_t)nullptr;
}
#endif

uintptr_t PatternScan(const char* signature)
{
    static auto pattern_to_byte = [](const char* pattern) {
        auto bytes = std::vector<int>{};
        auto start = const_cast<char*>(pattern);
        auto end = const_cast<char*>(pattern) + strlen(pattern);

        for (auto current = start; current < end; ++current) {
            if (*current == '?') {
                ++current;
                if (*current == '?')
                    ++current;
                bytes.push_back(-1);
            }
            else {
                bytes.push_back(strtoul(current, &current, 16));
            }
        }
        return bytes;
    };

    uintptr_t moduleBase = DLL_base_address;
    size_t moduleSize = DLL_size;

    auto patternBytes = pattern_to_byte(signature);
    auto scanSize = patternBytes.size();
    auto patternData = patternBytes.data();

    // Read the entire module into buffer
    std::vector<uint8_t> moduleBuffer(moduleSize);
    if (!VMMDLL_MemReadEx(hVMM, process_id, moduleBase, moduleBuffer.data(), moduleSize, 0, VMMDLL_FLAG_NOCACHE)) {
        printf("[!] PatternScan: Failed to read DLL memory\n");
        return 0;
    }


    printf("[>] Scanning pattern in 0x%zX bytes...\n", moduleSize);

    for (size_t i = 0; i < moduleSize - scanSize; ++i)
    {
        bool found = true;

        for (size_t j = 0; j < scanSize; ++j)
        {
            // FIX: Compare against the actual buffer we read
            if (patternData[j] != -1 && patternData[j] != moduleBuffer[i + j])
            {
                found = false;
                break;
            }
        }

        if (found)
        {
            uintptr_t found_address = moduleBase + i;
            uintptr_t relative_address = found_address - DLL_base_address;

            printf("[+] Pattern found:\n");
            printf("    Absolute: 0x%llX\n", found_address);
            printf("    Relative: 0x%llX\n", relative_address);

            // Debug: show what we found
            printf("    Bytes at location: ");
            for (size_t k = 0; k < (std::min)(scanSize, size_t(16)); k++) {
                printf("%02X ", moduleBuffer[i + k]);
            }
            printf("\n");

            return found_address; // Return absolute for now
        }
    }

    printf("[-] Pattern not found in module\n");
    return 0;
}

std::vector<int> GetPidListFromName(std::string name)
{
    PVMMDLL_PROCESS_INFORMATION process_info = NULL;
    DWORD total_processes = 0;
    std::vector<int> list = { };

    if (!VMMDLL_ProcessGetInformationAll(hVMM, &process_info, &total_processes))
    {
        printf("[!] Failed to get process list\n");
        return list;
    }

    for (size_t i = 0; i < total_processes; i++)
    {
        auto process = process_info[i];
        if (strstr(process.szNameLong, name.c_str()))
            list.push_back(process.dwPID);
    }

    return list;
}

#ifdef _WIN32

#endif


void DebugAllModules()
{
    PVMMDLL_MAP_MODULE pModuleMap = NULL;
    if (!VMMDLL_Map_GetModuleU(hVMM, process_id, &pModuleMap, NULL)) {
        printf("[!] Failed to get module list\n");
        return;
    }

    printf("\n[>] ALL MODULES IN PROCESS %d:\n", process_id);
    printf("=================================================================\n");

    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        PVMMDLL_MAP_MODULEENTRY pEntry = pModuleMap->pMap + i;

        printf("[%3d] 0x%-14llX 0x%-12llX %s\n",
               i,
               pEntry->vaBase,
               pEntry->cbImageSize,
               pEntry->uszText);
    }

    printf("[+] Total modules: %d\n", pModuleMap->cMap);

    // Find potential game executables
    printf("\n[>] POTENTIAL GAME EXECUTABLES:\n");
    printf("=================================================================\n");

    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        PVMMDLL_MAP_MODULEENTRY pEntry = pModuleMap->pMap + i;
        std::string name = pEntry->uszText;

        // Look for game-related names
        if (name.find(".exe") != std::string::npos ||
            name.find("r5apex") != std::string::npos ||
            name.find("apex") != std::string::npos ||
            name.find("R5") != std::string::npos ||
            pEntry->cbImageSize > 0x1000000) {  // > 16MB

            printf("  -> 0x%-14llX 0x%-12llX (%llu MB) %s\n",
                   pEntry->vaBase,
                   pEntry->cbImageSize,
                   pEntry->cbImageSize / (1024 * 1024),
                   pEntry->uszText);
        }
    }

    VMMDLL_MemFree(pModuleMap);
}



void UpdateKeys()
{
    uint8_t previous_key_state_bitmap[64] = { 0 };
    memcpy(previous_key_state_bitmap, state_bitmap, 64);

    VMMDLL_MemReadEx(hVMM, win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, gafAsyncKeyStateExport, (PBYTE)&state_bitmap, 64, NULL, VMMDLL_FLAG_NOCACHE);
    for (int vk = 0; vk < 256; ++vk)
        if ((state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2) && !(previous_key_state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2))
            previous_state_bitmap[vk / 8] |= 1 << vk % 8;
}

bool IsKeyDown(uint32_t virtual_key_code)
{
    if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
        return false;
    if (std::chrono::system_clock::now() - start > std::chrono::milliseconds(5))
    {
        UpdateKeys();
        start = std::chrono::system_clock::now();
    }
    return state_bitmap[(virtual_key_code * 2 / 8)] & 1 << virtual_key_code % 4 * 2;
}

