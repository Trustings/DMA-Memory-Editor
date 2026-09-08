#include "core.hpp"
#include "config.hpp"
#include "list.hpp"
#include "search.hpp"
#include "menu.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

struct state0 state0_s;
struct state1 state1_s;

std::atomic<int> selectedProcessIndex{-1};

namespace {

std::thread              g_worker;
std::mutex               g_wakeMutex;
std::condition_variable  g_wake;
std::atomic<bool>        g_running{false};

// Watchpoint hits, guarded because the UI renders them while the worker
// appends to them.
std::mutex               g_wpMutex;
std::vector<uint64_t>    g_wpHits;

#ifdef __linux__

// Record a watchpoint hit address. Returns:
//   1  newly recorded
//   0  already present
//  -1  buffer full
//
// The old version incremented watchpoint_count when it found a *duplicate*,
// which walked the counter forward over unwritten slots and tripped the
// MAX_WATCHPOINTS limit early.
int RecordWatchpointHit(uint64_t addr) {
    std::lock_guard<std::mutex> lock(g_wpMutex);

    for (size_t i = 0; i < g_wpHits.size(); i++) {
        if (g_wpHits[i] == addr) {
            return 0;
        }
    }

    if (g_wpHits.size() >= MAX_WATCHPOINTS) {
        return -1;
    }

    g_wpHits.push_back(addr);
    return 1;
}

// One iteration of the "find what accesses this address" loop: let the guest
// run, wait for the watchpoint to trap, and record RIP.
// Returns false when the loop should stop.
bool WatchpointStep() {
    gdb_continue();

    if (!gdb_wait_for_stop(30000)) {
        printf("[!] Watchpoint did not trigger within 30s, stopping\n");
        return false;
    }

    const uint64_t rip = gdb_read_register("rip");
    if (rip == 0) {
        printf("[!] Could not read RIP, stopping\n");
        return false;
    }

    switch (RecordWatchpointHit(rip)) {
    case 1:
        printf("[+] New access from 0x%llx\n", (unsigned long long)rip);
        return true;
    case 0:
        printf("[=] Duplicate access from 0x%llx, skipping\n", (unsigned long long)rip);
        return true;
    default:
        printf("[!] Hit the %d-entry limit, stopping\n", MAX_WATCHPOINTS);
        return false;
    }
}

// True between gdb_init() and gdb_cleanup(). The UI can clear
// FindAccessesesClicked at any time (the "Exit accesses tab" button), and the
// worker owns the gdb session, so it has to notice that and tear the session
// down itself -- otherwise the gdb process and the planted watchpoint would
// outlive the tab.
bool g_wpSessionActive = false;

void StopWatchpointLoop() {
    if (g_wpSessionActive) {
        gdb_clear_all_watchpoints();
        gdb_cleanup();
        g_wpSessionActive = false;
    }
    state1_s.wp_loop_completed     = true;
    state1_s.FindAccessesesClicked = false;
    gdb_state_c.wp_started         = false;
    gdb_state_c.gdb_start_init     = true;
}

void ServiceWatchpoints() {
    if (!state1_s.FindAccessesesClicked) {
        if (g_wpSessionActive) {
            printf("[>] Leaving the accesses view, detaching gdb\n");
            StopWatchpointLoop();
        }
        return;
    }

    // Bring gdb up once per session.
    if (gdb_state_c.gdb_start_init) {
        if (!gdb_init()) {
            // This used to abort(), taking the whole GUI down because gdb was
            // not installed or the VM had no gdbstub.
            printf("[!] gdb failed to initialize; 'find what accesses' is unavailable.\n"
                   "    Start the VM with -gdb tcp::1234 and make sure gdb is installed.\n");
            StopWatchpointLoop();
            return;
        }
        g_wpSessionActive          = true;
        gdb_state_c.gdb_start_init = false;
    }

    // Plant the watchpoint once.
    if (!gdb_state_c.wp_started) {
        const uint64_t address = state1_s.watchAddress.load();

        // awatch: traps reads *and* writes, which is what "what accesses this"
        // means. A plain write watchpoint missed every read.
        if (gdb_set_watchpoint(address, 4, GDB_WATCH_ACCESS) != 0) {
            printf("[!] Failed to set watchpoint at 0x%llx\n",
                   (unsigned long long)address);
            StopWatchpointLoop();
            return;
        }
        gdb_state_c.wp_started = true;
        printf("[+] Watching 0x%llx for reads and writes\n",
               (unsigned long long)address);
    }

    if (!state1_s.wp_loop_completed && state1_s.FindAccessesesClicked) {
        if (!WatchpointStep()) {
            StopWatchpointLoop();
        }
    }
}

#endif // __linux__

void ServiceProcessTab() {
    if (state0_s.firstTimeInTab.exchange(false)) {
        list_all_processes(hVMM);
        state0_s.firstTimeInTab_Completed = true;
    }

    if (state0_s.ButtonRefreshProcessClicked.exchange(false)) {
        process_name.clear();
        DLL_Name.clear();
        process_id           = 0;
        process_handle       = 0;
        process_base_address = 0;
        DLL_base_address     = 0;
        process_size         = 0;
        DLL_size             = 0;

        // The selection indexes into a list that is about to change size.
        selectedProcessIndex = -1;

        list_all_processes(hVMM);
    }

    if (state0_s.AttachProcessButtonClicked.exchange(false)) {
        const int index = selectedProcessIndex.load();

        ProcessEntry entry;
        if (index < 0 || !ProcessList_Get((size_t)index, entry)) {
            // The list can shrink between the click and this handler running;
            // it used to index the raw array here with no bounds check at all.
            printf("[!] Selected process is no longer in the list\n");
            return;
        }

        printf("[>] Attaching to PID %lu - %s\n",
               (unsigned long)entry.pid, entry.name.c_str());

        process_name = entry.name;
        process_id   = entry.pid;

        if (!get_process_base_address(process_name, process_id)) {
            printf("[!] Attached, but could not resolve the module base for %s\n",
                   process_name.c_str());
        }
    }
}

void ServiceSearchTab() {
    if (state1_s.FirstMemorySearch.exchange(false)) {
        MemorySearch_FirstScan(process_id);
        state1_s.currentPage  = 0;
        state1_s.g_isFirstScan = false;
    }

    if (state1_s.NextMemorySearch.exchange(false)) {
        // This used to run on the render thread, freezing the UI for the whole
        // pass and mutating the result vector the renderer was walking.
        MemorySearch_NextScan(process_id);
        state1_s.currentPage = 0;
    }
}

void WorkerMain() {
    printf("[+] Worker thread started\n");

    while (g_running) {
        {
            std::unique_lock<std::mutex> lock(g_wakeMutex);
            // Sleep until the UI raises a request. The timeout is a safety net
            // for the watchpoint loop, which needs to keep stepping without a
            // new notification. This replaces a `while(tabCount==1){}` spin
            // that pinned a core for the lifetime of the process.
            g_wake.wait_for(lock, std::chrono::milliseconds(50));
        }

        if (!g_running) {
            break;
        }

        switch (imGuiMenu::tabCount.load()) {
        case 1:
            ServiceProcessTab();
            break;
        case 2:
            ServiceSearchTab();
            break;
        default:
            break;
        }

#ifdef __linux__
        // Runs on every tab, not just the search tab: it is also what notices
        // that the request flag was cleared and tears the gdb session down.
        ServiceWatchpoints();
#endif
    }

#ifdef __linux__
    // Never leave a gdb process or a planted watchpoint behind on shutdown.
    StopWatchpointLoop();
#endif

    printf("[+] Worker thread stopped\n");
}

} // namespace

void Core_Start() {
    if (g_running) {
        return;
    }
    g_running = true;
    g_worker  = std::thread(WorkerMain);
}

void Core_Stop() {
    if (!g_running) {
        return;
    }

    g_running = false;

    // Make sure an in-flight scan gives up promptly.
    g_searchCancel = true;
    state1_s.FindAccessesesClicked = false;

    g_wake.notify_all();

    if (g_worker.joinable()) {
        g_worker.join();
    }
}

void Core_Notify() {
    g_wake.notify_all();
}

size_t Watchpoints_Snapshot(std::vector<uint64_t>& out) {
    std::lock_guard<std::mutex> lock(g_wpMutex);
    out = g_wpHits;
    return out.size();
}

void Watchpoints_Clear() {
    std::lock_guard<std::mutex> lock(g_wpMutex);
    g_wpHits.clear();
}
