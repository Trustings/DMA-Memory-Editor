#include "debugger.hpp"
#include <iostream>  // C++ headers outside extern "C"

void target_function(void) {
    std::cout << ">>> Target function executed!" << std::endl;
    int x = 1;
    int y = 2;
    int z = x + y;
    std::cout << ">>> Result: " << z << std::endl;
}

int test() {
    int fd = open_driver("/dev/ioctl");

    // Use the first kernel address from your system
        void *kernel_addr = (void*)0xffffffff81000000; // startup_64

        std::cout << "Testing kernel address: " << kernel_addr << std::endl;

        set_breakpoint(fd, kernel_addr, HW_BREAKPOINT_X, 1, 0);
            std::cout << "SUCCESS! Kernel debugging works!\n";
            std::cout << "This breakpoint will trigger system-wide.\n";

            // Keep it running to see breakpoints
            while (true) {
                sleep(1);
            }

    close_driver("/dev/ioctl", fd);

    return 0;
}
