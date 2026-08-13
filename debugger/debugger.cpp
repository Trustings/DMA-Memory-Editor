#include "debugger.hpp"
#include <thread>
#include <chrono>

pid_t gdb_pid = -1;
int gdb_stdin = -1;
int gdb_stdout = -1;
int breakpoint_count = 0;
std::atomic<int> watchpoint_count = 0;

std::atomic<int> MAX_WATCHPOINTS{20};

// Declare wp_buffer as a vector of atomic elements
std::vector<std::atomic<uint64_t>> wp_buffer(MAX_WATCHPOINTS.load());

static bool g_verbose = true;  // Toggle verbose output

struct gdb_state gdb_state_c = {
    .gdb_start_init = true,
    .wp_started = false,
};

void gdb_set_verbose(bool verbose) {
    g_verbose = verbose;
}

static void safe_close(int* fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) return -1;
        written += n;
    }
    return 0;
}

// Fast silent read - no debug output
static int read_until_prompt_silent(int fd, char* buffer, size_t buffer_size, int timeout_ms) {
    fd_set fds;
    struct timeval tv;
    size_t pos = 0;
    auto start = std::chrono::steady_clock::now();
    bool found_prompt = false;

    memset(buffer, 0, buffer_size);

    while (!found_prompt && pos < buffer_size - 1) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) break;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms poll 50000

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) return -1;
        if (ret == 0) continue;

        char c;
        if (read(fd, &c, 1) != 1) return -1;

        if (pos < buffer_size - 1) buffer[pos++] = c;

        if (pos >= 5 && strstr(buffer + pos - 5, "(gdb)") != nullptr) {
            found_prompt = true;
            char* prompt_pos = strstr(buffer, "(gdb)");
            if (prompt_pos) { *prompt_pos = '\0'; pos = prompt_pos - buffer; }
        }
    }

    // IMPORTANT: a timeout with no prompt found must be unambiguous from a
    // successful-but-empty response. Callers (gdb_wait_for_stop especially)
    // rely on this to know whether GDB actually stopped or we just ran out
    // of time waiting.
    if (!found_prompt) return -1;

    buffer[pos] = '\0';
    while (pos > 0 && (buffer[pos-1] == '\n' || buffer[pos-1] == '\r' || buffer[pos-1] == ' '))
        buffer[--pos] = '\0';
    return (int)pos;
}

// Verbose read with debug output
static int read_until_prompt_verbose(int fd, char* buffer, size_t buffer_size, int timeout_ms) {
    int len = read_until_prompt_silent(fd, buffer, buffer_size, timeout_ms);
    if (len >= 0 && buffer[0] != '\0' && g_verbose) {
        char* line = buffer;
        char* newline;
        while ((newline = strchr(line, '\n')) != nullptr) {
            *newline = '\0';
            if (strlen(line) > 0 && strcmp(line, "(gdb)") != 0)
                printf("[GDB <-] %s\n", line);
            line = newline + 1;
        }
        if (strlen(line) > 0 && strcmp(line, "(gdb)") != 0)
            printf("[GDB <-] %s\n", line);
    }
    return len;
}

// Fast command - no debug output
static int send_gdb_command_fast(const char* cmd, char* response, size_t resp_size, int timeout_ms) {
    if (gdb_stdin < 0 || gdb_stdout < 0) return -1;

    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "%s\n", cmd);

    if (write_all(gdb_stdin, full_cmd, strlen(full_cmd)) < 0) return -1;

    char output[65536];
    int len = read_until_prompt_silent(gdb_stdout, output, sizeof(output), timeout_ms);

    if (len >= 0 && response) {
        strncpy(response, output, resp_size - 1);

      //  printf("%s\n", response);

        response[resp_size - 1] = '\0';
        return 0;
    }
    return -1;
}

// Verbose command
static int send_gdb_command_verbose(const char* cmd, char* response, size_t resp_size, int timeout_ms) {
    if (gdb_stdin < 0 || gdb_stdout < 0) return -1;
    if (g_verbose) printf("[GDB ->] %s\n", cmd);
    return send_gdb_command_fast(cmd, response, resp_size, timeout_ms);
}

// Public wrapper that respects verbose mode for important commands
static int send_gdb_command(const char* cmd, char* response, size_t resp_size, int timeout_ms) {
    return send_gdb_command_verbose(cmd, response, resp_size, timeout_ms);
}

static bool g_cleanup_done = false;

static void gdb_signal_handler(int sig) {
    gdb_cleanup();
    // Re-raise with default disposition so the process still dies the way
    // it normally would (correct exit status, core dump if applicable).
    signal(sig, SIG_DFL);
    raise(sig);
}

int gdb_init(void) {
    printf("[DEBUG] Starting GDB process...\n");
    g_cleanup_done = false;

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) { perror("pipe"); return 0; }

    gdb_pid = fork();
    if (gdb_pid == -1) { perror("fork"); return 0; }

    if (gdb_pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        // Just connect, don't continue or interrupt
        execlp("gdb", "gdb", "-q", "-nx", "-ex", "target remote localhost:1234", NULL);
        perror("execlp"); exit(1);
    }

    gdb_stdin = stdin_pipe[1];
    gdb_stdout = stdout_pipe[0];
    close(stdin_pipe[0]); close(stdout_pipe[1]);

    printf("[DEBUG] GDB started with PID: %d\n", gdb_pid);

    /*
    char response[4096];
    for (int i = 0; i < 10; i++) {
        if (read_until_prompt_silent(gdb_stdout, response, sizeof(response), 50) >= 0) {
            printf("[+] GDB ready\n");
            break;
        }
    }
    */

    char response[4096];
    int len = read_until_prompt_silent(gdb_stdout, response, sizeof(response), 2000);
    if (len < 0 || strstr(response, "Remote debugging using") == NULL) {
        printf("[!] Failed to attach to remote target:\n%s\n", response);
        gdb_cleanup();
        return 0;
    }
    printf("[+] GDB ready\n");

    send_gdb_command_fast("set pagination off", NULL, 0, 50);
    send_gdb_command_fast("set confirm off", NULL, 0, 50);

    // Make sure we always clean up after ourselves -- even on Ctrl-C or an
    // unexpected exit -- so we never leave hardware breakpoints planted on
    // the target or a zombie gdb process behind.
    atexit(gdb_cleanup);
    signal(SIGINT, gdb_signal_handler);
    signal(SIGTERM, gdb_signal_handler);

    return 1;
}

void gdb_cleanup(void) {
    if (g_cleanup_done) return;
    g_cleanup_done = true;

    if (gdb_pid > 0 && gdb_stdin >= 0) {
        // In case we're mid-`continue`, stop the target first so GDB can
        // actually accept commands again.
        gdb_send_interrupt();
        char resp[4096];
        read_until_prompt_silent(gdb_stdout, resp, sizeof(resp), 300);

        // Remove everything we planted on the target.
        send_gdb_command_fast("delete", NULL, 0, 200);

        // This is a remote target (`target remote localhost:1234`), so GDB
        // doesn't own the inferior process -- detach rather than kill, so
        // the remote target is left running exactly as if we'd never
        // attached, with no breakpoints left behind.
        send_gdb_command_fast("detach", NULL, 0, 500);

        send_gdb_command_fast("quit", NULL, 0, 200);
    }

    if (gdb_pid > 0) {
        kill(gdb_pid, SIGTERM);
        waitpid(gdb_pid, NULL, 0);
        gdb_pid = -1;
    }
    safe_close(&gdb_stdin);
    safe_close(&gdb_stdout);
    breakpoint_count = 0;
    watchpoint_count = 0;
}

int gdb_set_breakpoint_hardware(uint64_t address) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hbreak *0x%llx", (unsigned long long)address);
    char response[4096];
    if (send_gdb_command(cmd, response, sizeof(response), 50) < 0) return -1;
    if (strstr(response, "Hardware assisted breakpoint") || strstr(response, "Breakpoint")) {
        breakpoint_count++;
        return 0;
    }
    return -1;
}

int gdb_remove_breakpoint(uint64_t address) {
    (void)address;
    send_gdb_command_fast("delete", NULL, 0, 50);
    breakpoint_count = 0;
    return 0;
}

void gdb_clear_all_breakpoints(void) {
    send_gdb_command_fast("delete", NULL, 0, 50);
    breakpoint_count = 0;
}

int gdb_set_watchpoint_hardware(uint64_t address) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "watch *0x%llx", (unsigned long long)address);
    char response[4096];
    if (send_gdb_command(cmd, response, sizeof(response), 50) < 0) return -1;
    if (strstr(response, "Hardware watchpoint")) {
        watchpoint_count++;
        return 0;
    }
    return -1;
}

int gdb_remove_watchpoint(uint64_t address) {
    (void)address;
    send_gdb_command_fast("delete", NULL, 0, 50);
    watchpoint_count = 0;
    return 0;
}

void gdb_clear_all_watchpoints(void) {
    send_gdb_command_fast("delete", NULL, 0, 50);
    watchpoint_count = 0;
}

int gdb_continue(void) {
    if (gdb_stdin < 0) return -1;
    const char* cmd = "continue\n";
    write_all(gdb_stdin, cmd, strlen(cmd));
    return 0;
}
int gdb_single_step(void) {
    send_gdb_command_fast("stepi", NULL, 0, 50);
    return 0;
}

int gdb_wait_for_stop(int timeout_ms) {
    if (gdb_stdout < 0) return 0;

    // Do NOT send any command here. GDB is currently blocked executing
    // `continue` and will not read new stdin commands until it regains
    // control (breakpoint/signal hit). Writing polling commands like
    // "info register rip" in a loop just queues them up unread in GDB's
    // stdin buffer; when the breakpoint finally hits, GDB drains that
    // entire backlog almost instantly and we end up reading the response
    // to some *earlier* queued command instead of the current state --
    // which is exactly what was corrupting subsequent register reads.
    //
    // GDB itself prints the stop banner + a fresh "(gdb) " prompt the
    // moment it stops, with no input required from us. Just wait for it.
    char response[8192];
    int len = read_until_prompt_silent(gdb_stdout, response, sizeof(response), timeout_ms);
    if (len < 0) return 0; // timed out, still running (or pipe error)

    if (g_verbose && response[0] != '\0') {
        printf("[GDB <-] %s\n", response);
    }
    return 1;
}

uint64_t gdb_read_register(const char* reg_name) {
    char response[32768];
    memset(response, 0, sizeof(response));
    if (send_gdb_command_fast("info registers", response, sizeof(response), 200) < 0) return 0;

    // Find the register name followed by its value
    // Format: "rcx            0x20babbd7580"
    char search[32];
    snprintf(search, sizeof(search), "%s", reg_name);

    char* pos = strstr(response, search);
    if (!pos) return 0;

    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t') pos++;

    if (pos[0] == '0' && pos[1] == 'x') {
        return strtoull(pos, NULL, 16);
    }
    return 0;
}

uint64_t gdb_read_in_op_register(const char* reg_name) {
    char response[32768];
    char command[64];
    memset(response, 0, sizeof(response));

    // Build command: "print $reg_name"
    snprintf(command, sizeof(command), "print $%s", reg_name);

    printf("%s\n", command);

    if (send_gdb_command_fast(command, response, sizeof(response), 200) < 0)
        return 0;

    // GDB print output format: "$1 = 0x20babbd7580" or "$1 = 12345"
    // Find the "= " separator
    char* pos = strstr(response, "= ");
    if (!pos) return 0;

    pos += 2; // Skip "= "

    // Skip any leading whitespace
    while (*pos == ' ' || *pos == '\t') pos++;

    // Parse the value (handles hex with 0x prefix or decimal)
    if (pos[0] == '0' && (pos[1] == 'x' || pos[1] == 'X')) {
        return strtoull(pos, NULL, 16);
    } else {
        return strtoull(pos, NULL, 10);
    }
}

void breakpoint_read_register(uint64_t address, const char* reg_name) {
    gdb_init();

    gdb_set_breakpoint_hardware(address);
    gdb_continue();
    gdb_wait_for_stop(3000);

    uint64_t value = gdb_read_register(reg_name);   // <-- saved to a variable
    printf("%s = 0x%llx\n", reg_name, (unsigned long long)value);   // <-- printed

    gdb_remove_breakpoint(address);
    gdb_cleanup();
}

void gdb_trace_function(uint64_t address) {
    char response[8192];

    // Set breakpoint using existing function
    if (gdb_set_breakpoint_hardware(address) < 0) {
        printf("[!] Failed to set breakpoint\n");
        return;
    }

    printf("[*] Breakpoint set. Booting VM...\n");

    // Continue execution
    gdb_continue();

    printf("[*] Waiting for breakpoint to be hit...\n");

    // Wait for breakpoint
    if (!gdb_wait_for_stop(300000)) {
        printf("[!] Breakpoint not hit\n");
        return;
    }

    printf("[+] BREAKPOINT HIT!\n\n");
    printf("========================================\n");
    printf("INSTRUCTION TRACE\n");
    printf("========================================\n\n");

    // Now trace using the working send_gdb_command_fast
    for (int step = 1; step <= 200; step++) {
        // Get current instruction
        if (send_gdb_command_fast("x/i $rip", response, sizeof(response), 100) == 0) {
            // Clean up response - find the instruction line
            char* line = strstr(response, "=>");
            if (!line) line = strstr(response, "0x");

            if (line) {
                // Remove newline
                char* nl = strchr(line, '\n');
                if (nl) *nl = '\0';

                printf("[%03d] %s\n", step, line);

                if (strstr(line, "ret")) {
                    printf("\n[!] RETURN - stopping\n");
                    break;
                }
            }
        }

        // Single step
        gdb_single_step();
        usleep(50000);
    }

    printf("\n========================================\n");
    printf("TRACE COMPLETE\n");
    printf("========================================\n");

    gdb_remove_breakpoint(address);
}

void gdb_set_register(const char* reg_name, uint64_t value) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "set $%s = 0x%llx", reg_name, (unsigned long long)value);
    send_gdb_command_fast(cmd, NULL, 0, 50);
}

void gdb_set_rip(uint64_t rip) {
    gdb_set_register("rip", rip);
}

int gdb_read_memory(uint64_t address, uint8_t* buffer, size_t size) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "x/%zubx 0x%llx", size, (unsigned long long)address);
    char response[8192];
    if (send_gdb_command_fast(cmd, response, sizeof(response), 50) < 0) return -1;
    if (strlen(response) == 0 || strstr(response, "Cannot access")) return -1;

    const char* colon = strchr(response, ':');
    if (!colon) return -1;

    const char* p = colon + 1;
    for (size_t i = 0; i < size && p && *p; i++) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '0' && *(p+1) == 'x') p += 2;
        char* endptr = NULL;
        buffer[i] = (uint8_t)strtoul(p, &endptr, 16);
        p = endptr;
    }
    return size;
}

int gdb_write_memory(uint64_t address, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "set *(unsigned char*)0x%llx = 0x%02x",
                 (unsigned long long)(address + i), data[i]);
        if (send_gdb_command_fast(cmd, NULL, 0, 50) < 0) return -1;
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

// Fast context save - only GP registers, silent
void gdb_save_context_fast(DebuggerContext* ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));

    char response[32768];
    if (send_gdb_command_fast("info registers", response, sizeof(response), 50) == 0) {
        char* line = strtok(response, "\n");
        while (line) {
            uint64_t value = 0;
            char reg_name[32] = {0};
            if (sscanf(line, "%s 0x%llx", reg_name, (unsigned long long*)&value) == 2) {
                if (strcmp(reg_name, "rax") == 0) ctx->rax = value;
                else if (strcmp(reg_name, "rbx") == 0) ctx->rbx = value;
                else if (strcmp(reg_name, "rcx") == 0) ctx->rcx = value;
                else if (strcmp(reg_name, "rdx") == 0) ctx->rdx = value;
                else if (strcmp(reg_name, "rsi") == 0) ctx->rsi = value;
                else if (strcmp(reg_name, "rdi") == 0) ctx->rdi = value;
                else if (strcmp(reg_name, "rbp") == 0) ctx->rbp = value;
                else if (strcmp(reg_name, "rsp") == 0) ctx->rsp = value;
                else if (strcmp(reg_name, "r8") == 0) ctx->r8 = value;
                else if (strcmp(reg_name, "r9") == 0) ctx->r9 = value;
                else if (strcmp(reg_name, "r10") == 0) ctx->r10 = value;
                else if (strcmp(reg_name, "r11") == 0) ctx->r11 = value;
                else if (strcmp(reg_name, "r12") == 0) ctx->r12 = value;
                else if (strcmp(reg_name, "r13") == 0) ctx->r13 = value;
                else if (strcmp(reg_name, "r14") == 0) ctx->r14 = value;
                else if (strcmp(reg_name, "r15") == 0) ctx->r15 = value;
                else if (strcmp(reg_name, "rip") == 0) ctx->rip = value;
                else if (strcmp(reg_name, "eflags") == 0) ctx->rflags = value;
            }
            line = strtok(NULL, "\n");
        }
    }

    // Fill sub-registers
    ctx->eax = (uint32_t)ctx->rax;  ctx->ebx = (uint32_t)ctx->rbx;
    ctx->ecx = (uint32_t)ctx->rcx;  ctx->edx = (uint32_t)ctx->rdx;
    ctx->esi = (uint32_t)ctx->rsi;  ctx->edi = (uint32_t)ctx->rdi;
    ctx->ebp = (uint32_t)ctx->rbp;  ctx->esp = (uint32_t)ctx->rsp;
    ctx->eflags = (uint32_t)ctx->rflags;
    ctx->r8d = (uint32_t)ctx->r8;   ctx->r9d = (uint32_t)ctx->r9;
    ctx->r10d = (uint32_t)ctx->r10; ctx->r11d = (uint32_t)ctx->r11;
    ctx->r12d = (uint32_t)ctx->r12; ctx->r13d = (uint32_t)ctx->r13;
    ctx->r14d = (uint32_t)ctx->r14; ctx->r15d = (uint32_t)ctx->r15;

    ctx->ax = (uint16_t)ctx->rax;   ctx->bx = (uint16_t)ctx->rbx;
    ctx->cx = (uint16_t)ctx->rcx;   ctx->dx = (uint16_t)ctx->rdx;
    ctx->si = (uint16_t)ctx->rsi;   ctx->di = (uint16_t)ctx->rdi;
    ctx->bp = (uint16_t)ctx->rbp;   ctx->sp = (uint16_t)ctx->rsp;
    ctx->r8w = (uint16_t)ctx->r8;   ctx->r9w = (uint16_t)ctx->r9;
    ctx->r10w = (uint16_t)ctx->r10; ctx->r11w = (uint16_t)ctx->r11;
    ctx->r12w = (uint16_t)ctx->r12; ctx->r13w = (uint16_t)ctx->r13;
    ctx->r14w = (uint16_t)ctx->r14; ctx->r15w = (uint16_t)ctx->r15;

    ctx->al = (uint8_t)ctx->rax;    ctx->cl = (uint8_t)ctx->rcx;
    ctx->dl = (uint8_t)ctx->rdx;    ctx->bl = (uint8_t)ctx->rbx;
    ctx->ah = (uint8_t)(ctx->rax >> 8); ctx->ch = (uint8_t)(ctx->rcx >> 8);
    ctx->dh = (uint8_t)(ctx->rdx >> 8); ctx->bh = (uint8_t)(ctx->rbx >> 8);
    ctx->spl = (uint8_t)ctx->rsp;   ctx->bpl = (uint8_t)ctx->rbp;
    ctx->sil = (uint8_t)ctx->rsi;   ctx->dil = (uint8_t)ctx->rdi;
    ctx->r8b = (uint8_t)ctx->r8;    ctx->r9b = (uint8_t)ctx->r9;
    ctx->r10b = (uint8_t)ctx->r10;  ctx->r11b = (uint8_t)ctx->r11;
    ctx->r12b = (uint8_t)ctx->r12;  ctx->r13b = (uint8_t)ctx->r13;
    ctx->r14b = (uint8_t)ctx->r14;  ctx->r15b = (uint8_t)ctx->r15;
}

void gdb_save_context(DebuggerContext* ctx) {
    gdb_save_context_fast(ctx);
}

void gdb_restore_context(const DebuggerContext* ctx) {
    if (!ctx) return;
    gdb_set_register("rax", ctx->rax);    gdb_set_register("rbx", ctx->rbx);
    gdb_set_register("rcx", ctx->rcx);    gdb_set_register("rdx", ctx->rdx);
    gdb_set_register("rsi", ctx->rsi);    gdb_set_register("rdi", ctx->rdi);
    gdb_set_register("rbp", ctx->rbp);    gdb_set_register("rsp", ctx->rsp);
    gdb_set_register("r8", ctx->r8);      gdb_set_register("r9", ctx->r9);
    gdb_set_register("r10", ctx->r10);    gdb_set_register("r11", ctx->r11);
    gdb_set_register("r12", ctx->r12);    gdb_set_register("r13", ctx->r13);
    gdb_set_register("r14", ctx->r14);    gdb_set_register("r15", ctx->r15);
    gdb_set_register("rip", ctx->rip);    gdb_set_register("eflags", ctx->rflags);
}

void gdb_send_interrupt(void) {
    // gdb_stdin is a pipe, not a real TTY, so writing the raw 0x03 byte
    // does nothing -- the terminal line discipline is what normally turns
    // Ctrl-C into SIGINT, and pipes don't do that translation. Signal the
    // process directly instead.
    if (gdb_pid > 0) kill(gdb_pid, SIGINT);
}

// Assumes GDB is already initialized and currently stopped at a breakpoint
// (i.e. called between gdb_wait_for_stop() returning 1 and the next
// gdb_continue()). Just reads one register and returns it -- no lifecycle
// management, so it's safe to call from inside an existing session/loop
// such as get_array().
uint64_t gdb_read_register_verbose(const char* reg_name) {
    uint64_t value = gdb_read_register(reg_name);
    printf("[+] %s = 0x%llx\n", reg_name, (unsigned long long)value);
    return value;
}

// Self-contained one-shot: owns the entire GDB session from init to
// cleanup. Breaks once, reads a single register, prints it, and tears
// everything down. Do not call this from inside a function that already
// has its own gdb_init()/gdb_cleanup() pair open -- gdb_pid/gdb_stdin/
// gdb_stdout are global statics, so only one session can be alive at a
// time.
void gdb_get_register_value(uint64_t breakpoint_addr, const char* reg_name) {
    if (!gdb_init()) {
        printf("[!] Failed to initialize GDB\n");
        return;
    }

    if (gdb_set_breakpoint_hardware(breakpoint_addr) < 0) {
        printf("[!] Failed to set breakpoint at 0x%llx\n", (unsigned long long)breakpoint_addr);
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
