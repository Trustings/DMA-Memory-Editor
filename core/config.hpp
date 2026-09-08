#pragma once
#include <string>

// Runtime configuration.
//
// All of this used to be hardcoded in the middle of Initialize() and the gdb
// helpers -- the QEMU process name, the QMP socket paths, the FUSE mount
// point, the gdb target, and "-device fpga" on Windows. That forced every
// user onto exactly the author's QEMU setup, which is why the README had to
// prescribe specific socket names.
//
// Values are resolved in this order, last wins:
//   1. built-in defaults (below)
//   2. dma-editor.conf, from the working directory or next to the executable
//   3. command line arguments
struct AppConfig {
    // --- backend ----------------------------------------------------------
    // LeechCore device string. Empty means "derive one automatically":
    // on Linux, a qemu:// URL built from the discovered QEMU process, and on
    // Windows, "fpga".
    std::string device;

    // Linux/QEMU autodetection.
    std::string qemuProcessName = "qemu-system-x86";
    std::string qmpSocket       = "/tmp/qmp-win10.sock";

    // --- MemProcFS mount (Linux, used to recover a process DTB) -----------
    bool        mountEnabled    = true;
    std::string mountPoint      = "/mnt/memproc";
    std::string memprocfsPath   = "./memprocfs";
    std::string memprocfsQmp    = "/tmp/qmp-win10-1.sock";

    // --- debugger ---------------------------------------------------------
    std::string gdbPath         = "gdb";
    std::string gdbTarget       = "localhost:1234";
    int         gdbTimeoutMs    = 2000;

    // --- misc -------------------------------------------------------------
    bool        verbose         = false;
};

extern AppConfig g_config;

// Load key=value pairs from `path`. Missing file is not an error.
bool Config_Load(const std::string& path);

// Look for dma-editor.conf in the working directory, then next to argv[0].
void Config_LoadDefaultFile(const char* argv0);

enum class ConfigParse {
    Ok,             // carry on
    ExitSuccess,    // --help was asked for; exit 0
    ExitFailure,    // bad argument; exit non-zero
};

// Parse command line overrides.
ConfigParse Config_ParseArgs(int argc, char** argv);

void Config_Print();
void Config_PrintUsage(const char* argv0);
