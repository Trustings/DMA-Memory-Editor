#pragma once
#define NOMINMAX
#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>

typedef union _IMAGE_NT_HEADERS_WIN_UNION {
    DWORD Signature;
    IMAGE_NT_HEADERS32 Headers32;
    IMAGE_NT_HEADERS64 Headers64;
} IMAGE_NT_HEADERS_WIN_UNION, * PIMAGE_NT_HEADERS_WIN_UNION;

typedef union _IMAGE_OPTIONAL_HEADER_WIN_UNION {
    IMAGE_OPTIONAL_HEADER32 OptionalHeader32;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader64;
} IMAGE_OPTIONAL_HEADER_WIN_UNION, * PIMAGE_OPTIONAL_HEADER_WIN_UNION;

#elif defined (__linux__)
//#define LINUX
#include <filesystem>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include "sys/mount.h"
#endif

#include <string.h>
#include <string_view>
#include <memory>
#include <fstream>
#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include "vmmdll.h"
#include <iostream>
#include <vector>
#include <algorithm>

extern uint32_t pid;
extern uint64_t gafAsyncKeyStateExport;
extern uint8_t state_bitmap[];
extern uint8_t previous_state_bitmap[];
extern uint64_t win32kbase;

extern int win_logon_pid;

//e_registry_type registry;

extern std::chrono::time_point<std::chrono::system_clock> start;

struct Info
{
    uint32_t index;
    uint32_t process_id;
    uint64_t dtb;
    uint64_t kernelAddr;
    char name[256];
};

extern VMM_HANDLE hVMM;
extern std::string process_name;
extern std::string DLL_Name;
extern uint32_t process_id;
extern HANDLE process_handle;
extern ULONG64 process_base_address;
extern ULONG64 DLL_base_address;
extern DWORD process_size;
extern DWORD DLL_size;

//#define MEMPROCFS

#ifdef MEMPROCFS
#define FUSE_USE_VERSION 30
extern "C" {
#include "charutil.h"
#include "vfslist.h"
#include "version.h"
#include <fuse.h>
#include <signal.h>
}


typedef struct tdFUSE_INFO {
    struct fuse* pfuse;
    char* szMountPoint;
    struct fuse_chan *pchan;
} FUSE_INFO;


//-----------------------------------------------------------------------------
// FUSE FILE SYSTEM FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

#define FILETIME_TO_UNIX(ft)        (time_t)((ft) / 10000000ULL - 11644473600ULL)
#define VER_OSARCH                  "Linux"

int vfs_getattr(const char *uszPathFull, struct stat *st);


typedef struct td_readdir_cb_ctx {
    void *buffer;
    fuse_fill_dir_t filler;
} readdir_cb_ctx, *preaddir_cb_ctx;

void vfs_readdir_cb(_In_ PVFS_ENTRY pVfsEntry, _In_opt_ preaddir_cb_ctx ctx);

int vfs_readdir(const char *uszPath, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);

int vfs_read(const char *uszPath, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);

int vfs_truncate(const char *path, off_t size);

int vfs_write(const char *uszPath, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);

int vfs_initialize_and_mount_displayinfo(char *szMountPoint);

_Success_(return) BOOL MemProcFS_VfsListU(_In_ LPSTR uszPath, _Inout_ PVMMDLL_VFS_FILELIST2 pFileList);

void signal_handler_execute(int signo);

VOID GetMountPoint(_In_ DWORD argc, _In_ char *argv[], _Out_ LPSTR *pszMountPoint, _Out_ PBOOL pfPythonExec);

VOID Vfs_InitializeAndMount_DisplayInfo(_In_ LPSTR uszMountPoint);

int memprocfs(LPSTR *mount_point);

#endif

bool Initialize();

bool InitializeDLL(const std::string process_name, const std::string DLL_Name);

VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);

bool vmmdll_read(uint64_t address, void* buffer, size_t size);

bool vmmdll_write(uint64_t address, void* buffer, size_t size);

template <typename T>
T dma_read(void* address)
{
    T buffer{ };
    memset(&buffer, 0, sizeof(T));
    vmmdll_read(reinterpret_cast<uint64_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T));

    return buffer;
}

template <typename T>
T dma_read(uint64_t address)
{
    return dma_read<T>(reinterpret_cast<void*>(address));
}

template <typename T>
T dma_read(uint64_t address, uint32_t pid)
{
    return dma_read<T>(address);
}

template <typename T>
bool dma_write(void* address, T value)
{
    return vmmdll_write(address, &value, sizeof(T));
}

template <typename T>
bool dma_write(uintptr_t address, T value)
{
    return vmmdll_write(address, &value, sizeof(T));
}


bool read_buffer(uintptr_t address, void* buffer, size_t size);

VMMDLL_SCATTER_HANDLE CreateScatterHandle();

void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);

void AddScatterRead(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);

void ExecuteScatterRead(VMMDLL_SCATTER_HANDLE handle);

bool vmmdll_write(uint64_t address, void* buffer, size_t size);

template<class T> T dma_write(uintptr_t address);

uint32_t get_process_id(const std::string process_name);

bool get_process_base_address(const std::string process_name, const uint32_t& process_id);

bool GetDLLModuleBase(const uint32_t& process_id, const std::string DLL_Name);

bool DumpExe();

bool DumpDLL();

uintptr_t PatternScan1(void* module, const char* signature, const char* sectionName = nullptr, int skip = 0);

uintptr_t PatternScan(const char* signature);

bool DumpExe();

bool DumpDLL();

bool InitKeyboard();

void UpdateKeys();

bool IsKeyDown(uint32_t virtual_key_code);

std::vector<int> GetPidListFromName(std::string name);

const char* LPWSTR_TO_CC(LPWSTR in);

LPSTR CC_TO_LPSTR(const char* in);

void DebugAllModules();


