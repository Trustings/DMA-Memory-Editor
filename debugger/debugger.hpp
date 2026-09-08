// debugger.hpp -- drives a gdb process attached to the VM's gdbstub.
//
// NOTE ON SCOPE: watchpoints are set on guest *virtual* addresses through the
// QEMU gdbstub, which knows nothing about guest processes. A watchpoint on
// 0x7ff... will therefore also fire in any other process that happens to have
// that virtual address mapped. Results should be treated as "something in the
// guest touched this VA", not "the attached process touched it".
#ifndef DEBUGGER_HPP
#define DEBUGGER_HPP

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <atomic>
#include <thread>
#include <chrono>

extern pid_t gdb_pid;
extern int   gdb_stdin;
extern int   gdb_stdout;
extern int   breakpoint_count;
extern int   watchpoint_count;

#define MAX_WATCHPOINTS 20

extern uint64_t wp_buffer[MAX_WATCHPOINTS];

struct gdb_state {
    std::atomic<bool> gdb_start_init;
    std::atomic<bool> wp_started;
};

extern struct gdb_state gdb_state_c;

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
} DebuggerContext;

// Watchpoint kinds. "Access" is what "find what accesses this address"
// actually needs -- plain `watch` in gdb only traps writes.
enum GdbWatchKind {
    GDB_WATCH_WRITE  = 0,   // gdb: watch
    GDB_WATCH_READ   = 1,   // gdb: rwatch
    GDB_WATCH_ACCESS = 2,   // gdb: awatch
};

int  gdb_init(void);
void gdb_cleanup(void);
void gdb_set_verbose(bool verbose);

int  gdb_set_breakpoint_hardware(uint64_t address);
int  gdb_remove_breakpoint(uint64_t address);
void gdb_clear_all_breakpoints(void);

// size is the watched width in bytes (1, 2, 4 or 8).
int  gdb_set_watchpoint(uint64_t address, int size, GdbWatchKind kind);
int  gdb_set_watchpoint_hardware(uint64_t address);   // write watchpoint, 4 bytes
void gdb_clear_all_watchpoints(void);

int  gdb_continue(void);
int  gdb_single_step(void);
int  gdb_wait_for_stop(int timeout_ms);
void gdb_send_interrupt(void);

uint64_t gdb_read_register(const char* reg_name);
uint64_t gdb_read_register_verbose(const char* reg_name);
void     gdb_set_register(const char* reg_name, uint64_t value);
void     gdb_set_rip(uint64_t rip);

int      gdb_read_memory(uint64_t address, uint8_t* buffer, size_t size);
int      gdb_write_memory(uint64_t address, const uint8_t* data, size_t size);
uint32_t gdb_read_memory_uint32(uint64_t address, int* success);
uint64_t gdb_read_memory_uint64(uint64_t address, int* success);

void gdb_save_context(DebuggerContext* ctx);
void gdb_restore_context(const DebuggerContext* ctx);

void gdb_trace_function(uint64_t address);
void breakpoint_read_register(uint64_t address, const char* reg_name);
void gdb_get_register_value(uint64_t breakpoint_addr, const char* reg_name);

#endif // DEBUGGER_HPP
