#pragma once
#define NOMINMAX

#ifdef _WIN32
// WIN32_LEAN_AND_MEAN alone does not keep wingdi.h out, and wingdi.h defines
// ERROR as 0 -- which used to turn a plain ERROR("...") log call in this file
// into 0("..."), breaking the Windows build outright. NOGDI keeps the macro
// out entirely; nothing here draws with GDI.
#ifndef NOGDI
#define NOGDI
#endif
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

#elif defined(__linux__)
#include <filesystem>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/mount.h>
#endif

#if defined(__linux__) && !defined(LINUX)
// MemProcFS gates the Linux half of vmmdll.h (DWORD, QWORD, HANDLE, the
// VMMDLL_* handle types...) on LINUX rather than on __linux__. Define it here
// so the headers are correct however the project is built, instead of relying
// on every build system to remember -DLINUX -- without it this header quietly
// produces hundreds of "DWORD was not declared" errors.
#define LINUX
#endif

#include <string.h>
#include <string_view>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include "vmmdll.h"
#include <iostream>
#include <vector>
#include <algorithm>

struct Info
{
    uint32_t index;
    uint32_t process_id;
    uint64_t dtb;
    uint64_t kernelAddr;
    char name[256];
};

extern VMM_HANDLE  hVMM;
extern std::string process_name;
extern std::string DLL_Name;
extern uint32_t    process_id;
extern HANDLE      process_handle;
extern ULONG64     process_base_address;
extern ULONG64     DLL_base_address;
extern DWORD       process_size;
extern DWORD       DLL_size;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Bring up the VMM against the configured backend. Returns false if the
// backend could not be opened -- callers must not proceed on false, since
// every VMMDLL call below would then run against a null handle.
bool Initialize();

// Tear the VMM down. Safe to call more than once.
void Shutdown();

bool InitializeDLL(const std::string& process_name, const std::string& DLL_Name);

VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb,
               _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo);

// ---------------------------------------------------------------------------
// Reads and writes
// ---------------------------------------------------------------------------

bool vmmdll_read(uint64_t address, void* buffer, size_t size);
bool vmmdll_write(uint64_t address, const void* buffer, size_t size);

template <typename T>
T dma_read(uint64_t address)
{
    T buffer{};
    memset(&buffer, 0, sizeof(T));
    vmmdll_read(address, &buffer, sizeof(T));
    return buffer;
}

template <typename T>
T dma_read(void* address)
{
    return dma_read<T>(reinterpret_cast<uint64_t>(address));
}

template <typename T>
bool dma_write(uint64_t address, T value)
{
    return vmmdll_write(address, &value, sizeof(T));
}

template <typename T>
bool dma_write(void* address, T value)
{
    return dma_write<T>(reinterpret_cast<uint64_t>(address), value);
}

// ---------------------------------------------------------------------------
// Scatter reads
// ---------------------------------------------------------------------------
// Batching reads through the scatter API is the difference between one DMA
// round-trip and hundreds. Buffers passed to AddScatterRead must stay alive
// until CloseScatterHandle().

VMMDLL_SCATTER_HANDLE CreateScatterHandle(uint32_t pid);
void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);
void AddScatterRead(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer,
                    uint32_t size, uint32_t* bytesRead);
bool ExecuteScatterRead(VMMDLL_SCATTER_HANDLE handle, uint32_t pid);

// ---------------------------------------------------------------------------
// Process / module lookup
// ---------------------------------------------------------------------------

uint32_t get_process_id(const std::string& process_name);
bool get_process_base_address(const std::string& process_name, const uint32_t& process_id);
bool GetDLLModuleBase(const uint32_t& process_id, const std::string& DLL_Name);
std::vector<int> GetPidListFromName(const std::string& name);
void DebugAllModules();

// ---------------------------------------------------------------------------
// Pattern scanning
// ---------------------------------------------------------------------------

#ifdef _WIN32
uintptr_t PatternScan1(void* module, const char* signature,
                       const char* sectionName = nullptr, int skip = 0);
#endif

// Scans the attached process's currently selected DLL range for an IDA-style
// signature ("48 8B ?? 89"). Returns 0 when not found.
uintptr_t PatternScan(const char* signature);
