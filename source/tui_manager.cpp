#include "tui_manager.hpp"
#include "scanner.hpp"
#include "cheat_generator.hpp"
#include "preset_editor.hpp"

#include <switch.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

static std::map<u64, std::string> g_titleNameCache;
const char* MAIN_CONFIG_PATH = "sdmc:/config/ue4cheatcreator-proper/config.json";
const char* PRESET_DIR = "sdmc:/config/ue4cheatcreator-proper/presets";

void showDumpBrowser();
void enterGameDashboard(const DumpInfo& dump);
void runBatchGeneration(const std::vector<DumpInfo>& dumps);
void showAboutScreen();
void selectPresetAndGenerate(const DumpInfo& dump);
void regenerateCheats(const DumpInfo& dump, bool useCategories, const std::string& defaultIndicator, const std::string& configPath);
void deleteDump(const DumpInfo& dump);
void viewCheatFile(const std::string& path);
void toggleCheatFile(const DumpInfo& dump, bool& isEnabled);
std::string getGameName(u64 titleID);

int countLines(const std::string& str) {
    if (str.empty()) return 0;
    int lines = 1;
    for (char c : str) {
        if (c == '\n') lines++;
    }
    return lines;
}

size_t getVisibleLength(const std::string& s) {
    size_t len = 0;
    bool inCode = false;
    for (char c : s) {
        if (c == '\x1b') inCode = true;
        else if (inCode && c == 'm') inCode = false;
        else if (!inCode) len++;
    }
    return len;
}

int countVisualRows(const std::string& str, int consoleWidth) {
    if (str.empty()) return 0;
    
    int rows = 0;
    std::stringstream ss(str);
    std::string segment;

    while (std::getline(ss, segment, '\n')) {
        size_t len = getVisibleLength(segment);
        if (len == 0) {
            rows++;
        } else {
            rows += (len + consoleWidth - 1) / consoleWidth;
        }
    }
    
    if (!str.empty() && str.back() == '\n') rows++;

    return rows;
}

void renderFrame(const std::string& header, const std::vector<std::string>& bodyLines, const std::string& footer) {
    int conWidth = 80;
    int conHeight = 46;
    
    PrintConsole* console = consoleGetDefault();
    if (console) {
        conWidth = console->consoleWidth;
        conHeight = console->consoleHeight;
    }

    printf("\x1b[H");

    printf("%s\n", header.c_str());
    
    int linesUsed = countVisualRows(header, conWidth) + 1;

    for (const auto& line : bodyLines) {
        printf("%s\x1b[K\n", line.c_str());
        
        int visualRows = countVisualRows(line, conWidth);
        linesUsed += (visualRows > 0 ? visualRows : 1);
    }

    int footerRows = countVisualRows(footer, conWidth);

    int linesToFill = (conHeight - 1) - linesUsed - footerRows;
    
    if (linesToFill < 0) linesToFill = 0;

    for (int i = 0; i < linesToFill; ++i) {
        printf("\x1b[K\n");
    }

    printf("%s", footer.c_str());
    
    printf("\x1b[J");
    
    consoleUpdate(NULL);
}

std::string formatListItem(bool isSelected, const std::string& label, const std::string& value) {
    char buffer[128];
    const char* prefix = isSelected ? CONSOLE_GREEN "> " : "  ";
    const char* color = isSelected ? CONSOLE_GREEN : "";
    const char* reset = CONSOLE_RESET;
    
    if (value.empty()) {
        snprintf(buffer, sizeof(buffer), "%s%s%-60s%s", prefix, color, label.c_str(), reset);
    } else {
        snprintf(buffer, sizeof(buffer), "%s%s%-50s [%s]%s", prefix, color, label.c_str(), value.c_str(), reset);
    }
    return std::string(buffer);
}

void flushInput() {
    PadState pad; padInitializeDefault(&pad);
    padUpdate(&pad);
    svcSleepThread(150'000'000); // 0.15s delay to eat inputs
    padUpdate(&pad);
}

void showBlockingMessage(const std::string& title, const std::vector<std::string>& msg) {
    flushInput();
    PadState pad; padInitializeDefault(&pad);
    while(appletMainLoop()) {
        padUpdate(&pad);
        if(padGetButtonsDown(&pad) & HidNpadButton_A) break;
        renderFrame(title, msg, "Press (A) to Continue");
    }
}

void enterManageMode() {
    flushInput(); // Prevent ghost clicks from previous screen
    PadState pad; padInitializeDefault(&pad);
    int selectedIndex = 0;
    
    std::vector<std::string> menuOptions = {
        "Manage Game Dumps",
        "Batch Generate All Cheats",
        "About / Credits",
        "Exit App"
    };

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        if (k & HidNpadButton_Down) selectedIndex = (selectedIndex + 1) % menuOptions.size();
        if (k & HidNpadButton_Up) selectedIndex = (selectedIndex - 1 + menuOptions.size()) % menuOptions.size();

        if (k & HidNpadButton_A) {
            if (selectedIndex == 0) showDumpBrowser();
            else if (selectedIndex == 1) {
                std::vector<DumpInfo> dumps = scanForDumps();
                if (!dumps.empty()) runBatchGeneration(dumps);
                else showBlockingMessage("Batch Generate", {"No dumps found to process."});
            }
            else if (selectedIndex == 2) showAboutScreen();
            else if (selectedIndex == 3) break;
            
            flushInput(); // Flush when returning to this menu
        }
        if (k & HidNpadButton_Plus) break;

        std::string header = CONSOLE_CYAN "== UE4 Cheat Creator ==" CONSOLE_RESET "\nMain Menu\n----------------------------------------------------------------";
        std::vector<std::string> body;
        for (size_t i = 0; i < menuOptions.size(); ++i) {
            body.push_back(formatListItem((int)i == selectedIndex, menuOptions[i]));
        }
        renderFrame(header, body, "----------------------------------------------------------------\n(A) Select | (+) Exit");
    }
}

void showDumpBrowser() {
    flushInput();
    renderFrame("Scanning...", {"Please wait while we scan for game dumps..."}, "");
    
    std::vector<DumpInfo> dumps = scanForDumps();
    if (dumps.empty()) {
        showBlockingMessage(CONSOLE_RED "No Dumps Found" CONSOLE_RESET, {
            "",
            "1. Run a UE4/UE5 game.",
            "2. Open HBMenu in Applet Mode (Album).",
            "3. Run this tool to auto-scan.",
            ""
        });
        return;
    }

    PadState pad; padInitializeDefault(&pad);
    int selectedIndex = 0;
    int scrollOffset = 0;
    int viewportRows = 30;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        u64 h = padGetButtons(&pad);

        if ((k & HidNpadButton_Down) || (h & HidNpadButton_Down && (h & HidNpadButton_R))) {
            if (selectedIndex < (int)dumps.size() - 1) {
                selectedIndex++;
                if (selectedIndex >= scrollOffset + viewportRows) scrollOffset++;
            }
            if(h & HidNpadButton_R) svcSleepThread(50'000'000);
        }
        if ((k & HidNpadButton_Up) || (h & HidNpadButton_Up && (h & HidNpadButton_R))) {
            if (selectedIndex > 0) {
                selectedIndex--;
                if (selectedIndex < scrollOffset) scrollOffset--;
            }
            if(h & HidNpadButton_R) svcSleepThread(50'000'000);
        }

        if (k & HidNpadButton_A) {
            enterGameDashboard(dumps[selectedIndex]);
            
            // Refresh logic
            dumps = scanForDumps();
            if (dumps.empty()) return;
            if (selectedIndex >= (int)dumps.size()) selectedIndex = dumps.size() - 1;
            flushInput();
        }
        if (k & HidNpadButton_B) return;

        std::string header = CONSOLE_CYAN "== Game Dump Browser ==" CONSOLE_RESET "\nFound: " + std::to_string(dumps.size()) + " dumps\n----------------------------------------------------------------";
        std::vector<std::string> body;
        int endItem = std::min((int)dumps.size(), scrollOffset + viewportRows);
        
        for (int i = scrollOffset; i < endItem; ++i) {
            body.push_back(formatListItem(i == selectedIndex, dumps[i].gameName, dumps[i].titleIdStr));
        }

        char infoBuf[512];
        if (selectedIndex < (int)dumps.size()) {
            snprintf(infoBuf, sizeof(infoBuf), 
                "----------------------------------------------------------------\n"
                "Title ID: %s\nBuild ID: %s\nEngine:   %s\n"
                "----------------------------------------------------------------\n"
                "(A) Select | (B) Back",
                dumps[selectedIndex].titleIdStr.c_str(),
                dumps[selectedIndex].buildIdStr.c_str(),
                dumps[selectedIndex].ueSdkVersion.c_str()
            );
        } else {
            snprintf(infoBuf, sizeof(infoBuf), "----------------------------------------------------------------\n(B) Back");
        }

        renderFrame(header, body, std::string(infoBuf));
    }
}

void enterGameDashboard(const DumpInfo& dump) {
    flushInput();
    PadState pad; padInitializeDefault(&pad);
    int selectedIndex = 0;

    std::string cheatPath = "sdmc:/atmosphere/contents/" + dump.titleIdStr + "/cheats/" + dump.buildIdStr + ".txt";
    std::string cheatPathOff = cheatPath + ".off";

    while (appletMainLoop()) {
        bool isEnabled = false;
        bool exists = false;
        struct stat s;
        if (stat(cheatPath.c_str(), &s) == 0) { isEnabled = true; exists = true; }
        else if (stat(cheatPathOff.c_str(), &s) == 0) { isEnabled = false; exists = true; }

        std::vector<UIMenuItem> menu;
        if (exists) {
            menu.push_back({ "Cheat Status", isEnabled ? CONSOLE_GREEN "ENABLED" CONSOLE_RESET : CONSOLE_RED "DISABLED" CONSOLE_RESET });
        } else {
            menu.push_back({ "Cheat Status", CONSOLE_YELLOW "NOT INSTALLED" CONSOLE_RESET });
        }

        menu.push_back({ "View Cheat File", "" });
        menu.push_back({ "Update Cheats (Default)", "" });
        menu.push_back({ "Update Cheats (Select Preset)", "" });
        menu.push_back({ "Preset Editor (Experimental)", "" });
        menu.push_back({ "Delete Dump", "" });
        menu.push_back({ "Back", "" });

        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        if (k & HidNpadButton_Down) selectedIndex = (selectedIndex + 1) % menu.size();
        if (k & HidNpadButton_Up) selectedIndex = (selectedIndex - 1 + menu.size()) % menu.size();
        if (k & HidNpadButton_B) return;

        if (k & HidNpadButton_A) {
            if (selectedIndex == 0) { if (exists) toggleCheatFile(dump, isEnabled); }
            else if (selectedIndex == 1) {
                if (isEnabled) viewCheatFile(cheatPath);
                else if (exists) viewCheatFile(cheatPathOff);
                else showBlockingMessage("Error", {"Cheat file does not exist."});
            }
            else if (selectedIndex == 2) regenerateCheats(dump, false, "Default", MAIN_CONFIG_PATH);
            else if (selectedIndex == 3) selectPresetAndGenerate(dump);
            else if (selectedIndex == 4) createAdvancedPreset(dump);
            else if (selectedIndex == 5) {
                deleteDump(dump);
                return; 
            }
            else if (selectedIndex == 6) return;
            
            flushInput();
        }

        std::string header = CONSOLE_CYAN "== Dashboard: " + dump.gameName + " ==" CONSOLE_RESET "\n" +
                             "Build: " + dump.buildIdStr + "\n" +
                             "----------------------------------------------------------------";
        
        std::vector<std::string> body;
        for (size_t i = 0; i < menu.size(); ++i) {
            body.push_back(formatListItem((int)i == selectedIndex, menu[i].label, menu[i].value));
        }
        renderFrame(header, body, "----------------------------------------------------------------\n(A) Select | (B) Back");
    }
}

void toggleCheatFile(const DumpInfo& dump, bool& isEnabled) {
    std::string p = "sdmc:/atmosphere/contents/" + dump.titleIdStr + "/cheats/" + dump.buildIdStr + ".txt";
    std::string pO = p + ".off";
    
    if (isEnabled) {
        std::rename(p.c_str(), pO.c_str());
        isEnabled = false;
    } else {
        std::rename(pO.c_str(), p.c_str());
        isEnabled = true;
    }
}

void viewCheatFile(const std::string& path) {
    flushInput();
    std::ifstream f(path);
    if(!f.is_open()) return;
    
    std::vector<std::string> lines;
    std::string s;
    while(std::getline(f,s)) lines.push_back(s);
    f.close();
    if (lines.empty()) lines.push_back("<Empty File>");

    int offset = 0;
    int viewport = 38;
    PadState pad; padInitializeDefault(&pad);

    while(appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        u64 h = padGetButtons(&pad);

        if((k|h) & HidNpadButton_Down && offset < (int)lines.size() - viewport) offset++;
        if((k|h) & HidNpadButton_Up && offset > 0) offset--;
        if(k & HidNpadButton_B) break;

        std::string header = "Viewing: " + path + "\n----------------------------------------------------------------";
        std::vector<std::string> body;
        int end = std::min((int)lines.size(), offset + viewport);
        for(int i = offset; i < end; ++i) body.push_back(lines[i]);

        renderFrame(header, body, "----------------------------------------------------------------\n(B) Close");
        svcSleepThread(20'000'000);
    }
}

void deleteDump(const DumpInfo& dump) {
    flushInput();
    PadState pad; padInitializeDefault(&pad);
    
    while(appletMainLoop()){
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        
        if(k & HidNpadButton_A) {
            std::remove(dump.fullLogPath.c_str());
            std::string txtPath = dump.fullLogPath.substr(0, dump.fullLogPath.length()-3) + "txt";
            std::remove(txtPath.c_str());
            showBlockingMessage("Deleted", {"Dump deleted successfully."});
            break;
        }
        if(k & HidNpadButton_B) break;

        renderFrame(
            CONSOLE_RED "DELETE CONFIRMATION" CONSOLE_RESET,
            {
                "",
                "Are you sure you want to delete the dump for:",
                dump.gameName,
                "",
                "This cannot be undone."
            },
            "(A) Confirm Delete | (B) Cancel"
        );
    }
}

void regenerateCheats(const DumpInfo& dump, bool useCategories, const std::string& defaultIndicator, const std::string& configPath) {
    renderFrame("Generating...", {"Parsing log and generating cheats..."}, "");
    
    ParsedLog log_data = parseLogFile(dump.fullLogPath);
    if (log_data.empty()) {
        showBlockingMessage(CONSOLE_RED "Error" CONSOLE_RESET, {"Could not parse log file."});
        return;
    }

    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/contents", 0777);
    std::string title_dir = "sdmc:/atmosphere/contents/" + dump.titleIdStr;
    mkdir(title_dir.c_str(), 0777);
    std::string cheats_dir = title_dir + "/cheats";
    mkdir(cheats_dir.c_str(), 0777);
    
    std::string outPath = cheats_dir + "/" + dump.buildIdStr + ".txt";
    generateCheatsFromConfig(log_data, configPath, outPath, useCategories, defaultIndicator);

    showBlockingMessage(CONSOLE_GREEN "Success!" CONSOLE_RESET, {
        "Cheats generated successfully.",
        "Location:",
        outPath
    });
}

void selectPresetAndGenerate(const DumpInfo& dump) {
    flushInput();
    std::vector<std::string> presets = scanForPresets();
    if (presets.empty()) {
        showBlockingMessage("No Presets", {"No presets found in:", PRESET_DIR, "", "Use 'Preset Editor' to create one."});
        return;
    }

    PadState pad; padInitializeDefault(&pad);
    int selectedIndex = 0;
    int scrollOffset = 0;
    int viewport = 30;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        if (k & HidNpadButton_Down) {
            if (selectedIndex < (int)presets.size() - 1) {
                selectedIndex++;
                if (selectedIndex >= scrollOffset + viewport) scrollOffset++;
            }
        }
        if (k & HidNpadButton_Up) {
            if (selectedIndex > 0) {
                selectedIndex--;
                if (selectedIndex < scrollOffset) scrollOffset--;
            }
        }
        if (k & HidNpadButton_B) return;

        if (k & HidNpadButton_A) {
            std::string configPath = std::string(PRESET_DIR) + "/" + presets[selectedIndex];
            regenerateCheats(dump, false, "Default", configPath);
            return;
        }

        std::string header = "Select Preset\n----------------------------------------------------------------";
        std::vector<std::string> body;
        int end = std::min((int)presets.size(), scrollOffset + viewport);
        for(int i = scrollOffset; i < end; ++i) {
            body.push_back(formatListItem(i == selectedIndex, presets[i]));
        }
        renderFrame(header, body, "(A) Generate | (B) Back");
    }
}

void runBatchGeneration(const std::vector<DumpInfo>& dumps) {
    flushInput();
    PadState pad; padInitializeDefault(&pad);
    
    while(appletMainLoop()){
        padUpdate(&pad);
        if(padGetButtonsDown(&pad) & HidNpadButton_B) return;
        if(padGetButtonsDown(&pad) & HidNpadButton_A) break;
        
        renderFrame("Batch Generation", {"This will regenerate cheats for ALL found dumps", "using the default configuration.", "", "Are you sure?"}, "(A) Start | (B) Cancel");
    }

    std::vector<std::string> log;
    log.push_back("Processing...");
    
    for(size_t i=0; i<dumps.size(); ++i) {
        std::string msg = "[" + std::to_string(i+1) + "/" + std::to_string(dumps.size()) + "] " + dumps[i].gameName + "... ";
        
        std::string title_dir = "sdmc:/atmosphere/contents/" + dumps[i].titleIdStr;
        std::string cheats_dir = title_dir + "/cheats";
        mkdir("sdmc:/atmosphere", 0777); mkdir("sdmc:/atmosphere/contents", 0777);
        mkdir(title_dir.c_str(), 0777); mkdir(cheats_dir.c_str(), 0777);
        
        std::string out_path = cheats_dir + "/" + dumps[i].buildIdStr + ".txt";
        ParsedLog l = parseLogFile(dumps[i].fullLogPath);
        
        if(!l.empty()) {
            generateCheatsFromConfig(l, MAIN_CONFIG_PATH, out_path, false, "Default");
            msg += "OK";
        } else {
            msg += "ERR";
        }
        log.push_back(msg);
        if(log.size() > 35) log.erase(log.begin());
        renderFrame("Batch Processing", log, "");
    }

    showBlockingMessage("Batch Complete", log);
}

void showAboutScreen() {
    flushInput();
    showBlockingMessage(CONSOLE_CYAN "== ue4cheatcreator ==" CONSOLE_RESET, {
        "",
        "TUI-based cheat creator for Unreal Engine 4/5 games on Switch",
        "Author: biase-d",
        "",
        "-- Credits --",
        "Based on: UE4cfgdumper by MasaGratoR",
        ""
    });
}

std::string getGameName(u64 titleID) {
    if (titleID == 0) return "Unknown Game";
    if (g_titleNameCache.count(titleID)) return g_titleNameCache[titleID];

    std::string nameStr;
    NsApplicationControlData* controlData = new NsApplicationControlData();
    size_t controlSize = 0;

    if (R_SUCCEEDED(nsInitialize())) {
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleID, controlData, sizeof(NsApplicationControlData), &controlSize))) {
            NacpLanguageEntry* langEntry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &langEntry))) {
                if (langEntry) nameStr = std::string(langEntry->name);
            }
        }
        nsExit();
    }
    delete controlData;

    if (nameStr.empty()) {
        char titleIdHex[17]; snprintf(titleIdHex, sizeof(titleIdHex), "%016lX", titleID);
        nameStr = std::string(titleIdHex);
    }

    g_titleNameCache[titleID] = nameStr;
    return nameStr;
}

std::vector<DumpInfo> scanForDumps() {
    std::vector<DumpInfo> v;
    DIR* d = opendir("sdmc:/switch/UE4cfgdumper/");
    if(!d) return v;
    
    struct dirent* e;
    while((e=readdir(d))) {
        if(e->d_type==DT_DIR && e->d_name[0]!='.') {
            std::string tid = e->d_name;
            u64 titleId = strtoull(tid.c_str(), nullptr, 16);
            DIR* td = opendir(("sdmc:/switch/UE4cfgdumper/"+tid).c_str());
            if(td) {
                struct dirent* le;
                while((le=readdir(td))) {
                    std::string ln = le->d_name;
                    if(ln.length()>4 && ln.substr(ln.length()-4)==".log") {
                        DumpInfo i; i.titleIdStr=tid; i.buildIdStr=ln.substr(0,ln.length()-4);
                        i.fullLogPath="sdmc:/switch/UE4cfgdumper/"+tid+"/"+ln;
                        i.gameName = getGameName(titleId);
                        
                        std::ifstream lf(i.fullLogPath);
                        if(lf.is_open()) std::getline(lf, i.ueSdkVersion);
                        lf.close();
                        
                        i.ueSdkVersion.erase(std::remove(i.ueSdkVersion.begin(), i.ueSdkVersion.end(), '\n'), i.ueSdkVersion.end());
                        i.ueSdkVersion.erase(std::remove(i.ueSdkVersion.begin(), i.ueSdkVersion.end(), '\r'), i.ueSdkVersion.end());
                        v.push_back(i);
                    }
                } closedir(td);
            }
        }
    }
    closedir(d);
    std::sort(v.begin(), v.end(), [](const DumpInfo& a, const DumpInfo& b){ return a.gameName < b.gameName; });
    return v;
}

std::vector<std::string> scanForPresets() {
    std::vector<std::string> p;
    DIR* d = opendir(PRESET_DIR);
    if (!d) return p;
    struct dirent* e;
    while((e=readdir(d))) {
        std::string n=e->d_name;
        if(n.length()>5&&n.substr(n.length()-5)==".json") p.push_back(n);
    }
    closedir(d);
    std::sort(p.begin(), p.end());
    return p;
}

void runDefaultScan() {
    renderFrame("Applet Mode", {"Scanning for running game..."}, "");
    svcSleepThread(1'000'000'000);

    if (run_scan()) {
        char titleIdStr[17]; snprintf(titleIdStr, sizeof(titleIdStr), "%016lX", cheatMetadata.title_id);
        char buildIdStr[17];
        u64 bid_val = 0; memcpy(&bid_val, cheatMetadata.main_nso_build_id, sizeof(u64));
        u64 bid_swapped = __builtin_bswap64(bid_val);
        snprintf(buildIdStr, sizeof(buildIdStr), "%016lX", bid_swapped);
        
        std::string log_path = "sdmc:/switch/UE4cfgdumper/" + std::string(titleIdStr) + "/" + std::string(buildIdStr) + ".log";
        DumpInfo dump = { ue4_sdk, std::string(titleIdStr), std::string(buildIdStr), log_path };
        
        regenerateCheats(dump, false, "Default", MAIN_CONFIG_PATH);
    } else {
        showBlockingMessage(CONSOLE_RED "Scan Failed" CONSOLE_RESET, {"Could not detect UE4/UE5 game."});
    }
}