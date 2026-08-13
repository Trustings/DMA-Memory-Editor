#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <string.h>
#include <signal.h>

#include "ioctl_app.h"

#define IOCTL_DRIVER_NAME "/dev/ioctl"
#define MAX_HW_BREAKPOINTS 4

static int debugger_fd = -1;

int open_driver(const char* driver_name)
{
    printf("Opening Driver\n");

    int fd_driver = open(driver_name, O_RDWR);
    if (fd_driver == -1)
    {
        printf("Error opening \"%s\".\n", driver_name);
        printf("    errno = %s\n", strerror(errno));
        printf("Make sure the module is loaded and device node exists:\n");
        printf("    sudo mknod /dev/ioctl c 240 0\n");
        printf("    sudo chmod 666 /dev/ioctl\n");
        exit(EXIT_FAILURE);
    }

    return fd_driver;
}

void close_driver(const char* driver_name, int fd_driver)
{
    printf("Closing Driver\n");

    int result = close(fd_driver);
    if (result == -1)
    {
        printf("Error closing \"%s\".\n", driver_name);
        printf("    errno = %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void set_breakpoint(int fd, void *address, int bp_type, int bp_len, int bp_id)
{
    struct hw_breakpoint_request bp_req;

       // Convert pointer to uint64_t for safe transfer
       bp_req.address = (uint64_t)(unsigned long)address;
       bp_req.bp_type = bp_type;
       bp_req.bp_len = bp_len;
       bp_req.bp_id = bp_id;

       printf("Setting hardware breakpoint at address %p (0x%llx)\n",
              address, bp_req.address);

       if (ioctl(fd, IOCTL_SET_HW_BREAKPOINT, &bp_req) < 0) {
           perror("Error setting hardware breakpoint");
           return -1;
       }

       printf("Hardware breakpoint set successfully\n");
}

void clear_breakpoint(int fd, int bp_id)
{
    printf("Clearing hardware breakpoint %d\n", bp_id);
    
    if (ioctl(fd, IOCTL_CLEAR_HW_BREAKPOINT, &bp_id) < 0) {
        perror("Error clearing hardware breakpoint");
        exit(EXIT_FAILURE);
    }
    
    printf("Hardware breakpoint cleared successfully\n");
}

void clear_all_breakpoints(int fd)
{
    printf("Clearing all hardware breakpoints\n");
    
    if (ioctl(fd, IOCTL_CLEAR_ALL_HW_BREAKPOINTS, 0) < 0) {
        perror("Error clearing all hardware breakpoints");
        exit(EXIT_FAILURE);
    }
    
    printf("All hardware breakpoints cleared successfully\n");
}

void enable_global_single_step(int fd)
{
    struct single_step_control step_ctrl;
    
    step_ctrl.mode = SINGLE_STEP_GLOBAL;
    step_ctrl.pid = 0;
    
    printf("Enabling GLOBAL single-stepping - ANY process that hits breakpoint will single-step\n");
    
    if (ioctl(fd, IOCTL_ENABLE_SINGLE_STEP, &step_ctrl) < 0) {
        perror("Error enabling global single-step");
        exit(EXIT_FAILURE);
    }
    
    printf("Global single-stepping enabled successfully\n");
}

void enable_selective_single_step(int fd, pid_t pid)
{
    struct single_step_control step_ctrl;
    
    step_ctrl.mode = SINGLE_STEP_SELECTIVE;
    step_ctrl.pid = pid;
    
    printf("Enabling selective single-stepping for PID %d\n", pid);
    
    if (ioctl(fd, IOCTL_ENABLE_SINGLE_STEP, &step_ctrl) < 0) {
        perror("Error enabling selective single-step");
        exit(EXIT_FAILURE);
    }
    
    printf("Selective single-stepping enabled successfully\n");
}

void disable_single_step(int fd)
{
    printf("Disabling single-stepping\n");
    
    if (ioctl(fd, IOCTL_DISABLE_SINGLE_STEP, 0) < 0) {
        perror("Error disabling single-step");
        exit(EXIT_FAILURE);
    }
    
    printf("Single-stepping disabled successfully\n");
}

void continue_execution(int fd)
{
    printf("Continuing execution after single-step\n");
    
    if (ioctl(fd, IOCTL_CONTINUE_EXECUTION, 0) < 0) {
        perror("Error continuing execution");
        exit(EXIT_FAILURE);
    }
    
    printf("Execution continued\n");
}

void get_breakpoint_info(int fd)
{
    struct breakpoint_info bp_info;
    
    if (ioctl(fd, IOCTL_GET_BREAKPOINT_INFO, &bp_info) < 0) {
        perror("Error getting breakpoint info");
        exit(EXIT_FAILURE);
    }
    
    printf("=== Debugger Status ===\n");
    printf("Active breakpoints: %d\n", bp_info.active_bps);
    
    const char* step_mode_str = "Unknown";
    if (bp_info.single_step_mode == SINGLE_STEP_DISABLED)
        step_mode_str = "Disabled";
    else if (bp_info.single_step_mode == SINGLE_STEP_GLOBAL)
        step_mode_str = "Global";
    else if (bp_info.single_step_mode == SINGLE_STEP_SELECTIVE)
        step_mode_str = "Selective";
    
    printf("Single-step mode: %s", step_mode_str);
    if (bp_info.single_step_mode == SINGLE_STEP_SELECTIVE)
        printf(" (PID: %d)", bp_info.single_step_pid);
    printf("\n");
    
    for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (bp_info.bp_requests[i].bp_id >= 0) {
            printf("  Breakpoint %d: active\n", bp_info.bp_requests[i].bp_id);
        }
    }
}

// Signal handler for clean shutdown
void signal_handler(int sig)
{
    printf("\nReceived signal %d - cleaning up...\n", sig);
    if (debugger_fd != -1) {
        clear_all_breakpoints(debugger_fd);
        disable_single_step(debugger_fd);
        close_driver(IOCTL_DRIVER_NAME, debugger_fd);
    }
    exit(EXIT_SUCCESS);
}

// Example function to set breakpoint on
void example_function(void) {
    printf("Example function executed\n");
    // Some operations to step through
    int a = 1;
    int b = 2;
    int c = a + b;
    printf("Result: %d\n", c);
}

void demo_global_breakpoint(int fd)
{
    printf("\n=== Global Breakpoint Demo ===\n");
    
    // Set a breakpoint that will trigger for ANY process
    set_breakpoint(fd, (void*)example_function, HW_BREAKPOINT_X, 1, 0);
    
    printf("Global breakpoint set at %p\n", (void*)example_function);
    printf("This breakpoint will trigger for ANY process that calls this function\n");
    
    get_breakpoint_info(fd);
    
    printf("Calling example_function (will trigger breakpoint)...\n");
    example_function();
    
    clear_breakpoint(fd, 0);
}

void demo_global_single_stepping(int fd)
{
    printf("\n=== Global Single-Stepping Demo ===\n");
    
    // Enable global single-stepping
    enable_global_single_step(fd);
    
    // Set a breakpoint
    set_breakpoint(fd, (void*)example_function, HW_BREAKPOINT_X, 1, 0);
    
    printf("Global single-stepping enabled with breakpoint at %p\n", (void*)example_function);
    printf("ANY process that hits this breakpoint will enter single-step mode\n");
    
    get_breakpoint_info(fd);
    
    printf("Calling example_function...\n");
    example_function();
    
    printf("After breakpoint, check dmesg for single-step messages\n");
    printf("Use CONTINUE to resume execution after each single-step\n");
    
    // Clean up
    clear_breakpoint(fd, 0);
    disable_single_step(fd);
}
