#include "debugger.hpp"
#include "config.hpp"

#include <sys/select.h>
#include <string>
#include <vector>

pid_t gdb_pid          = -1;
int   gdb_stdin        = -1;
int   gdb_stdout       = -1;
int   breakpoint_count = 0;
int   watchpoint_count = 0;

uint64_t wp_buffer[MAX_WATCHPOINTS] = { 0 };

static bool g_verbose = true;

struct gdb_state gdb_state_c = { {true}, {false} };

void gdb_set_verbose(bool verbose) {
    g_verbose = verbose;
}

// ---------------------------------------------------------------------------
// Pipe plumbing
// ---------------------------------------------------------------------------

static void safe_close(int* fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        const ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

// Read until gdb prints its "(gdb)" prompt.
//
// This used to read one byte per select() call, which meant two syscalls for
// every single character of a multi-kilobyte `info registers` dump. Now it
// reads in blocks and scans the tail for the prompt.
//
// Returns the response length, or -1 on timeout/error. A timeout must stay
// distinguishable from a successful-but-empty response: gdb_wait_for_stop()
// relies on it to tell "stopped" from "still running".
static int read_until_prompt(int fd, char* buffer, size_t buffer_size, int timeout_ms) {
    static const char PROMPT[] = "(gdb)";
    static const size_t PROMPT_LEN = sizeof(PROMPT) - 1;

    if (fd < 0 || buffer_size == 0) {
        return -1;
    }

    size_t pos = 0;
    memset(buffer, 0, buffer_size);

    const auto start = std::chrono::steady_clock::now();

    while (pos + 1 < buffer_size) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            return -1;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 50000;   // 50 ms

        const int ready = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ready == 0) {
            continue;
        }

        const ssize_t n = read(fd, buffer + pos, buffer_size - pos - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -1;   // gdb closed the pipe
        }

        pos += (size_t)n;
        buffer[pos] = '\0';

        // Only the tail can contain a newly-arrived prompt.
        const size_t scanFrom = (pos > PROMPT_LEN + (size_t)n)
                                    ? pos - PROMPT_LEN - (size_t)n
                                    : 0;
        char* prompt = strstr(buffer + scanFrom, PROMPT);
        if (prompt) {
            *prompt = '\0';
            pos = (size_t)(prompt - buffer);
            while (pos > 0 && (buffer[pos - 1] == '\n' || buffer[pos - 1] == '\r' ||
                               buffer[pos - 1] == ' ')) {
                buffer[--pos] = '\0';
            }
            return (int)pos;
        }
    }

    return -1;   // buffer full without a prompt
}

static int send_gdb_command(const char* cmd, char* response, size_t resp_size, int timeout_ms) {
    if (gdb_stdin < 0 || gdb_stdout < 0) {
        return -1;
    }

    if (g_verbose) {
        printf("[GDB ->] %s\n", cmd);
    }

    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "%s\n", cmd);

    if (write_all(gdb_stdin, full_cmd, strlen(full_cmd)) < 0) {
        return -1;
    }

    // Heap-allocated: this used to be a 64 KB stack buffer per call.
    std::vector<char> output(65536);
    const int len = read_until_prompt(gdb_stdout, output.data(), output.size(), timeout_ms);
    if (len < 0) {
        return -1;
    }

    if (g_verbose && output[0] != '\0') {
        printf("[GDB <-] %s\n", output.data());
    }

    if (response && resp_size > 0) {
        strncpy(response, output.data(), resp_size - 1);
        response[resp_size - 1] = '\0';
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static std::atomic<bool> g_cleanup_done(false);

// Async-signal-safe: no printf, no malloc, no waitpid. Just kill the child so
// gdb tears down its remote connection (which removes anything we planted),
// then re-raise with the default handler so the exit status is honest.
static void gdb_signal_handler(int sig) {
    if (gdb_pid > 0) {
        kill(gdb_pid, SIGTERM);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

int gdb_init(void) {
    if (gdb_pid > 0) {
        return 1;   // already running
    }

    printf("[>] Starting %s -> %s\n", g_config.gdbPath.c_str(), g_config.gdbTarget.c_str());
    g_cleanup_done = false;

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0) {
        perror("pipe");
        return 0;
    }
    if (pipe(stdout_pipe) < 0) {
        perror("pipe");
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return 0;
    }

    gdb_pid = fork();
    if (gdb_pid == -1) {
        perror("fork");
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return 0;
    }

    if (gdb_pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        const std::string target = "target remote " + g_config.gdbTarget;
        execlp(g_config.gdbPath.c_str(), "gdb", "-q", "-nx",
               "-ex", target.c_str(), (char*)NULL);
        perror("execlp");
        _exit(127);
    }

    gdb_stdin  = stdin_pipe[1];
    gdb_stdout = stdout_pipe[0];
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    printf("[+] gdb started with PID %d\n", (int)gdb_pid);

    std::vector<char> response(8192);
    const int len = read_until_prompt(gdb_stdout, response.data(), response.size(),
                                      g_config.gdbTimeoutMs);
    if (len < 0 || strstr(response.data(), "Remote debugging using") == NULL) {
        printf("[!] Failed to attach to %s\n", g_config.gdbTarget.c_str());
        if (len >= 0) {
            printf("%s\n", response.data());
        } else {
            printf("    (no response within %d ms -- is the VM started with "
                   "-gdb tcp::1234, and is gdb installed?)\n", g_config.gdbTimeoutMs);
        }
        gdb_cleanup();
        return 0;
    }
    printf("[+] gdb ready\n");

    send_gdb_command("set pagination off", NULL, 0, 500);
    send_gdb_command("set confirm off", NULL, 0, 500);

    // Clean up after ourselves even on Ctrl-C, so we never leave hardware
    // breakpoints planted on the target or a zombie gdb behind.
    static bool handlersInstalled = false;
    if (!handlersInstalled) {
        atexit(gdb_cleanup);
        signal(SIGINT, gdb_signal_handler);
        signal(SIGTERM, gdb_signal_handler);
        handlersInstalled = true;
    }

    return 1;
}

void gdb_cleanup(void) {
    bool expected = false;
    if (!g_cleanup_done.compare_exchange_strong(expected, true)) {
        return;
    }

    if (gdb_pid > 0 && gdb_stdin >= 0) {
        // In case we are mid-`continue`, stop the target first so gdb can
        // accept commands again.
        gdb_send_interrupt();
        std::vector<char> resp(4096);
        read_until_prompt(gdb_stdout, resp.data(), resp.size(), 500);

        send_gdb_command("delete", NULL, 0, 500);

        // This is a remote target, so gdb does not own the inferior -- detach
        // rather than kill, leaving the guest running as if we had never
        // attached.
        send_gdb_command("detach", NULL, 0, 500);
        send_gdb_command("quit", NULL, 0, 500);
    }

    if (gdb_pid > 0) {
        kill(gdb_pid, SIGTERM);
        int status = 0;
        waitpid(gdb_pid, &status, 0);
        gdb_pid = -1;
    }

    safe_close(&gdb_stdin);
    safe_close(&gdb_stdout);
    breakpoint_count = 0;
    watchpoint_count = 0;
    g_cleanup_done   = false;   // allow a later gdb_init()
}

// ---------------------------------------------------------------------------
// Breakpoints and watchpoints
// ---------------------------------------------------------------------------

int gdb_set_breakpoint_hardware(uint64_t address) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hbreak *0x%llx", (unsigned long long)address);

    std::vector<char> response(4096);
    if (send_gdb_command(cmd, response.data(), response.size(), g_config.gdbTimeoutMs) < 0) {
        return -1;
    }
    if (strstr(response.data(), "Hardware assisted breakpoint") ||
        strstr(response.data(), "Breakpoint")) {
        breakpoint_count++;
        return 0;
    }
    return -1;
}

int gdb_remove_breakpoint(uint64_t address) {
    (void)address;
    send_gdb_command("delete", NULL, 0, g_config.gdbTimeoutMs);
    breakpoint_count = 0;
    return 0;
}

void gdb_clear_all_breakpoints(void) {
    send_gdb_command("delete", NULL, 0, g_config.gdbTimeoutMs);
    breakpoint_count = 0;
}

int gdb_set_watchpoint(uint64_t address, int size, GdbWatchKind kind) {
    // `watch` traps writes only. "Find what accesses this address" needs
    // `awatch`, which traps reads and writes both -- using `watch` here is why
    // read-only accesses never showed up.
    const char* verb = "watch";
    switch (kind) {
    case GDB_WATCH_READ:   verb = "rwatch"; break;
    case GDB_WATCH_ACCESS: verb = "awatch"; break;
    case GDB_WATCH_WRITE:
    default:               verb = "watch";  break;
    }

    // Width matters: `watch *0xADDR` defaults to whatever type gdb infers.
    // Casting makes it explicit.
    const char* cast = "int";
    switch (size) {
    case 1: cast = "char";      break;
    case 2: cast = "short";     break;
    case 8: cast = "long long"; break;
    case 4:
    default: cast = "int";      break;
    }

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "%s *(%s*)0x%llx", verb, cast, (unsigned long long)address);

    std::vector<char> response(4096);
    if (send_gdb_command(cmd, response.data(), response.size(), g_config.gdbTimeoutMs) < 0) {
        return -1;
    }

    if (strstr(response.data(), "watchpoint")  || strstr(response.data(), "Watchpoint")) {
        // x86 exposes only 4 debug registers; gdb silently degrades to a
        // software watchpoint beyond that, which single-steps the whole guest
        // and is unusably slow.
        if (strstr(response.data(), "Watchpoint") &&
            !strstr(response.data(), "Hardware")) {
            printf("[!] gdb fell back to a SOFTWARE watchpoint -- the guest will "
                   "crawl. Clear existing watchpoints first (x86 has 4).\n");
        }
        watchpoint_count++;
        return 0;
    }
    return -1;
}

int gdb_set_watchpoint_hardware(uint64_t address) {
    return gdb_set_watchpoint(address, 4, GDB_WATCH_WRITE);
}

void gdb_clear_all_watchpoints(void) {
    send_gdb_command("delete", NULL, 0, g_config.gdbTimeoutMs);
    watchpoint_count = 0;
}

// ---------------------------------------------------------------------------
// Execution control
// ---------------------------------------------------------------------------

int gdb_continue(void) {
    if (gdb_stdin < 0) {
        return -1;
    }
    const char* cmd = "continue\n";
    if (g_verbose) {
        printf("[GDB ->] continue\n");
    }
    return write_all(gdb_stdin, cmd, strlen(cmd));
}

int gdb_single_step(void) {
    return send_gdb_command("stepi", NULL, 0, g_config.gdbTimeoutMs) == 0 ? 0 : -1;
}

int gdb_wait_for_stop(int timeout_ms) {
    if (gdb_stdout < 0) {
        return 0;
    }

    // Do NOT send any command here. gdb is currently blocked executing
    // `continue` and will not read new stdin commands until it regains control
    // (breakpoint/signal hit). Polling commands would just queue up unread and
    // then be drained all at once, so we would read the response to some
    // earlier queued command instead of the current state.
    //
    // gdb prints the stop banner and a fresh prompt on its own the moment it
    // stops, with no input required from us. Just wait for it.
    std::vector<char> response(16384);
    const int len = read_until_prompt(gdb_stdout, response.data(), response.size(), timeout_ms);
    if (len < 0) {
        return 0;   // timed out, still running (or pipe error)
    }

    if (g_verbose && response[0] != '\0') {
        printf("[GDB <-] %s\n", response.data());
    }
    return 1;
}

void gdb_send_interrupt(void) {
    // gdb_stdin is a pipe, not a TTY, so writing a raw 0x03 does nothing --
    // the terminal line discipline is what turns Ctrl-C into SIGINT, and pipes
    // do not do that translation. Signal the process directly.
    if (gdb_pid > 0) {
        kill(gdb_pid, SIGINT);
    }
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

uint64_t gdb_read_register(const char* reg_name) {
    if (!reg_name || !*reg_name) {
        return 0;
    }

    // `p/x $reg` returns exactly one value ("$1 = 0x7ff..."), so there is
    // nothing else in the output to match by accident. Searching the whole
    // `info registers` dump for a register name used to hit the stop banner
    // above it ("Hardware watchpoint 1: ... Old value = ...") and return that
    // number instead.
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "p/x $%s", reg_name);

    std::vector<char> response(8192);
    if (send_gdb_command(cmd, response.data(), response.size(), g_config.gdbTimeoutMs) < 0) {
        return 0;
    }

    const char* eq = strstr(response.data(), "= ");
    if (!eq) {
        return 0;
    }
    const char* p = eq + 2;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        return strtoull(p, NULL, 16);
    }
    return strtoull(p, NULL, 10);
}

uint64_t gdb_read_register_verbose(const char* reg_name) {
    const uint64_t value = gdb_read_register(reg_name);
    printf("[+] %s = 0x%llx\n", reg_name, (unsigned long long)value);
    return value;
}

void gdb_set_register(const char* reg_name, uint64_t value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "set $%s = 0x%llx", reg_name, (unsigned long long)value);
    send_gdb_command(cmd, NULL, 0, g_config.gdbTimeoutMs);
}

void gdb_set_rip(uint64_t rip) {
    gdb_set_register("rip", rip);
}

void gdb_save_context(DebuggerContext* ctx) {
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));

    std::vector<char> response(32768);
    if (send_gdb_command("info registers", response.data(), response.size(),
                         g_config.gdbTimeoutMs) < 0) {
        return;
    }

    char* saveptr = NULL;
    char* line = strtok_r(response.data(), "\n", &saveptr);
    while (line) {
        char reg_name[32] = { 0 };
        unsigned long long value = 0;

        // %31s, not %s: an unexpectedly long token would otherwise run off
        // the end of reg_name.
        if (sscanf(line, "%31s 0x%llx", reg_name, &value) == 2) {
            if      (!strcmp(reg_name, "rax")) ctx->rax = value;
            else if (!strcmp(reg_name, "rbx")) ctx->rbx = value;
            else if (!strcmp(reg_name, "rcx")) ctx->rcx = value;
            else if (!strcmp(reg_name, "rdx")) ctx->rdx = value;
            else if (!strcmp(reg_name, "rsi")) ctx->rsi = value;
            else if (!strcmp(reg_name, "rdi")) ctx->rdi = value;
            else if (!strcmp(reg_name, "rbp")) ctx->rbp = value;
            else if (!strcmp(reg_name, "rsp")) ctx->rsp = value;
            else if (!strcmp(reg_name, "r8"))  ctx->r8  = value;
            else if (!strcmp(reg_name, "r9"))  ctx->r9  = value;
            else if (!strcmp(reg_name, "r10")) ctx->r10 = value;
            else if (!strcmp(reg_name, "r11")) ctx->r11 = value;
            else if (!strcmp(reg_name, "r12")) ctx->r12 = value;
            else if (!strcmp(reg_name, "r13")) ctx->r13 = value;
            else if (!strcmp(reg_name, "r14")) ctx->r14 = value;
            else if (!strcmp(reg_name, "r15")) ctx->r15 = value;
            else if (!strcmp(reg_name, "rip")) ctx->rip = value;
            else if (!strcmp(reg_name, "eflags")) ctx->rflags = value;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

void gdb_restore_context(const DebuggerContext* ctx) {
    if (!ctx) {
        return;
    }
    gdb_set_register("rax", ctx->rax);  gdb_set_register("rbx", ctx->rbx);
    gdb_set_register("rcx", ctx->rcx);  gdb_set_register("rdx", ctx->rdx);
    gdb_set_register("rsi", ctx->rsi);  gdb_set_register("rdi", ctx->rdi);
    gdb_set_register("rbp", ctx->rbp);  gdb_set_register("rsp", ctx->rsp);
    gdb_set_register("r8",  ctx->r8);   gdb_set_register("r9",  ctx->r9);
    gdb_set_register("r10", ctx->r10);  gdb_set_register("r11", ctx->r11);
    gdb_set_register("r12", ctx->r12);  gdb_set_register("r13", ctx->r13);
    gdb_set_register("r14", ctx->r14);  gdb_set_register("r15", ctx->r15);
    gdb_set_register("rip", ctx->rip);  gdb_set_register("eflags", ctx->rflags);
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

int gdb_read_memory(uint64_t address, uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "x/%zubx 0x%llx", size, (unsigned long long)address);

    std::vector<char> response(65536);
    if (send_gdb_command(cmd, response.data(), response.size(), g_config.gdbTimeoutMs) < 0) {
        return -1;
    }
    if (response[0] == '\0' || strstr(response.data(), "Cannot access")) {
        return -1;
    }

    // x/Nbx prints 8 bytes per line, each line prefixed with its own address:
    //   0x7ffd1000: 0x48  0x89  0xe5 ...
    // Parsing from the first ':' only meant every address prefix after the
    // first line was parsed as if it were data, so any read over 8 bytes came
    // back as garbage. Parse per line instead.
    size_t got = 0;
    char*  saveptr = NULL;
    char*  line = strtok_r(response.data(), "\n", &saveptr);

    while (line && got < size) {
        const char* p = strchr(line, ':');
        if (!p) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        p++;

        while (got < size) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0') {
                break;
            }

            char* end = NULL;
            const unsigned long value = strtoul(p, &end, 16);
            if (end == p) {
                break;
            }
            buffer[got++] = (uint8_t)value;
            p = end;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    return (got == size) ? (int)size : -1;
}

int gdb_write_memory(uint64_t address, const uint8_t* data, size_t size) {
    if (!data) {
        return -1;
    }

    // One gdb round-trip per byte was murder over a slow stub. Batch the
    // assignments into a few commands instead.
    const size_t PER_COMMAND = 16;
    for (size_t i = 0; i < size; i += PER_COMMAND) {
        const size_t n = ((size - i) < PER_COMMAND) ? (size - i) : PER_COMMAND;

        std::string cmd;
        for (size_t j = 0; j < n; j++) {
            char part[96];
            snprintf(part, sizeof(part), "set *(unsigned char*)0x%llx = 0x%02x",
                     (unsigned long long)(address + i + j), data[i + j]);
            if (j) {
                cmd += "\n";
            }
            cmd += part;
        }

        // gdb accepts newline-separated commands on stdin; send them as one
        // write and then wait for the final prompt.
        cmd += "\n";
        if (write_all(gdb_stdin, cmd.c_str(), cmd.size()) < 0) {
            return -1;
        }

        std::vector<char> resp(4096);
        for (size_t j = 0; j < n; j++) {
            if (read_until_prompt(gdb_stdout, resp.data(), resp.size(),
                                  g_config.gdbTimeoutMs) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

uint32_t gdb_read_memory_uint32(uint64_t address, int* success) {
    uint32_t value = 0;
    if (gdb_read_memory(address, (uint8_t*)&value, sizeof(value)) == (int)sizeof(value)) {
        if (success) *success = 1;
        return value;
    }
    if (success) *success = 0;
    return 0;
}

uint64_t gdb_read_memory_uint64(uint64_t address, int* success) {
    uint64_t value = 0;
    if (gdb_read_memory(address, (uint8_t*)&value, sizeof(value)) == (int)sizeof(value)) {
        if (success) *success = 1;
        return value;
    }
    if (success) *success = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// Convenience one-shots
// ---------------------------------------------------------------------------

void breakpoint_read_register(uint64_t address, const char* reg_name) {
    gdb_get_register_value(address, reg_name);
}

void gdb_trace_function(uint64_t address) {
    if (gdb_set_breakpoint_hardware(address) < 0) {
        printf("[!] Failed to set breakpoint\n");
        return;
    }

    printf("[*] Breakpoint set, waiting for it to be hit...\n");
    gdb_continue();

    if (!gdb_wait_for_stop(300000)) {
        printf("[!] Breakpoint not hit\n");
        gdb_remove_breakpoint(address);
        return;
    }

    printf("[+] Breakpoint hit\n\n");
    printf("========================================\n");
    printf("INSTRUCTION TRACE\n");
    printf("========================================\n\n");

    std::vector<char> response(16384);
    for (int step = 1; step <= 200; step++) {
        if (send_gdb_command("x/i $rip", response.data(), response.size(),
                             g_config.gdbTimeoutMs) == 0) {
            char* line = strstr(response.data(), "=>");
            if (!line) {
                line = strstr(response.data(), "0x");
            }
            if (line) {
                char* nl = strchr(line, '\n');
                if (nl) {
                    *nl = '\0';
                }
                printf("[%03d] %s\n", step, line);
                if (strstr(line, "ret")) {
                    printf("\n[!] RETURN - stopping\n");
                    break;
                }
            }
        }
        gdb_single_step();
    }

    printf("\n========================================\n");
    printf("TRACE COMPLETE\n");
    printf("========================================\n");

    gdb_remove_breakpoint(address);
}

// Self-contained one-shot: owns the entire gdb session from init to cleanup.
// Do not call this from inside a function that already has its own
// gdb_init()/gdb_cleanup() pair open -- gdb_pid/gdb_stdin/gdb_stdout are
// global, so only one session can be alive at a time.
void gdb_get_register_value(uint64_t breakpoint_addr, const char* reg_name) {
    if (!gdb_init()) {
        printf("[!] Failed to initialize gdb\n");
        return;
    }

    if (gdb_set_breakpoint_hardware(breakpoint_addr) < 0) {
        printf("[!] Failed to set breakpoint at 0x%llx\n",
               (unsigned long long)breakpoint_addr);
        gdb_cleanup();
        return;
    }

    gdb_continue();

    if (!gdb_wait_for_stop(3000)) {
        printf("[!] Breakpoint never hit\n");
        gdb_remove_breakpoint(breakpoint_addr);
        gdb_cleanup();
        return;
    }

    gdb_read_register_verbose(reg_name);

    gdb_remove_breakpoint(breakpoint_addr);
    gdb_cleanup();
}
