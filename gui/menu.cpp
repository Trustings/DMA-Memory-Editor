#include "menu.hpp"
//#include "config.hpp"


extern bool state;

std::atomic<int> imGuiMenu::tabCount = 1;

int selectedIndex = -1;

void imGuiMenu::setStyle() {
    // Setting styles
    ImGuiStyle* style = &ImGui::GetStyle();

    // Sizes
    style->FramePadding = ImVec2(5, 5);
    style->FrameBorderSize = 1.f;
    style->FrameRounding = 0.f;

    style->WindowPadding = ImVec2(6, 6);

    style->GrabRounding = 0.f;
    style->GrabMinSize = 20.f;

    style->ButtonTextAlign = ImVec2(0.5, 0.5);

    style->ItemSpacing = ImVec2(9, 4);

    // Colour setup
    ImColor darkGrey = ImColor(29, 31, 31, 255);
    ImColor lightGrey = ImColor(38, 42, 43, 255);
    ImColor lightGreyTrans = ImColor(38, 42, 43, 175);

    ImColor telos_orange = ImColor(235, 106, 2, 255);

    // Colours
    style->Colors[ImGuiCol_FrameBg] = lightGrey;
    style->Colors[ImGuiCol_FrameBgHovered] = darkGrey;
    style->Colors[ImGuiCol_FrameBgActive] = darkGrey;

    style->Colors[ImGuiCol_TitleBgActive] = telos_orange;
    style->Colors[ImGuiCol_TitleBgCollapsed] = lightGreyTrans;

    style->Colors[ImGuiCol_ChildBg] = darkGrey;

    style->Colors[ImGuiCol_MenuBarBg] = lightGrey;
    style->Colors[ImGuiCol_WindowBg] = lightGrey;

    style->Colors[ImGuiCol_CheckMark] = telos_orange;

    style->Colors[ImGuiCol_Button] = telos_orange;
    style->Colors[ImGuiCol_ButtonHovered] = telos_orange;

    style->Colors[ImGuiCol_SliderGrab] = telos_orange;
    style->Colors[ImGuiCol_SliderGrabActive] = telos_orange;

    style->Colors[ImGuiCol_ResizeGrip] = telos_orange;
    style->Colors[ImGuiCol_ResizeGripHovered] = telos_orange;

    style->Colors[ImGuiCol_HeaderHovered] = telos_orange;
    style->Colors[ImGuiCol_HeaderActive] = telos_orange;
}

void imGuiMenu::verticalSplitter(float width, float height) {
    ImGui::SameLine();
    ImGui::InvisibleButton("vsplitter", ImVec2(8.0f, height));
    if (ImGui::IsItemActive())
        width += ImGui::GetIO().MouseDelta.x;
    ImGui::SameLine();
}

void imGuiMenu::horizontalSplitter(float height) {
    ImGui::InvisibleButton("hsplitter", ImVec2(-1, 8.0f));
    if (ImGui::IsItemActive())
        height += ImGui::GetIO().MouseDelta.y;
}

void imGuiMenu::process_tab_render() {
    if (tabCount == 1) {

        ImGui::BeginChild("Attach Process Tab", ImVec2(0, 0), true);

        ImGui::PushFont(imGuiMenu::titleText);
        ImGui::Text("Select a process");
        ImGui::PopFont();

        // Auto-call list processes on first render of this tab

        if (!state0_s.firstTimeInTab_Completed){

        state0_s.firstTimeInTab = true;

        }


        // Refresh button
        if (ImGui::Button("Refresh Processes")) {

            state0_s.ButtonRefreshProcessClicked = true;

        }

        // Dropdown/Combo box for process selection
        ImGui::PushItemWidth(-1);

        const char* previewText;
        if (process_count > 0 && processes) {
            previewText = "Select a process...";
        } else {
            previewText = "No processes found";
        }

        if (ImGui::BeginCombo("##ProcessList", previewText)) {
            if (process_count > 0 && processes) {
                // Show process count at the top (non-selectable)
                ImGui::TextDisabled("Found %lu processes", process_count);
                ImGui::Separator();

                // Optional search/filter (uncomment if needed)
                // static char searchBuf[128] = "";
                // ImGui::InputText("##Search", searchBuf, sizeof(searchBuf));
                // ImGui::Separator();

                // Use a child window for scrolling if you have many processes
                ImGui::BeginChild("##ProcessListChild", ImVec2(0, 200), false);

                // List all processes
                for (DWORD i = 0; i < process_count; i++) {
                    char processEntry[256];
                    snprintf(processEntry, sizeof(processEntry), "[%lu] %s",
                             processes[i].dwPID, processes[i].szNameLong);

                    // Optional: Add filtering logic here
                    // if (searchBuf[0] && !strstr(processEntry, searchBuf)) continue;

                    if (ImGui::Selectable(processEntry)) {

                        selectedIndex = i;
                        printf("[+] Selected: PID %lu - %s\n",
                               processes[i].dwPID, processes[i].szNameLong);
                    }
                }

                ImGui::EndChild();

                // Show count at bottom too for large lists
                ImGui::Separator();
                ImGui::TextDisabled("%lu processes", process_count);
            } else {
                ImGui::Text("No processes available");
                ImGui::Text("Click 'Refresh Processes' to enumerate");
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Show process count summary
        if (process_count > 0) {
            ImGui::TextDisabled("Total: %lu process%s",
                                process_count, process_count == 1 ? "" : "es");
        }

        // Show which process is currently selected
        if (selectedIndex >= 0 && processes && selectedIndex < (int)process_count) {
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                               "[+] Currently selected: PID %lu - %s",
                               processes[selectedIndex].dwPID,
                               processes[selectedIndex].szNameLong);
        }

        // Button to attach to selected process
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        // Make button state depend on selection
        bool hasSelection = (selectedIndex >= 0 && selectedIndex < (int)process_count);

        if (!hasSelection) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
        }

        if (ImGui::Button("Attach Process", ImVec2(-1, 0))) {
            if (hasSelection && processes) {

                state0_s.AttachProcessButtonClicked = true;
            }
        }

        ImGui::PopStyleColor(3);

        ImGui::EndChild();


    }

}

void CopyToClipboard(const char* text) {
    if (ImGui::BeginMenu("Copy")) {
        if (ImGui::MenuItem("Copy to Clipboard")) {
            ImGui::SetClipboardText(text);
        }
        ImGui::EndMenu();
    }
}

    void imGuiMenu::mem_search_render() {
        if (tabCount == 2) {

            ImGui::BeginChild("Memory Search Tab", ImVec2(0, 0), true);

            ImGui::PushFont(imGuiMenu::titleText);

            // Check if a process is selected
            if (process_id) {
                ImGui::Text("PID: %lu - %s\n", process_id, process_name.c_str());
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

            // ===== MEMORY SEARCH UI =====

            // Static variables for UI state
            static int selectedType = 2; // Default to DWORD
            static int selectedCompare = 0;
            static int selectedAlignment = 2; // 4-byte alignment
            static char valueInput[64] = "0";
            static char stringInput[256] = "";
            static float increasedByVal = 1.0f;
            static float decreasedByVal = 1.0f;
            static float increasedByPercent = 10.0f;
            static float decreasedByPercent = 10.0f;
            static int selectedResult = -1;
            static char newValueInput[64] = "0";
            static bool showWatchedOnly = false;
            static int resultsPerPage = 100;
           // static int state1_s.currentPage = 0;
            static char resultsPerPageInput[32] = "100";

            // Type selection
            const char* types[] = { "Byte", "Word", "DWORD", "QWORD", "Float", "Double", "String" };
            ImGui::Text("Type:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo("##Type", &selectedType, types, IM_ARRAYSIZE(types))) {
                g_currentOptions.type = selectedType;
            }

            // Alignment selection
            const char* alignments[] = { "1", "2", "4", "8" };
            ImGui::Text("Align:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("##Alignment", &selectedAlignment, alignments, IM_ARRAYSIZE(alignments))) {
                int alignVals[] = { 1, 2, 4, 8 };
                g_currentOptions.alignment = alignVals[selectedAlignment];
            }

            // Value input based on type
            if (selectedType == 6) { // String
                ImGui::Text("String:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(300);
                if (ImGui::InputText("##StringValue", stringInput, sizeof(stringInput))) {
                    // Safe string copy
                    size_t len = strlen(stringInput);
                    if (len >= sizeof(g_currentOptions.stringValue)) {
                        len = sizeof(g_currentOptions.stringValue) - 1;
                    }
                    memcpy(g_currentOptions.stringValue, stringInput, len);
                    g_currentOptions.stringValue[len] = '\0';
                    g_currentOptions.value.dwordVal = len;
                }
            } else {
                ImGui::Text("Value:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::InputText("##Value", valueInput, sizeof(valueInput))) {
                    // Parse based on type
                    switch(selectedType) {
                    case 0: g_currentOptions.value.byteVal = (uint8_t)atoi(valueInput); break;
                    case 1: g_currentOptions.value.wordVal = (uint16_t)atoi(valueInput); break;
                    case 2: g_currentOptions.value.dwordVal = (uint32_t)strtoull(valueInput, NULL, 0); break;
                    case 3: g_currentOptions.value.qwordVal = strtoull(valueInput, NULL, 0); break;
                    case 4: g_currentOptions.value.floatVal = (float)atof(valueInput); break;
                    case 5: g_currentOptions.value.doubleVal = atof(valueInput); break;
                    }
                }
            }

            ImGui::Separator();

            // Compare type selection
            const char* compareTypes[] = {
                "Exact Value", "Increased", "Decreased", "Unchanged", "Changed",
                "Increased by", "Decreased by", "Increased by %", "Decreased by %"
            };
            ImGui::Text("Compare:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo("##Compare", &selectedCompare, compareTypes, IM_ARRAYSIZE(compareTypes))) {
                g_currentOptions.compareType = selectedCompare;
            }

            // Additional inputs for increased/decreased by
            if (selectedCompare == 5) { // Increased by
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputFloat("##IncreasedBy", &increasedByVal, 0.1f, 1.0f, "%.2f")) {
                    g_currentOptions.increasedBy = increasedByVal;
                }
            } else if (selectedCompare == 6) { // Decreased by
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputFloat("##DecreasedBy", &decreasedByVal, 0.1f, 1.0f, "%.2f")) {
                    g_currentOptions.decreasedBy = decreasedByVal;
                }
            } else if (selectedCompare == 7) { // Increased by %
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputFloat("##IncreasedByPercent", &increasedByPercent, 0.1f, 1.0f, "%.1f%%")) {
                    g_currentOptions.increasedByPercent = increasedByPercent;
                }
            } else if (selectedCompare == 8) { // Decreased by %
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputFloat("##DecreasedByPercent", &decreasedByPercent, 0.1f, 1.0f, "%.1f%%")) {
                    g_currentOptions.decreasedByPercent = decreasedByPercent;
                }
            }

            ImGui::Separator();

            // Search controls
            if (ImGui::Button("First Scan", ImVec2(120, 0))) {
                // Update current options

                g_currentOptions.type = selectedType;
                g_currentOptions.compareType = selectedCompare;
                g_currentOptions.alignment = (selectedAlignment == 0) ? 1 :
                                                 (selectedAlignment == 1) ? 2 :
                                                 (selectedAlignment == 2) ? 4 : 8;

                state1_s.FirstMemorySearch = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Next Scan", ImVec2(120, 0)) && !state1_s.g_isFirstScan) {
                MemorySearch_NextScan(process_id);
                state1_s.currentPage = 0; // Reset to first page after next scan
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(80, 0))) {
                MemorySearch_Reset();
                state1_s.g_isFirstScan = true;
                selectedResult = -1;
                state1_s.currentPage = 0;
            }

            // Search info
            ImGui::Text("Depth: %d", g_searchDepth);
            ImGui::SameLine();
            ImGui::Text("Results: %zu", g_searchResults.size());

            ImGui::Separator();

            // Results table controls
            ImGui::Checkbox("Show Watched Only", &showWatchedOnly);

            ImGui::SameLine();
            ImGui::Text("   Results per page:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputText("##ResultsPerPage", resultsPerPageInput, sizeof(resultsPerPageInput),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                int newPerPage = atoi(resultsPerPageInput);
                if (newPerPage > 0) {
                    resultsPerPage = newPerPage;
                    state1_s.currentPage = 0; // Reset to first page
                }
            }

            // ===== NEW: ADD COPY ALL BUTTON HERE =====
            ImGui::SameLine();
            if (ImGui::Button("Copy All Page Addresses")) {
                std::string allAddresses;
                size_t startIdx = state1_s.currentPage * resultsPerPage;
                size_t endIdx = (std::min)(startIdx + resultsPerPage, g_searchResults.size());

                static int result = start_mutex_lock();

                for (size_t i = startIdx; i < endIdx; i++) {
                    if (!showWatchedOnly || g_searchResults[i].watched) {
                        char addrStr[64];
                        snprintf(addrStr, sizeof(addrStr), "0x%llX\n", g_searchResults[i].address);
                        allAddresses += addrStr;
                    }
                }

                if (result == 0) {
                end_mutex_lock();
                }
                
                if (!allAddresses.empty()) {
                    ImGui::SetClipboardText(allAddresses.c_str());
                }
            }
            // ===== END NEW SECTION =====

            // Pagination controls
            if (g_searchResults.size() > 0) {
                int totalPages = (int)((g_searchResults.size() + resultsPerPage - 1) / resultsPerPage);

                ImGui::Text("Page %d/%d", state1_s.currentPage + 1, totalPages);
                ImGui::SameLine();

                if (ImGui::Button("<<")) state1_s.currentPage = 0;
                ImGui::SameLine();
                if (ImGui::Button("<") && state1_s.currentPage > 0) state1_s.currentPage--;
                ImGui::SameLine();
                if (ImGui::Button(">") && state1_s.currentPage < totalPages - 1) state1_s.currentPage++;
                ImGui::SameLine();
                if (ImGui::Button(">>")) state1_s.currentPage = totalPages - 1;
            }

            ImGui::Separator();

            // ===== NEW: KEYBOARD SHORTCUT HANDLER (Ctrl+C) =====
            // This must be placed BEFORE the results table so it can capture keyboard input
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow) &&
                ImGui::IsKeyPressed(ImGuiKey_C) &&
                (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper)) {
                if (selectedResult >= 0 && selectedResult < (int)g_searchResults.size()) {
                    char addrStr[64];
                    snprintf(addrStr, sizeof(addrStr), "0x%llX", g_searchResults[selectedResult].address);
                    ImGui::SetClipboardText(addrStr);
                }
            }
            // ===== END KEYBOARD SHORTCUT =====

            // Results table

            if(!state1_s.FindAccessesesClicked){

            if (ImGui::BeginChild("ResultsTable", ImVec2(0, 300), true)) {
                // Headers
                ImGui::Columns(5, "ResultsColumns");
                ImGui::Text("Address");
                ImGui::NextColumn();
                ImGui::Text("Current");
                ImGui::NextColumn();
                ImGui::Text("Previous");
                ImGui::NextColumn();
                ImGui::Text("Type");
                ImGui::NextColumn();
                ImGui::Text("Watch");
                ImGui::NextColumn();
                ImGui::Separator();

                // Calculate pagination range
                if (g_searchResults.size() > 0) {
                    size_t startIdx = state1_s.currentPage * resultsPerPage;
                    size_t endIdx = (std::min)(startIdx + resultsPerPage, g_searchResults.size());

                    // Display only current page results
                    for (size_t i = startIdx; i < endIdx; i++) {
                        if (showWatchedOnly && !g_searchResults[i].watched) continue;

                        ImGui::PushID((int)i);

                        // ===== MODIFIED: Address with right-click copy menu =====
                        char addrStr[32];
                        snprintf(addrStr, sizeof(addrStr), "0x%llX", g_searchResults[i].address);

                        // Address (selectable)
                        if (ImGui::Selectable(addrStr, selectedResult == (int)i, ImGuiSelectableFlags_SpanAllColumns)) {
                            selectedResult = (int)i;
                            snprintf(newValueInput, sizeof(newValueInput), "%llu",
                                     *reinterpret_cast<uint64_t*>(g_searchResults[i].currentValue.data()));
                        }

                        // Right-click context menu for copying
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            ImGui::OpenPopup("AddressContextMenu");
                        }

                        #ifdef __linux__       
                        if (ImGui::BeginPopup("AddressContextMenu")) {
                            if (ImGui::MenuItem("Find what accesses this address")){

                                MAX_WATCHPOINTS = 20;

                                state1_s.FindAccessesesClicked = true;
                                state1_s.wp_loop_completed = false;

                                char *endptr;
                                long int result;

                                result = strtol(addrStr, &endptr, 16);
                                state1_s.strtol_result = result;

                            }
                            if (ImGui::MenuItem("Copy Address")) {
                                ImGui::SetClipboardText(addrStr);
                            }
                            if (ImGui::MenuItem("Copy Address and Value")) {
                                char copyStr[256];
                                if (!g_searchResults[i].currentValue.empty()) {
                                    switch(selectedType) {
                                    case 0: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %u",
                                                 addrStr, *reinterpret_cast<uint8_t*>(g_searchResults[i].currentValue.data())); break;
                                    case 1: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %u",
                                                 addrStr, *reinterpret_cast<uint16_t*>(g_searchResults[i].currentValue.data())); break;
                                    case 2: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %lu",
                                                 addrStr, *reinterpret_cast<uint32_t*>(g_searchResults[i].currentValue.data())); break;
                                    case 3: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %llu",
                                                 addrStr, *reinterpret_cast<uint64_t*>(g_searchResults[i].currentValue.data())); break;
                                    case 4: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %.3f",
                                                 addrStr, *reinterpret_cast<float*>(g_searchResults[i].currentValue.data())); break;
                                    case 5: snprintf(copyStr, sizeof(copyStr), "Address: %s, Value: %.6f",
                                                 addrStr, *reinterpret_cast<double*>(g_searchResults[i].currentValue.data())); break;
                                    default: snprintf(copyStr, sizeof(copyStr), "Address: %s", addrStr);
                                    }
                                } else {
                                    snprintf(copyStr, sizeof(copyStr), "Address: %s", addrStr);
                                }

                                ImGui::SetClipboardText(copyStr);

                            }
                            ImGui::EndPopup();
                     } 
                        #endif 

                        // ===== END MODIFIED SECTION =====

                        ImGui::NextColumn();

                        // Current value

                        static int result = start_mutex_lock();
                 
                        if (!g_searchResults[i].currentValue.empty()) {
                            switch (selectedType) {
                            case 0: ImGui::Text("%u", *reinterpret_cast<uint8_t*>(g_searchResults[i].currentValue.data())); break;
                            case 1: ImGui::Text("%u", *reinterpret_cast<uint16_t*>(g_searchResults[i].currentValue.data())); break;
                            case 2: ImGui::Text("%lu", *reinterpret_cast<uint32_t*>(g_searchResults[i].currentValue.data())); break;
                            case 3: ImGui::Text("%llu", *reinterpret_cast<uint64_t*>(g_searchResults[i].currentValue.data())); break;
                            case 4: ImGui::Text("%.3f", *reinterpret_cast<float*>(g_searchResults[i].currentValue.data())); break;
                            case 5: ImGui::Text("%.6f", *reinterpret_cast<double*>(g_searchResults[i].currentValue.data())); break;
                            default: ImGui::Text("---");
                            }

                            if (result == 0) {
                            end_mutex_lock();
                            }
                            
                        } else {
                            ImGui::Text("---");
                        }
                        ImGui::NextColumn();

                        // Previous value
                        if (!g_searchResults[i].previousValue.empty() &&
                            g_searchResults[i].previousValue != g_searchResults[i].currentValue) {
                            switch(selectedType) {
                            case 0: ImGui::Text("%u", *reinterpret_cast<uint8_t*>(g_searchResults[i].previousValue.data())); break;
                            case 1: ImGui::Text("%u", *reinterpret_cast<uint16_t*>(g_searchResults[i].previousValue.data())); break;
                            case 2: ImGui::Text("%lu", *reinterpret_cast<uint32_t*>(g_searchResults[i].previousValue.data())); break;
                            case 3: ImGui::Text("%llu", *reinterpret_cast<uint64_t*>(g_searchResults[i].previousValue.data())); break;
                            case 4: ImGui::Text("%.3f", *reinterpret_cast<float*>(g_searchResults[i].previousValue.data())); break;
                            case 5: ImGui::Text("%.6f", *reinterpret_cast<double*>(g_searchResults[i].previousValue.data())); break;
                            default: ImGui::Text("---");
                            }
                        } else {
                            ImGui::Text("---");
                        }
                        ImGui::NextColumn();

                        // Type
                        ImGui::Text(types[selectedType]);
                        ImGui::NextColumn();

                        // Watch checkbox
                        bool watched = g_searchResults[i].watched;
                        if (ImGui::Checkbox("##watch", &watched)) {
                            if (watched) {
                                MemorySearch_AddWatch((int)i);
                            } else {
                                MemorySearch_RemoveWatch((int)i);
                            }
                        }
                        ImGui::NextColumn();

                        ImGui::PopID();
                    }

                    // Show range info at the bottom
                    ImGui::Separator();
                    ImGui::Text("Showing %zu-%zu of %zu results",
                                startIdx + 1, endIdx, g_searchResults.size());
                } else {
                    ImGui::Text("No results to display");
                }

                ImGui::Columns(1);
            }
            ImGui::EndChild();
            }

            #ifdef __linux__

            if (state1_s.FindAccessesesClicked) {

                if (ImGui::BeginChild("ResultsTable", ImVec2(0, 300), true)) {

                    float buttonWidth = 120.0f;
                    float windowWidth = ImGui::GetWindowSize().x;
                    float padding = ImGui::GetStyle().WindowPadding.x;
                    float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;

                    // Calculate exact X position for the right corner (accounting for window padding and scrollbar)
                    float rightSideX = windowWidth - buttonWidth - padding - scrollbarWidth;

                    // Vertically align the text to match the height of the button next to it
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Addresses");

                    // Push the button to the same line at the calculated right-side coordinate
                    ImGui::SameLine(rightSideX);
                    if (ImGui::Button("Exit accesses tab", ImVec2(buttonWidth, 0.0f))) {

                        gdb_state_c.gdb_start_init = true;
                        gdb_state_c.wp_started = false;
                        state1_s.wp_loop_completed = false;

                        gdb_continue();
                        gdb_wait_for_stop(50);

                        for (int i = 0; i <= MAX_WATCHPOINTS; i++) {
                            wp_buffer[i].store(0, std::memory_order_relaxed);
                        }

                        MAX_WATCHPOINTS = 0;
                        watchpoint_count = 0;

                        for (int i = 0; i <= MAX_WATCHPOINTS; i++) {
                            wp_buffer[i].store(0, std::memory_order_relaxed);
                        }

                    }

                    ImGui::Separator();

                for (int i = 0; i <= MAX_WATCHPOINTS; i++) {
                    // Skip if the watchpoint value is 0
                    if (wp_buffer[i] <= 1) {
                        continue;
                    }

                    ImGui::PushID(i);

                    char addrStr[32];
                    // Note: If wp_buffer stores actual pointers/addresses, use it directly.
                    // (Kept your original format logic, but changed to match what you need)
                    snprintf(addrStr, sizeof(addrStr), "0x%llX", (unsigned long long)wp_buffer[i]);

                    // Selectable item
                    if (ImGui::Selectable(addrStr, selectedResult == i, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedResult = i;
                    }

                    // Trigger the context menu on right-click of this specific item
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        selectedResult = i;
                        ImGui::OpenPopup("WpAddressContextMenu"); // Use a distinct popup name
                    }

                    // Define the popup content scoped to the item or handled right here
                    if (ImGui::BeginPopup("WpAddressContextMenu")) {
                        if (selectedResult == i) { // Ensure we are acting on the right index
                            if (ImGui::MenuItem("Copy Address")) {
                                ImGui::SetClipboardText(addrStr);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }
     

            }

            ImGui::EndChild();
            }

            #endif

            // Edit section for selected result
            if (selectedResult >= 0 && selectedResult < (int)g_searchResults.size()) {
                ImGui::Separator();

                // ===== NEW: Add copy button for selected address =====
                if (ImGui::Button("Copy Selected Address")) {
                    char addrStr[32];
                    snprintf(addrStr, sizeof(addrStr), "0x%llX", g_searchResults[selectedResult].address);
                    ImGui::SetClipboardText(addrStr);
                }
                ImGui::SameLine();
                // ===== END NEW SECTION =====

                ImGui::Text("Edit Address: 0x%llX", g_searchResults[selectedResult].address);

                ImGui::Text("New Value:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::InputText("##NewValue", newValueInput, sizeof(newValueInput));

                ImGui::SameLine();
                if (ImGui::Button("Write")) {
                    uint64_t newVal = strtoull(newValueInput, NULL, 0);
                    MemorySearch_WriteValue(g_searchResults[selectedResult].address, newVal, selectedType);
                }

                ImGui::SameLine();
                if (ImGui::Button("Add to Watch")) {
                    MemorySearch_AddWatch(selectedResult);
                }

                ImGui::SameLine();
                if (ImGui::Button("Remove from Watch")) {
                    MemorySearch_RemoveWatch(selectedResult);
                }

                ImGui::SameLine();
                if (ImGui::Button("Refresh Value")) {
                    int typeSize = GetTypeSize(selectedType);
                    std::vector<uint8_t> newValue(typeSize);
                    if (vmmdll_read(g_searchResults[selectedResult].address, newValue.data(), typeSize)) {
                        g_searchResults[selectedResult].previousValue = g_searchResults[selectedResult].currentValue;
                        g_searchResults[selectedResult].currentValue = newValue;
                    }
                }
            }

            ImGui::EndChild();
        }
    }

void imGuiMenu::miscRender() {
    if (tabCount == 3) {
        ImGui::BeginChild("Misc Tab", ImVec2(0, 0), true);

        ImGui::PushFont(imGuiMenu::titleText);
        ImGui::Text("Miscellaneous Settings");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));

        // Movement section
        ImGui::PushFont(imGuiMenu::subTitleText);
        ImGui::Text("Empty");
        ImGui::PopFont();
        /*

        ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));
        ImGui::Checkbox("h", &state);

        ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace * 2));

        // Visual section
        ImGui::PushFont(imGuiMenu::subTitleText);
        ImGui::Text("h");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, textSeparatorSpace));
        ImGui::Checkbox("h", &state);
        ImGui::Checkbox("h", &state);
        */

        ImGui::EndChild();
    }
}


void imGuiMenu::aboutMeRender() {
    if (tabCount == 4) {
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

        ImGui::BulletText("Memory Search");
        ImGui::BulletText("Memory Write");
        ImGui::BulletText("Hardware Watchpoints");

        ImGui::EndChild();
    }
}

void imGuiMenu::menuBar() {
    // Set the menu bar to use the full width
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 10));

    ImGui::BeginMenuBar();

    // Get window width for responsive sizing
    float windowWidth = ImGui::GetWindowWidth();

    if (windowWidth > 600) {
        // Normal spacing for wide windows
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 0));
    } else {
        // Reduced spacing for narrow windows
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 0));
    }

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

    // Get the current window size from GLFW
    int width, height;
    if (Render::glfwWindow) {
        glfwGetWindowSize(Render::glfwWindow, &width, &height);
    } else {
        // Fallback values if window not initialized
        width = 800;
        height = 600;
    }

    // Set window to fill the entire GLFW window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // Use flags to make the window fill the space completely
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("tel.os", &state, window_flags);

    // Config
    setStyle();
    menuBar();

    // Render appropriate tab based on selection
    switch (tabCount) {
    case 1:
        process_tab_render();
        break;
    case 2:
        mem_search_render();
        break;
    case 3:
        miscRender();
        break;
    case 4:
        aboutMeRender();
        break;
    }

    ImGui::PopFont();
    ImGui::End();
}
