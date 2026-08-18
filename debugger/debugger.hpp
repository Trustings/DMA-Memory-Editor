// debugger.cpp - Optimized for speed, silent mode for repetitive operations
// debugger.hpp
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
#include <thread>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

extern pid_t gdb_pid;
extern int gdb_stdin;
extern int gdb_stdout;
extern int breakpoint_count;
extern int watchpoint_count;

#define MAX_WATCHPOINTS 20

// Declare wp_buffer as a vector of atomic elements
extern uint64_t wp_buffer[MAX_WATCHPOINTS];

struct gdb_state{
    // gdb_state is not fully atomic and not safe to share among multiple threads.

    bool gdb_start_init;
    bool wp_started;
    std::atomic <bool> InContinue;
    std::atomic <bool> WaitForStop;
    std::atomic <bool> ReadRegister;

};

extern struct gdb_state gdb_state_c;

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    uint32_t r8d, r9d, r10d, r11d, r12d, r13d, r14d, r15d;
    uint32_t eflags;
    uint16_t ax, bx, cx, dx, si, di, bp, sp;
    uint16_t r8w, r9w, r10w, r11w, r12w, r13w, r14w, r15w;
    uint16_t flags;
    uint16_t es, cs, ss, ds, fs, gs;
    uint8_t al, cl, dl, bl, ah, ch, dh, bh;
    uint8_t spl, bpl, sil, dil;
    uint8_t r8b, r9b, r10b, r11b, r12b, r13b, r14b, r15b;
} DebuggerContext;

int gdb_init(void);
void gdb_cleanup(void);
void gdb_set_verbose(bool verbose);
int gdb_set_breakpoint_hardware(uint64_t address);
int gdb_remove_breakpoint(uint64_t address);
void gdb_clear_all_breakpoints(void);
int gdb_set_watchpoint_hardware(uint64_t address);
int gdb_remove_watchpoint(uint64_t address);
void gdb_clear_all_watchpoints(void);
int gdb_continue(void);
int gdb_single_step(void);
int gdb_wait_for_stop(int timeout_ms);
uint64_t gdb_read_register(const char* reg_name);
uint64_t gdb_read_in_op_register(const char* reg_name);
void breakpoint_read_register(uint64_t address, const char* reg_name);
void gdb_trace_function(uint64_t address);
void gdb_set_register(const char* reg_name, uint64_t value);
void gdb_set_rip(uint64_t rip);
int gdb_read_memory(uint64_t address, uint8_t* buffer, size_t size);
int gdb_read_memory_batch(uint64_t* addresses, uint64_t* values, int count);
void gdb_save_context_fast(DebuggerContext* ctx);
int gdb_write_memory(uint64_t address, const uint8_t* data, size_t size);
uint32_t gdb_read_memory_uint32(uint64_t address, int* success);
uint64_t gdb_read_memory_uint64(uint64_t address, int* success);
void gdb_save_context(DebuggerContext* ctx);
void gdb_restore_context(const DebuggerContext* ctx);
void gdb_send_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
