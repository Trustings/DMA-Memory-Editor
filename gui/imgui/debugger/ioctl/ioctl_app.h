#ifndef __IOCTL_APP_DEFINE_H__
#define __IOCTL_APP_DEFINE_H__

#include <sys/types.h>
#include <linux/ioctl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOCTL_BASE 'W'

// Breakpoint types
#define HW_BREAKPOINT_R 0x1
#define HW_BREAKPOINT_W 0x2
#define HW_BREAKPOINT_RW 0x3
#define HW_BREAKPOINT_X 0x4

// Single-stepping modes
#define SINGLE_STEP_DISABLED 0
#define SINGLE_STEP_GLOBAL   1
#define SINGLE_STEP_SELECTIVE 2

struct hw_breakpoint_request {
    uint64_t address;
    int bp_type;
    int bp_len;
    int bp_id;
};

struct breakpoint_info {
    int active_bps;
    int single_step_mode;
    pid_t single_step_pid;
    struct hw_breakpoint_request bp_requests[4];
};

struct single_step_control {
    int mode;
    pid_t pid;
};

int open_driver(const char* driver_name);

void close_driver(const char* driver_name, int fd_driver);

void set_breakpoint(int fd, void *address, int bp_type, int bp_len, int bp_id);

void clear_breakpoint(int fd, int bp_id);

void clear_all_breakpoints(int fd);

void enable_global_single_step(int fd);

void enable_selective_single_step(int fd, pid_t pid);

void disable_single_step(int fd);

void continue_execution(int fd);

void get_breakpoint_info(int fd);

void signal_handler(int sig);

void example_function(void);

void demo_global_breakpoint(int fd);

void demo_global_single_stepping(int fd);

// IOCTL commands
#define IOCTL_SET_HW_BREAKPOINT _IOW(IOCTL_BASE, 1, struct hw_breakpoint_request)
#define IOCTL_CLEAR_HW_BREAKPOINT _IOW(IOCTL_BASE, 2, int)
#define IOCTL_CLEAR_ALL_HW_BREAKPOINTS _IO(IOCTL_BASE, 3)
#define IOCTL_GET_BREAKPOINT_INFO _IOR(IOCTL_BASE, 4, struct breakpoint_info)
#define IOCTL_ENABLE_SINGLE_STEP _IOW(IOCTL_BASE, 5, struct single_step_control)
#define IOCTL_DISABLE_SINGLE_STEP _IO(IOCTL_BASE, 6)
#define IOCTL_CONTINUE_EXECUTION _IO(IOCTL_BASE, 7)

#ifdef __cplusplus
}
#endif

#endif // __IOCTL_APP_DEFINE_H__
