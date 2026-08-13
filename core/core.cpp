#include "core.hpp"

pthread_mutex_t mutex0;
bool has_lock = false;

struct state0 state0_s = { .firstTimeInTab = false,
                          .firstTimeInTab_Completed = false,
                          .AttachProcessButtonClicked = false,
                          .State0End = false};

struct state1 state1_s = {.State1End = false,
                          .currentPage = 0,
                          .FirstMemorySearch = false,
                          .g_isFirstScan = false,
                          .FindAccessesesClicked = false,
                          .strtol_result = 0,
                          .wp_loop_completed = false};

#ifdef __linux__
static int wp_helper(uint64_t addr){
    for (int i = 0; i < watchpoint_count; i++){
        if (wp_buffer[i] == addr){
            watchpoint_count++;
            if (watchpoint_count < MAX_WATCHPOINTS) {
                return 0;
            }
        }
    }

    if (watchpoint_count < MAX_WATCHPOINTS) {
        wp_buffer[watchpoint_count++] = addr;
        return 1;
    }

    return -1;

}
#endif

void* sync_operations(void* arg){

    printf("sync_operations function started. \n");

    static bool thread_alive = true;

    while(thread_alive){

        while (imGuiMenu::tabCount == 1){

            if (state0_s.firstTimeInTab) {

                list_all_processes(hVMM);
                state0_s.firstTimeInTab = false;
                state0_s.firstTimeInTab_Completed = true;
            }

            if(state0_s.ButtonRefreshProcessClicked){

                process_name.clear();
                DLL_Name.clear();
                process_id = 0;
                process_handle = 0;
                process_base_address = 0;
                DLL_base_address = 0;
                process_size = 0;
                DLL_size = 0;
                list_all_processes(hVMM);

                state0_s.ButtonRefreshProcessClicked = false;
            }

            if(state0_s.AttachProcessButtonClicked){
                printf("[>] Attaching to PID %lu - %s\n",
                       processes[selectedIndex].dwPID,
                       processes[selectedIndex].szNameLong);

                process_name = processes[selectedIndex].szNameLong;
                process_id = processes[selectedIndex].dwPID;
                get_process_base_address(process_name, process_id);

                state0_s.AttachProcessButtonClicked = false;
            }
        }


        while(imGuiMenu::tabCount == 2){

            if(state1_s.FirstMemorySearch){

                MemorySearch_FirstScan(process_id);

                // Reset to first page when new scan is done
                state1_s.currentPage = 0;

                state1_s.FirstMemorySearch = false;

            }

            #ifdef __linux
            if(state1_s.FindAccessesesClicked && !state1_s.wp_loop_completed){

                if(gdb_state_c.gdb_start_init){
                    if(gdb_init() == 1){
                        gdb_state_c.gdb_start_init = false;
                    } else {
                        printf("gdb failed to initialize!");
                        abort();
                    }

                    if(!gdb_state_c.wp_started){
                        if(gdb_set_watchpoint_hardware(state1_s.strtol_result) == 0){
                            gdb_state_c.wp_started = true;

                            gdb_continue();
                            gdb_wait_for_stop(30000);

                            uint64_t wp_addr = gdb_read_register("rip");

                            printf("RIP at wp: 0x%llx\n", wp_addr);

                            int result = wp_helper(wp_addr);

                            if (result == 1){
                                printf("New wp addr at 0x%llx\n", wp_addr);
                            } else if (result == 0) {
                                printf("Duplicate wp addr 0x%llx...skipping\n", wp_addr);
                            } else {
                                printf("Buffer full, MAX_WATCHPOINTS hit\n");
                            }

                        }

                     }



                 }

                if(!state1_s.wp_loop_completed){
                gdb_continue();
                gdb_wait_for_stop(30000);

                uint64_t wp_addr = gdb_read_register("rip");

                printf("RIP at wp: 0x%llx\n", wp_addr);

                int result = wp_helper(wp_addr);

                if (result == 1){
                    printf("New wp addr at 0x%llx\n", wp_addr);
                } else if (result == 0) {
                    printf("Duplicate wp addr 0x%llx...skipping\n", wp_addr);
                } else {
                    printf("Buffer full, MAX_WATCHPOINTS hit\n");
                    gdb_clear_all_watchpoints();
                    gdb_cleanup();

                    for (int i = 0; i <= MAX_WATCHPOINTS; i++) {
                        wp_buffer[i].store(0, std::memory_order_relaxed);
                    }


                    MAX_WATCHPOINTS = 20;
                    watchpoint_count = 0;


                    state1_s.wp_loop_completed = true;
                    state1_s.FindAccessesesClicked = false;
                }

                }

            }
            #endif

        }
    }

    return arg;
}

int start_mutex_lock(){

    while (&mutex0 == 0){

    }

    int result = pthread_mutex_lock(&mutex0);
    has_lock = true;

    return result;
}

int start_mutex_try_lock(){

    while (&mutex0 == 0){

    }

    int result = pthread_mutex_trylock(&mutex0);

    if (result == 0){
        has_lock = true;
    }

    return result;
}


int end_mutex_lock(){

    int result = pthread_mutex_unlock(&mutex0);
    has_lock = false;

    return result;
}
