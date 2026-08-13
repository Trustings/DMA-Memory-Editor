#pragma once
#include <pthread.h>
#include "memory.hpp"
#include <stdbool.h>
#include "menu.hpp"
#include "debugger.hpp"

#include <stdatomic.h>


#ifdef __cplusplus
extern "C" {
#endif

extern pthread_mutex_t mutex0;
extern bool has_lock;

struct state0{

    //process tab state

    std::atomic<bool> firstTimeInTab;
    std::atomic<bool> firstTimeInTab_Completed;

    std::atomic<bool> ButtonRefreshProcessClicked;

    std::atomic<bool> AttachProcessButtonClicked;

    std::atomic<bool> State0End;
};

struct state1 {

   // mem_search_render tab state

    std::atomic<bool> State1End;

    std::atomic<int> currentPage;

    std::atomic<bool> FirstMemorySearch;

    std::atomic<bool> g_isFirstScan;

    std::atomic<bool> FindAccessesesClicked;

    std::atomic<uint64_t> strtol_result;

    std::atomic<bool> wp_loop_completed;

};

extern struct state0 state0_s;
extern struct state1 state1_s;

void* sync_operations(void* arg);

int start_mutex_lock();
int start_mutex_try_lock();
int end_mutex_lock();

#ifdef __cplusplus
}
#endif
