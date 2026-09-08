#include "config.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

AppConfig g_config;

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

bool ParseBool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true"  || v == "yes" || v == "on")  return true;
    if (v == "0" || v == "false" || v == "no"  || v == "off") return false;
    return fallback;
}

bool ApplySetting(const std::string& key, const std::string& value) {
    if (key == "device")           { g_config.device = value;           return true; }
    if (key == "qemu_process")     { g_config.qemuProcessName = value;  return true; }
    if (key == "qmp_socket")       { g_config.qmpSocket = value;        return true; }
    if (key == "mount_enabled")    { g_config.mountEnabled = ParseBool(value, g_config.mountEnabled); return true; }
    if (key == "mount_point")      { g_config.mountPoint = value;       return true; }
    if (key == "memprocfs_path")   { g_config.memprocfsPath = value;    return true; }
    if (key == "memprocfs_qmp")    { g_config.memprocfsQmp = value;     return true; }
    if (key == "gdb_path")         { g_config.gdbPath = value;          return true; }
    if (key == "gdb_target")       { g_config.gdbTarget = value;        return true; }
    if (key == "gdb_timeout_ms")   { g_config.gdbTimeoutMs = atoi(value.c_str()); return true; }
    if (key == "verbose")          { g_config.verbose = ParseBool(value, g_config.verbose); return true; }
    return false;
}

std::string DirectoryOf(const char* path) {
    if (!path) return std::string();
    std::string p(path);
    const size_t slash = p.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    return p.substr(0, slash + 1);
}

} // namespace

bool Config_Load(const std::string& path) {
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        lineNo++;
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            printf("[!] %s:%d: expected key=value\n", path.c_str(), lineNo);
            continue;
        }
        const std::string key   = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (!ApplySetting(key, value)) {
            printf("[!] %s:%d: unknown setting '%s'\n", path.c_str(), lineNo, key.c_str());
        }
    }

    printf("[+] Loaded configuration from %s\n", path.c_str());
    return true;
}

void Config_LoadDefaultFile(const char* argv0) {
    if (Config_Load("dma-editor.conf")) {
        return;
    }
    const std::string dir = DirectoryOf(argv0);
    if (!dir.empty()) {
        Config_Load(dir + "dma-editor.conf");
    }
}

void Config_PrintUsage(const char* argv0) {
    printf(
        "Usage: %s [options]\n"
        "\n"
        "  --device <str>        LeechCore device string (default: auto)\n"
        "                        e.g. fpga, qemu://hugepage-pid=1234,qmp=/tmp/q.sock\n"
        "  --qemu-process <str>  QEMU process name to autodetect (default: %s)\n"
        "  --qmp-socket <path>   QMP socket for the qemu device (default: %s)\n"
        "  --mount-point <path>  MemProcFS mount point (default: %s)\n"
        "  --memprocfs <path>    Path to the memprocfs binary (default: %s)\n"
        "  --memprocfs-qmp <p>   QMP socket used by the mount helper (default: %s)\n"
        "  --no-mount            Never mount MemProcFS (skips DTB recovery)\n"
        "  --gdb <path>          gdb binary to use (default: %s)\n"
        "  --gdb-target <t>      gdb remote target (default: %s)\n"
        "  --gdb-timeout <ms>    gdb attach timeout (default: %d)\n"
        "  --config <path>       Load a configuration file\n"
        "  -v, --verbose         Verbose logging\n"
        "  -h, --help            Show this help\n",
        argv0 ? argv0 : "DMA-Memory-Editor",
        g_config.qemuProcessName.c_str(),
        g_config.qmpSocket.c_str(),
        g_config.mountPoint.c_str(),
        g_config.memprocfsPath.c_str(),
        g_config.memprocfsQmp.c_str(),
        g_config.gdbPath.c_str(),
        g_config.gdbTarget.c_str(),
        g_config.gdbTimeoutMs);
}

ConfigParse Config_ParseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];

        // Options that take a value.
        struct { const char* flag; const char* key; } valued[] = {
            { "--device",         "device"         },
            { "--qemu-process",   "qemu_process"   },
            { "--qmp-socket",     "qmp_socket"     },
            { "--mount-point",    "mount_point"    },
            { "--memprocfs",      "memprocfs_path" },
            { "--memprocfs-qmp",  "memprocfs_qmp"  },
            { "--gdb",            "gdb_path"       },
            { "--gdb-target",     "gdb_target"     },
            { "--gdb-timeout",    "gdb_timeout_ms" },
        };

        bool handled = false;
        for (const auto& opt : valued) {
            if (strcmp(a, opt.flag) == 0) {
                if (i + 1 >= argc) {
                    printf("[!] %s requires a value\n", a);
                    return ConfigParse::ExitFailure;
                }
                ApplySetting(opt.key, argv[++i]);
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }

        if (strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) {
                printf("[!] --config requires a path\n");
                return ConfigParse::ExitFailure;
            }
            if (!Config_Load(argv[++i])) {
                printf("[!] Could not open config file %s\n", argv[i]);
                return ConfigParse::ExitFailure;
            }
        } else if (strcmp(a, "--no-mount") == 0) {
            g_config.mountEnabled = false;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            g_config.verbose = true;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            Config_PrintUsage(argv[0]);
            return ConfigParse::ExitSuccess;
        } else {
            printf("[!] Unknown argument: %s\n", a);
            Config_PrintUsage(argv[0]);
            return ConfigParse::ExitFailure;
        }
    }
    return ConfigParse::Ok;
}

void Config_Print() {
    printf("[cfg] device         : %s\n",
           g_config.device.empty() ? "(auto)" : g_config.device.c_str());
    printf("[cfg] qemu process   : %s\n", g_config.qemuProcessName.c_str());
    printf("[cfg] qmp socket     : %s\n", g_config.qmpSocket.c_str());
    printf("[cfg] mount          : %s (%s)\n",
           g_config.mountPoint.c_str(), g_config.mountEnabled ? "enabled" : "disabled");
    printf("[cfg] memprocfs      : %s\n", g_config.memprocfsPath.c_str());
    printf("[cfg] gdb            : %s -> %s\n",
           g_config.gdbPath.c_str(), g_config.gdbTarget.c_str());
}
