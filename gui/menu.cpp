#include "menu.hpp"
#include "core.hpp"
#include "list.hpp"
#include "search.hpp"

#include <cinttypes>
#include <string>
#include <vector>

std::atomic<int> imGuiMenu::tabCount{1};

namespace {

// Everything the render thread draws comes out of a snapshot taken under a
// short lock. The render thread never holds a reference into the scanning
// thread's containers across an ImGui call -- an in-flight scan reallocates
// them, which is what used to dangle the table's iterators.
std::vector<ProcessEntry>       g_processView;
std::vector<MemorySearchResult> g_pageView;

const char* const kTypeNames[] = {
    "Byte", "Word", "DWORD", "QWORD", "Float", "Double", "String"
};

const char* const kCompareNames[] = {
    "Exact Value", "Increased", "Decreased", "Unchanged", "Changed",
    "Increased by", "Decreased by", "Increased by %", "Decreased by %"
};

int AlignmentFromIndex(int index) {
    switch (index) {
    case 0:  return 1;
    case 1:  return 2;
    case 2:  return 4;
    default: return 8;
    }
}

} // namespace

void imGuiMenu::setStyle() {
    ImGuiStyle* style = &ImGui::GetStyle();

    style->FramePadding     = ImVec2(5, 5);
    style->FrameBorderSize  = 1.f;
    style->FrameRounding    = 0.f;
    style->WindowPadding    = ImVec2(6, 6);
    style->GrabRounding     = 0.f;
    style->GrabMinSize      = 20.f;
    style->ButtonTextAlign  = ImVec2(0.5f, 0.5f);
    style->ItemSpacing      = ImVec2(9, 4);

    const ImColor darkGrey       = ImColor(29, 31, 31, 255);
    const ImColor lightGrey      = ImColor(38, 42, 43, 255);
    const ImColor lightGreyTrans = ImColor(38, 42, 43, 175);
    const ImColor accent         = ImColor(235, 106, 2, 255);

    style->Colors[ImGuiCol_FrameBg]        = lightGrey;
    style->Colors[ImGuiCol_FrameBgHovered] = darkGrey;
    style->Colors[ImGuiCol_FrameBgActive]  = darkGrey;

    style->Colors[ImGuiCol_TitleBgActive]    = accent;
    style->Colors[ImGuiCol_TitleBgCollapsed] = lightGreyTrans;

    style->Colors[ImGuiCol_ChildBg]   = darkGrey;
    style->Colors[ImGuiCol_MenuBarBg] = lightGrey;
    style->Colors[ImGuiCol_WindowBg]  = lightGrey;

    style->Colors[ImGuiCol_CheckMark]        = accent;
    style->Colors[ImGuiCol_Button]           = accent;
    style->Colors[ImGuiCol_ButtonHovered]    = accent;
    style->Colors[ImGuiCol_SliderGrab]       = accent;
    style->Colors[ImGuiCol_SliderGrabActive] = accent;
    style->Colors[ImGuiCol_ResizeGrip]       = accent;
    style->Colors[ImGuiCol_ResizeGripHovered]= accent;
    style->Colors[ImGuiCol_HeaderHovered]    = accent;
    style->Colors[ImGuiCol_HeaderActive]     = accent;
    style->Colors[ImGuiCol_PlotHistogram]    = accent;
}

// ---------------------------------------------------------------------------
// Process tab
// ---------------------------------------------------------------------------

void imGuiMenu::process_tab_render() {
    ImGui::BeginChild("Attach Process Tab", ImVec2(0, 0), true);

    ImGui::PushFont(imGuiMenu::titleText);
    ImGui::Text("Select a process");
    ImGui::PopFont();

    if (!state0_s.firstTimeInTab_Completed) {
        state0_s.firstTimeInTab = true;
        Core_Notify();
    }

    if (ImGui::Button("Refresh Processes")) {
        state0_s.ButtonRefreshProcessClicked = true;
        Core_Notify();
    }

    ProcessList_Snapshot(g_processView);
    const int  count       = (int)g_processView.size();
    const int  selected    = selectedProcessIndex.load();
    const bool hasSelection = (selected >= 0 && selected < count);

    ImGui::PushItemWidth(-1);

    const char* previewText = hasSelection ? g_processView[selected].name.c_str()
                            : (count > 0 ? "Select a process..." : "No processes found");

    if (ImGui::BeginCombo("##ProcessList", previewText)) {
        if (count > 0) {
            ImGui::TextDisabled("Found %d processes", count);
            ImGui::Separator();

            ImGui::BeginChild("##ProcessListChild", ImVec2(0, 200), false);
            for (int i = 0; i < count; i++) {
                char label[300];
                snprintf(label, sizeof(label), "[%lu] %s",
                         (unsigned long)g_processView[i].pid,
                         g_processView[i].name.c_str());

                if (ImGui::Selectable(label, i == selected)) {
                    selectedProcessIndex = i;
                    printf("[+] Selected: PID %lu - %s\n",
                           (unsigned long)g_processView[i].pid,
                           g_processView[i].name.c_str());
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::Text("No processes available");
            ImGui::Text("Click 'Refresh Processes' to enumerate");
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    if (count > 0) {
        ImGui::TextDisabled("Total: %d process%s", count, count == 1 ? "" : "es");
    }

    if (hasSelection) {
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                           "[+] Currently selected: PID %lu - %s",
                           (unsigned long)g_processView[selected].pid,
                           g_processView[selected].name.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (!hasSelection) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
    }

    if (ImGui::Button("Attach Process", ImVec2(-1, 0)) && hasSelection) {
        state0_s.AttachProcessButtonClicked = true;
        Core_Notify();
    }

    ImGui::PopStyleColor(3);

    if (process_id) {
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::Separator();
        ImGui::Text("Attached: PID %lu - %s", (unsigned long)process_id, process_name.c_str());
        ImGui::Text("Base: 0x%llX  Size: 0x%lX",
                    (unsigned long long)process_base_address, (unsigned long)process_size);
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Memory search tab
// ---------------------------------------------------------------------------

void imGuiMenu::mem_search_render() {
    ImGui::BeginChild("Memory Search Tab", ImVec2(0, 0), true);

    ImGui::PushFont(imGuiMenu::titleText);
    if (process_id) {
        ImGui::Text("PID: %lu - %s", (unsigned long)process_id, process_name.c_str());
    } else {
        ImGui::Text("No process attached!");
    }
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));

    if (process_id == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                           "Please select and attach a process first");
        ImGui::EndChild();
        return;
    }

    static int   selectedType      = MVT_DWORD;
    static int   selectedCompare   = MCT_EXACT;
    static int   selectedAlignment = 2;          // 4 bytes
    static char  valueInput[64]    = "0";
    static char  stringInput[256]  = "";
    static float increasedByVal      = 1.0f;
    static float decreasedByVal      = 1.0f;
    static float increasedByPercent  = 10.0f;
    static float decreasedByPercent  = 10.0f;
    static int   selectedResult      = -1;
    static char  newValueInput[64]   = "0";
    static bool  showWatchedOnly     = false;
    static int   resultsPerPage      = 100;
    static char  resultsPerPageInput[32] = "100";

    // Type
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##Type", &selectedType, kTypeNames, IM_ARRAYSIZE(kTypeNames));

    // Alignment
    ImGui::SameLine();
    ImGui::Text("Align:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* alignments[] = { "1", "2", "4", "8" };
    ImGui::Combo("##Alignment", &selectedAlignment, alignments, IM_ARRAYSIZE(alignments));

    // Value
    if (selectedType == MVT_STRING) {
        ImGui::Text("String:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##StringValue", stringInput, sizeof(stringInput));
    } else {
        ImGui::Text("Value:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##Value", valueInput, sizeof(valueInput));
    }

    ImGui::Separator();

    // Compare mode
    ImGui::Text("Compare:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##Compare", &selectedCompare, kCompareNames, IM_ARRAYSIZE(kCompareNames));

    if (selectedCompare == MCT_INCREASED_BY) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputFloat("##IncreasedBy", &increasedByVal, 0.1f, 1.0f, "%.2f");
    } else if (selectedCompare == MCT_DECREASED_BY) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputFloat("##DecreasedBy", &decreasedByVal, 0.1f, 1.0f, "%.2f");
    } else if (selectedCompare == MCT_INCREASED_BY_PCT) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputFloat("##IncreasedByPercent", &increasedByPercent, 0.1f, 1.0f, "%.1f");
    } else if (selectedCompare == MCT_DECREASED_BY_PCT) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputFloat("##DecreasedByPercent", &decreasedByPercent, 0.1f, 1.0f, "%.1f");
    }

    if (selectedCompare != MCT_EXACT) {
        ImGui::TextDisabled("First scan captures a baseline; run Next Scan to filter.");
    }

    ImGui::Separator();

    // Commit the UI state into the search options. This used to happen only
    // inside the combo's on-change branch, so a user who never touched the
    // Align dropdown scanned with alignment 0 -- a zero step.
    auto CommitOptions = [&]() {
        g_currentOptions.type        = selectedType;
        g_currentOptions.compareType = selectedCompare;
        g_currentOptions.alignment   = AlignmentFromIndex(selectedAlignment);

        g_currentOptions.increasedBy        = increasedByVal;
        g_currentOptions.decreasedBy        = decreasedByVal;
        g_currentOptions.increasedByPercent = increasedByPercent;
        g_currentOptions.decreasedByPercent = decreasedByPercent;

        if (selectedType == MVT_STRING) {
            size_t len = strlen(stringInput);
            if (len >= sizeof(g_currentOptions.stringValue)) {
                len = sizeof(g_currentOptions.stringValue) - 1;
            }
            memcpy(g_currentOptions.stringValue, stringInput, len);
            g_currentOptions.stringValue[len] = '\0';
            g_currentOptions.stringLength     = len;
        } else {
            g_currentOptions.stringLength = 0;
            switch (selectedType) {
            case MVT_BYTE:   g_currentOptions.value.byteVal   = (uint8_t)strtoull(valueInput, nullptr, 0); break;
            case MVT_WORD:   g_currentOptions.value.wordVal   = (uint16_t)strtoull(valueInput, nullptr, 0); break;
            case MVT_DWORD:  g_currentOptions.value.dwordVal  = (uint32_t)strtoull(valueInput, nullptr, 0); break;
            case MVT_QWORD:  g_currentOptions.value.qwordVal  = strtoull(valueInput, nullptr, 0); break;
            case MVT_FLOAT:  g_currentOptions.value.floatVal  = strtof(valueInput, nullptr); break;
            case MVT_DOUBLE: g_currentOptions.value.doubleVal = strtod(valueInput, nullptr); break;
            default: break;
            }
        }
    };

    const bool scanning = g_searchRunning.load();

    ImGui::BeginDisabled(scanning);
    if (ImGui::Button("First Scan", ImVec2(120, 0))) {
        CommitOptions();
        selectedResult = -1;
        state1_s.FirstMemorySearch = true;
        Core_Notify();
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Scan", ImVec2(120, 0))) {
        CommitOptions();
        selectedResult = -1;
        state1_s.NextMemorySearch = true;
        Core_Notify();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(80, 0))) {
        MemorySearch_Reset();
        state1_s.g_SearchResultReset = true;
        state1_s.g_isFirstScan       = true;
        selectedResult               = -1;
        state1_s.currentPage         = 0;
    }
    ImGui::EndDisabled();

    if (scanning) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            g_searchCancel = true;
        }
        ImGui::ProgressBar(g_searchProgress.load() / 100.0f, ImVec2(-1, 0));
    }

    const size_t resultCount = MemorySearch_ResultCount();
    const int    resultsType = g_resultsType.load();

    ImGui::Text("Depth: %d", MemorySearch_Depth());
    ImGui::SameLine();
    ImGui::Text("Results: %zu", resultCount);
    if (resultCount > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(scanned as %s)", kTypeNames[resultsType % 7]);
    }

    ImGui::Separator();

    ImGui::Checkbox("Show Watched Only", &showWatchedOnly);
    ImGui::SameLine();
    ImGui::Text("   Results per page:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputText("##ResultsPerPage", resultsPerPageInput, sizeof(resultsPerPageInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        const int newPerPage = atoi(resultsPerPageInput);
        if (newPerPage > 0 && newPerPage <= 100000) {
            resultsPerPage       = newPerPage;
            state1_s.currentPage = 0;
        }
    }

    // Pagination
    int totalPages = 1;
    if (resultCount > 0) {
        totalPages = (int)((resultCount + (size_t)resultsPerPage - 1) / (size_t)resultsPerPage);
        if (totalPages < 1) {
            totalPages = 1;
        }
        if (state1_s.currentPage >= totalPages) {
            state1_s.currentPage = totalPages - 1;
        }

        ImGui::Text("Page %d/%d", state1_s.currentPage.load() + 1, totalPages);
        ImGui::SameLine();
        if (ImGui::Button("<<")) state1_s.currentPage = 0;
        ImGui::SameLine();
        if (ImGui::Button("<") && state1_s.currentPage > 0) state1_s.currentPage--;
        ImGui::SameLine();
        if (ImGui::Button(">") && state1_s.currentPage < totalPages - 1) state1_s.currentPage++;
        ImGui::SameLine();
        if (ImGui::Button(">>")) state1_s.currentPage = totalPages - 1;
    }

    // Take the page snapshot once per frame; everything below renders from it.
    const size_t pageStart = (size_t)state1_s.currentPage.load() * (size_t)resultsPerPage;
    MemorySearch_SnapshotRange(pageStart, (size_t)resultsPerPage, g_pageView);

    ImGui::SameLine();
    if (ImGui::Button("Copy All Page Addresses")) {
        std::string allAddresses;
        for (const auto& result : g_pageView) {
            if (showWatchedOnly && !result.watched) {
                continue;
            }
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)result.address);
            allAddresses += addrStr;
            allAddresses += "\n";
        }
        if (!allAddresses.empty()) {
            ImGui::SetClipboardText(allAddresses.c_str());
        }
    }

    ImGui::Separator();

    // Ctrl+C copies the selected address.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_C) &&
        (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper)) {
        MemorySearchResult picked;
        if (selectedResult >= 0 && MemorySearch_SnapshotOne((size_t)selectedResult, picked)) {
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)picked.address);
            ImGui::SetClipboardText(addrStr);
        }
    }

    const bool findingAccesses = state1_s.FindAccessesesClicked.load();

    if (!findingAccesses) {
        ImGui::BeginChild("ResultsTable", ImVec2(0, 300), true);

        ImGui::Columns(5, "ResultsColumns");
        ImGui::Text("Address");   ImGui::NextColumn();
        ImGui::Text("Current");   ImGui::NextColumn();
        ImGui::Text("Previous");  ImGui::NextColumn();
        ImGui::Text("Type");      ImGui::NextColumn();
        ImGui::Text("Watch");     ImGui::NextColumn();
        ImGui::Separator();

        if (!g_pageView.empty()) {
            for (size_t i = 0; i < g_pageView.size(); i++) {
                const MemorySearchResult& result = g_pageView[i];
                if (showWatchedOnly && !result.watched) {
                    continue;
                }

                const size_t globalIndex = pageStart + i;
                ImGui::PushID((int)globalIndex);

                char addrStr[32];
                snprintf(addrStr, sizeof(addrStr), "0x%llX",
                         (unsigned long long)result.address);

                if (ImGui::Selectable(addrStr, selectedResult == (int)globalIndex,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedResult = (int)globalIndex;
                    const std::string current =
                        MemorySearch_FormatValue(result.currentValue, resultsType);
                    snprintf(newValueInput, sizeof(newValueInput), "%s", current.c_str());
                }

                if (ImGui::BeginPopupContextItem("AddressContextMenu")) {
#ifdef __linux__
                    if (ImGui::MenuItem("Find what accesses this address")) {
                        state1_s.watchAddress          = result.address;
                        state1_s.wp_loop_completed     = false;
                        gdb_state_c.wp_started         = false;
                        gdb_state_c.gdb_start_init     = true;
                        Watchpoints_Clear();
                        state1_s.FindAccessesesClicked = true;
                        Core_Notify();
                    }
#endif
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addrStr);
                    }
                    if (ImGui::MenuItem("Copy Address and Value")) {
                        const std::string current =
                            MemorySearch_FormatValue(result.currentValue, resultsType);
                        char copyStr[384];
                        snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %s",
                                 addrStr, current.c_str());
                        ImGui::SetClipboardText(copyStr);
                    }
                    ImGui::EndPopup();
                }
                ImGui::NextColumn();

                // Formatting is done against the type the results were
                // *scanned* with and is bounds-checked against the stored
                // buffer, so flipping the type combo can no longer read past
                // the end of a value.
                ImGui::TextUnformatted(
                    MemorySearch_FormatValue(result.currentValue, resultsType).c_str());
                ImGui::NextColumn();

                if (result.previousValue != result.currentValue) {
                    ImGui::TextUnformatted(
                        MemorySearch_FormatValue(result.previousValue, resultsType).c_str());
                } else {
                    ImGui::TextUnformatted("---");
                }
                ImGui::NextColumn();

                ImGui::TextUnformatted(kTypeNames[resultsType % 7]);
                ImGui::NextColumn();

                bool watched = result.watched;
                if (ImGui::Checkbox("##watch", &watched)) {
                    MemorySearch_SetWatch(globalIndex, watched);
                }
                ImGui::NextColumn();

                ImGui::PopID();
            }

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Text("Showing %zu-%zu of %zu results",
                        resultCount ? pageStart + 1 : 0,
                        pageStart + g_pageView.size(), resultCount);
        } else {
            ImGui::Columns(1);
            ImGui::Text("No results to display");
        }

        ImGui::EndChild();
    }

#ifdef __linux__
    if (findingAccesses) {
        ImGui::BeginChild("AccessesTable", ImVec2(0, 300), true);

        const float buttonWidth    = 140.0f;
        const float windowWidth    = ImGui::GetWindowSize().x;
        const float padding        = ImGui::GetStyle().WindowPadding.x;
        const float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;
        const float rightSideX     = windowWidth - buttonWidth - padding - scrollbarWidth;

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Accessed by (RIP), watching 0x%llX",
                    (unsigned long long)state1_s.watchAddress.load());

        if (rightSideX > 0) {
            ImGui::SameLine(rightSideX);
        } else {
            ImGui::SameLine();
        }

        if (ImGui::Button("Exit accesses tab", ImVec2(buttonWidth, 0.0f))) {
            // Just ask the worker to stop; it owns the gdb session, so the UI
            // thread must not tear it down underneath it.
            state1_s.FindAccessesesClicked = false;
            state1_s.wp_loop_completed     = true;
            Core_Notify();
        }

        ImGui::Separator();

        std::vector<uint64_t> hits;
        Watchpoints_Snapshot(hits);

        if (hits.empty()) {
            ImGui::TextDisabled("Waiting for the guest to touch this address...");
        }

        for (size_t i = 0; i < hits.size(); i++) {
            ImGui::PushID((int)i);

            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)hits[i]);

            ImGui::Selectable(addrStr);

            if (ImGui::BeginPopupContextItem("WpAddressContextMenu")) {
                if (ImGui::MenuItem("Copy Address")) {
                    ImGui::SetClipboardText(addrStr);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
#endif

    // Edit section for the selected result.
    MemorySearchResult selected;
    if (selectedResult >= 0 && MemorySearch_SnapshotOne((size_t)selectedResult, selected)) {
        ImGui::Separator();

        char addrStr[32];
        snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)selected.address);

        if (ImGui::Button("Copy Selected Address")) {
            ImGui::SetClipboardText(addrStr);
        }
        ImGui::SameLine();
        ImGui::Text("Edit Address: %s", addrStr);

        ImGui::Text("New Value:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##NewValue", newValueInput, sizeof(newValueInput));

        ImGui::SameLine();
        if (ImGui::Button("Write")) {
            // Parsed in the destination type, so "1.5" into a float stays 1.5.
            MemorySearch_WriteValueFromString(selected.address, newValueInput, resultsType);
        }

        ImGui::SameLine();
        if (ImGui::Button("Add to Watch")) {
            MemorySearch_SetWatch((size_t)selectedResult, true);
        }

        ImGui::SameLine();
        if (ImGui::Button("Remove from Watch")) {
            MemorySearch_SetWatch((size_t)selectedResult, false);
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh Value")) {
            MemorySearch_RefreshValue((size_t)selectedResult);
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Other tabs
// ---------------------------------------------------------------------------

void imGuiMenu::miscRender() {
    ImGui::BeginChild("Misc Tab", ImVec2(0, 0), true);

    ImGui::PushFont(imGuiMenu::titleText);
    ImGui::Text("Miscellaneous");
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));

    if (process_id) {
        if (ImGui::Button("Dump module list to console")) {
            DebugAllModules();
        }
        if (ImGui::Button("Dump memory regions to console")) {
            PrintMemoryRegions(process_id);
        }
    } else {
        ImGui::TextDisabled("Attach to a process to enable these.");
    }

    ImGui::EndChild();
}

void imGuiMenu::aboutMeRender() {
    ImGui::BeginChild("About Tab", ImVec2(0, 0), true);

    ImGui::PushFont(imGuiMenu::titleText);
    ImGui::Text("About DMA-Memory-Editor");
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));

    ImGui::Text("DMA-Memory-Editor");
    ImGui::Text("Version: 1.0.0");

    ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace * 2));

    ImGui::PushFont(imGuiMenu::subTitleText);
    ImGui::Text("Features:");
    ImGui::PopFont();

    ImGui::BulletText("Memory search (exact, increased, decreased, changed, unchanged)");
    ImGui::BulletText("Memory write");
#ifdef __linux__
    ImGui::BulletText("Hardware watchpoints via the QEMU gdbstub");
#endif

    ImGui::EndChild();
}

void imGuiMenu::menuBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 10));
    ImGui::BeginMenuBar();

    const float windowWidth = ImGui::GetWindowWidth();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        (windowWidth > 600) ? ImVec2(20, 0) : ImVec2(10, 0));

    if (ImGui::MenuItem("Attach Process", NULL, tabCount == 1, true)) tabCount = 1;
    ImGui::SameLine();
    if (ImGui::MenuItem("Memory Search", NULL, tabCount == 2, true)) tabCount = 2;
    ImGui::SameLine();
    if (ImGui::MenuItem("Misc", NULL, tabCount == 3, true)) tabCount = 3;
    ImGui::SameLine();
    if (ImGui::MenuItem("About", NULL, tabCount == 4, true)) tabCount = 4;
    ImGui::SameLine();

    ImGui::PopStyleVar();
    ImGui::EndMenuBar();
    ImGui::PopStyleVar();
}

void imGuiMenu::renderMenu(bool state) {
    ImGui::PushFont(normalText);

    int width  = 800;
    int height = 600;
    if (Render::glfwWindow) {
        glfwGetWindowSize(Render::glfwWindow, &width, &height);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)width, (float)height));

    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
                                          ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("DMA-Memory-Editor", &state, window_flags);

    menuBar();

    switch (tabCount.load()) {
    case 1: process_tab_render(); break;
    case 2: mem_search_render();  break;
    case 3: miscRender();         break;
    case 4: aboutMeRender();      break;
    default: break;
    }

    ImGui::End();
    ImGui::PopFont();
}
