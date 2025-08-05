// Include the most common headers from the C standard library
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <map>
#include <cstdio>


#include <switch.h>
#include <dirent.h> 

#include "scanner.hpp"
#include "cheat_generator.hpp"

struct DumpInfo {
    std::string gameName;
    std::string titleIdStr;
    std::string buildIdStr;
    std::string fullLogPath;
};

void runDefaultScan();
void enterManageMode();
void enterActionMenu(const DumpInfo& selectedDump);
std::string getGameName(u64 titleID);
std::vector<DumpInfo> scanForDumps();
void displayDumpList(const std::vector<DumpInfo>& dumps, size_t selectedIndex);
void displayActionMenu(const DumpInfo& dump, int selectedIndex);
void regenerateCheats(const DumpInfo& dump);
void deleteDump(const DumpInfo& dump);

static std::map<u64, std::string> g_titleNameCache;

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    if (appletGetAppletType() == AppletType_Application)
    {
        enterManageMode();
    }
    else
    {
        runDefaultScan();
    }
    
    consoleExit(NULL);
    return 0;
}


void enterManageMode() {
    PadState pad;
    padInitializeDefault(&pad);

    printf("Scanning for previous dumps...\n");
    consoleUpdate(NULL);
    
    std::vector<DumpInfo> dumps = scanForDumps();
    
    if (dumps.empty()) {
        printf("No previous dumps found.\n");
        printf("To create a dump: launch a game, then run this app from the Album.\n\n");
        printf("Press + to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_Plus) break;
        }
        return;
    }

    size_t selectedIndex = 0;
    bool needsRedraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Down) {
            selectedIndex++;
            if (selectedIndex >= dumps.size()) {
                selectedIndex = 0;
            }
            needsRedraw = true;
        }

        if (kDown & HidNpadButton_Up) {
            if (selectedIndex == 0) {
                selectedIndex = dumps.size() - 1;
            } else {
                selectedIndex--;
            }
            needsRedraw = true;
        }
        
        if (kDown & HidNpadButton_A) {
            enterActionMenu(dumps[selectedIndex]);
            // After returning, rescan dumps in case one was deleted
            dumps = scanForDumps();
            if (selectedIndex >= dumps.size() && !dumps.empty()) {
                selectedIndex = dumps.size() - 1;
            }
            needsRedraw = true; 
        }

        if (kDown & HidNpadButton_Plus) {
            break;
        }

        if (needsRedraw) {
            if (dumps.empty()) { // Handle case where last dump was deleted
                consoleClear();
                printf("No previous dumps found.\nPress + to exit.\n");
                consoleUpdate(NULL);
                while(appletMainLoop()) {
                    padUpdate(&pad);
                    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
                }
                return;
            } else {
                displayDumpList(dumps, selectedIndex);
                consoleUpdate(NULL);
            }
            needsRedraw = false;
        }
    }
}

void enterActionMenu(const DumpInfo& selectedDump) {
    PadState pad;
    padInitializeDefault(&pad);
    
    int selectedIndex = 0;
    const int numOptions = 3;
    bool needsRedraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Down) {
            selectedIndex = (selectedIndex + 1) % numOptions;
            needsRedraw = true;
        }
        if (kDown & HidNpadButton_Up) {
            selectedIndex = (selectedIndex - 1 + numOptions) % numOptions;
            needsRedraw = true;
        }

        if (kDown & HidNpadButton_A) {
            switch (selectedIndex) {
                case 0:
                    regenerateCheats(selectedDump);
                    break;
                case 1:
                    deleteDump(selectedDump);
                    return;
                case 2:
                    return;
            }
            printf("\nPress any key to continue...");
            consoleUpdate(NULL);
            svcSleepThread(500'000'000); // Debounce to prevent reading same A press
            while(appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad)) break;
            }
            needsRedraw = true;
        }

        if (kDown & HidNpadButton_B) {
            break; // Go back
        }

        if (needsRedraw) {
            displayActionMenu(selectedDump, selectedIndex);
            consoleUpdate(NULL);
            needsRedraw = false;
        }
    }
}

void regenerateCheats(const DumpInfo& dump) {
    consoleClear();
    printf("Regenerating cheats for %s...\n", dump.gameName.c_str());
    consoleUpdate(NULL);
    
    const char* config_path = "sdmc:/config/ue4cheatcreator/config.json";
    
    struct stat buffer;   
    if (stat(config_path, &buffer) != 0) {
        printf("Error: config.json not found at %s\n", config_path);
        return;
    }
    
    // Create necessary directories for cheat file
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/contents", 0777);
    std::string title_dir_path = "sdmc:/atmosphere/contents/" + dump.titleIdStr;
    mkdir(title_dir_path.c_str(), 0777);
    std::string cheats_dir_path = title_dir_path + "/cheats";
    mkdir(cheats_dir_path.c_str(), 0777);
    std::string output_cheat_path = cheats_dir_path + "/" + dump.buildIdStr + ".txt";

    ParsedLog log_data = parseLogFile(dump.fullLogPath);
    if (!log_data.empty()) {
        generateCheatsFromConfig(log_data, config_path, output_cheat_path);
    } else {
        printf("Error: Log data is empty or could not be parsed.\n");
    }
}

void deleteDump(const DumpInfo& dump) {
    consoleClear();
    printf("Are you sure you want to delete the dump for:\n%s\n\n", dump.gameName.c_str());
    printf("(A) Yes, delete it\n");
    printf("(B) No, go back\n");
    consoleUpdate(NULL);

    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_A) {
            // Delete the .log file
            if (std::remove(dump.fullLogPath.c_str()) == 0) {
                printf("Deleted: %s\n", dump.fullLogPath.c_str());
            } else {
                printf("Error deleting: %s\n", dump.fullLogPath.c_str());
            }
            
            // Also delete the original .txt file if it exists
            std::string original_txt_path = dump.fullLogPath.substr(0, dump.fullLogPath.length() - 3) + "txt";
            std::remove(original_txt_path.c_str());

            printf("\nReturning to list...\n");
            consoleUpdate(NULL);
            svcSleepThread(2'000'000'000);
            break;
        }
        if (kDown & HidNpadButton_B) {
            break;
        }
    }
}

std::string getGameName(u64 titleID) {
    if (g_titleNameCache.count(titleID)) return g_titleNameCache[titleID];
    std::string nameStr;
    NsApplicationControlData* controlData = new NsApplicationControlData();
    size_t controlSize = 0;
    if (R_SUCCEEDED(nsInitialize())) {
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleID, controlData, sizeof(NsApplicationControlData), &controlSize))) {
            NacpLanguageEntry* langEntry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &langEntry))) nameStr = std::string(langEntry->name);
        }
        nsExit();
    }
    delete controlData;
    if (nameStr.empty()) {
        char titleIdHex[17];
        snprintf(titleIdHex, sizeof(titleIdHex), "%016lX", titleID);
        nameStr = std::string(titleIdHex);
    }
    g_titleNameCache[titleID] = nameStr;
    return nameStr;
}

std::vector<DumpInfo> scanForDumps() {
    std::vector<DumpInfo> foundDumps;
    const char* baseDir = "sdmc:/switch/UE4cfgdumper/";
    DIR* dir = opendir(baseDir);
    if (!dir) return foundDumps;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
            std::string titleIdStr = entry->d_name;
            u64 titleId = 0;
            sscanf(titleIdStr.c_str(), "%lX", &titleId);
            std::string titleDirStr = std::string(baseDir) + titleIdStr;
            DIR* titleDir = opendir(titleDirStr.c_str());
            if (titleDir) {
                struct dirent* logEntry;
                while ((logEntry = readdir(titleDir)) != NULL) {
                    std::string filename = logEntry->d_name;
                    if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".log") {
                        DumpInfo info;
                        info.titleIdStr = titleIdStr;
                        info.buildIdStr = filename.substr(0, filename.length() - 4);
                        info.fullLogPath = titleDirStr + "/" + filename;
                        info.gameName = getGameName(titleId);
                        foundDumps.push_back(info);
                    }
                }
                closedir(titleDir);
            }
        }
    }
    closedir(dir);
    return foundDumps;
}

void displayDumpList(const std::vector<DumpInfo>& dumps, size_t selectedIndex) {
    consoleClear();
    printf("ue4cheatcreator - Manage Mode\n\n");
    printf("Use D-Pad to navigate. Press (A) to select. Press (+) to exit.\n");
    printf("----------------------------------------------------------------\n\n");
    for (size_t i = 0; i < dumps.size(); ++i) {
        if (i == selectedIndex) printf(CONSOLE_GREEN "> " CONSOLE_RESET);
        else printf("  ");
        printf("[%zu] %s\n", i + 1, dumps[i].gameName.c_str());
        printf("    Title ID: %s\n", dumps[i].titleIdStr.c_str());
        printf("    Build ID: %s\n\n", dumps[i].buildIdStr.c_str());
    }
}

void displayActionMenu(const DumpInfo& dump, int selectedIndex) {
    consoleClear();
    printf("Selected: %s\n", dump.gameName.c_str());
    printf("Build ID: %s\n", dump.buildIdStr.c_str());
    printf("----------------------------------------------------------------\n\n");
    const char* options[] = {"Regenerate Cheats from config.json", "Delete this Dump", "Back to List"};
    for (int i = 0; i < 3; ++i) {
        if (i == selectedIndex) printf(CONSOLE_GREEN "> " CONSOLE_RESET);
        else printf("  ");
        printf("%s\n", options[i]);
    }
}


void runDefaultScan() {
    consoleClear();
    printf("Applet Mode: Default Scan Mode\n");
    consoleUpdate(NULL);
    svcSleepThread(1'000'000'000);

    if (run_scan()) {
        printf("Scan successful. Log file has been created.\n");
        consoleUpdate(NULL);
        svcSleepThread(1'000'000'000);

        char titleIdStr[17];
        snprintf(titleIdStr, sizeof(titleIdStr), "%016lX", cheatMetadata.title_id);
        
        char buildIdStr[17];
        u64 bid_val = 0;
        memcpy(&bid_val, cheatMetadata.main_nso_build_id, sizeof(u64));
        u64 bid_swapped = __builtin_bswap64(bid_val);
        snprintf(buildIdStr, sizeof(buildIdStr), "%016lX", bid_swapped);
        
        std::string log_path = "sdmc:/switch/UE4cfgdumper/" + std::string(titleIdStr) + "/" + std::string(buildIdStr) + ".log";
        std::string gameName = getGameName(cheatMetadata.title_id);

        DumpInfo newDump = {
            gameName,
            std::string(titleIdStr),
            std::string(buildIdStr),
            log_path
        };
        
        regenerateCheats(newDump);
    } else {
        printf("Scan failed. Please check the error messages above.\n");
    }

    printf("\nProcess complete. Press + to exit.\n");
    consoleUpdate(NULL);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {   
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    }
}