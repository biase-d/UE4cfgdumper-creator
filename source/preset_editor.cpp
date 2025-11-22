#include "preset_editor.hpp"
#include "cheat_generator.hpp" 
#include "tui_manager.hpp"
#include <switch.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <sys/stat.h>
#include <sstream>
#include <iomanip>
#include <cmath>

enum DisplayMode {
    MODE_HEX,
    MODE_DEC,
    MODE_FLOAT
};

struct EditorCvar {
    std::string name;
    std::string type;
    std::string defaultHex; 
    std::string currentHex;
    bool isSelected;
};

struct ConfigOption {
    std::string name;
    std::map<std::string, std::string> settings; 
};


void flushInput_pe() {
    PadState pad; padInitializeDefault(&pad);
    padUpdate(&pad);
    svcSleepThread(150'000'000); // 0.15s delay
    padUpdate(&pad);
}

void showBlockingMessage_pe(const std::string& title, const std::vector<std::string>& msg) {
    flushInput_pe();
    PadState pad; padInitializeDefault(&pad);
    while(appletMainLoop()) {
        padUpdate(&pad);
        if(padGetButtonsDown(&pad) & HidNpadButton_A) break;
        renderFrame(title, msg, "Press (A) to Continue");
    }
}

std::string hexToFloatStr(const std::string& hex) {
    try {
        uint32_t x = std::stoul(hex, nullptr, 16);
        float f;
        memcpy(&f, &x, sizeof(f));
        std::string s = std::to_string(f);
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if(s.back() == '.') s.pop_back();
        return s;
    } catch(...) { return "NaN"; }
}

std::string hexToDecStr(const std::string& hex) {
    try {
        long long i = std::stoll(hex, nullptr, 16);
        return std::to_string(i);
    } catch(...) { return "Err"; }
}

std::string floatToHexStr(float val) {
    uint32_t hexVal;
    memcpy(&hexVal, &val, sizeof(val));
    char buffer[9]; snprintf(buffer, sizeof(buffer), "%08X", hexVal);
    return std::string(buffer);
}

std::string decToHexStr(long long val) {
    char buffer[16]; snprintf(buffer, sizeof(buffer), "%08llX", val);
    return std::string(buffer);
}

std::string getDisplayValue(const std::string& hexVal, DisplayMode mode) {
    if (mode == MODE_FLOAT) return hexToFloatStr(hexVal);
    if (mode == MODE_DEC) return hexToDecStr(hexVal);
    return hexVal; // MODE_HEX
}

bool getKeyboardInput(const char* guide, const char* initial, char* outBuf, size_t bufSize) {
    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial);
    bool ok = R_SUCCEEDED(swkbdShow(&kbd, outBuf, bufSize));
    swkbdClose(&kbd);
    return ok;
}

bool savePresetToFile(const std::string& filename, const std::vector<ConfigOption>& options) {
    if(options.empty()) return false;

    nlohmann::json jRoot;
    nlohmann::json catArr = nlohmann::json::array();

    for(const auto& opt : options) {
        nlohmann::json jOpt;
        jOpt["name"] = opt.name;
        nlohmann::json jSettingsArr = nlohmann::json::array();
        
        for(const auto& kv : opt.settings) {
            nlohmann::json s;
            // Create the "VAL VAL" format
            std::string valStr = kv.second + " " + kv.second;
            s[kv.first] = valStr;
            jSettingsArr.push_back(s);
        }
        jOpt["options"] = jSettingsArr;
        catArr.push_back(jOpt);
    }

    jRoot[filename] = catArr; // Use filename as category key

    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/ue4cheatcreator-proper", 0777);
    mkdir("sdmc:/config/ue4cheatcreator-proper/presets", 0777);
    
    std::string path = "sdmc:/config/ue4cheatcreator-proper/presets/" + filename + ".json";
    std::ofstream o(path);
    if(!o.is_open()) return false;
    o << jRoot.dump(4);
    o.close();
    return true;
}

std::vector<ConfigOption> loadPresetFromFile(const std::string& filename) {
    std::vector<ConfigOption> loaded;
    std::string path = "sdmc:/config/ue4cheatcreator-proper/presets/" + filename + ".json";
    std::ifstream f(path);
    if(!f.is_open()) return loaded;

    try {
        nlohmann::json j = nlohmann::json::parse(f);
        if(j.is_object() && !j.empty()) {
            auto it = j.begin();
            if(it.value().is_array()) {
                for(const auto& jOpt : it.value()) {
                    ConfigOption opt;
                    opt.name = jOpt.value("name", "Unknown Option");
                    
                    if(jOpt.contains("options") && jOpt["options"].is_array()) {
                        for(const auto& jSet : jOpt["options"]) {
                            if(jSet.is_object() && !jSet.empty()) {
                                std::string cvar = jSet.begin().key();
                                std::string valRaw = jSet.begin().value();
                                // Value is usually "HEX HEX", we just want the first HEX
                                std::string hex = valRaw.substr(0, valRaw.find(' '));
                                opt.settings[cvar] = hex;
                            }
                        }
                    }
                    loaded.push_back(opt);
                }
            }
        }
    } catch(...) {}
    return loaded;
}

bool configureOption(const std::string& optionName, std::vector<EditorCvar>& masterList, ConfigOption& outOption) {
    flushInput_pe();
    PadState pad; padInitializeDefault(&pad);
    
    int idx = 0;
    int scroll = 0;
    int maxL = 30;
    DisplayMode currentMode = MODE_HEX;

    while(appletMainLoop()) {
        padUpdate(&pad); 
        u64 k = padGetButtonsDown(&pad);
        u64 h = padGetButtons(&pad);

        // Nav
        if ((k | h) & HidNpadButton_Down) {
            if (idx < (int)masterList.size()-1) {
                idx++;
                if (idx >= scroll + maxL) scroll++;
            }
            if(h & HidNpadButton_Down) svcSleepThread(50'000'000);
        }
        if ((k | h) & HidNpadButton_Up) {
            if (idx > 0) {
                idx--;
                if (idx < scroll) scroll--;
            }
            if(h & HidNpadButton_Up) svcSleepThread(50'000'000);
        }

        // (A) Toggle selection
        if (k & HidNpadButton_A) {
            masterList[idx].isSelected = !masterList[idx].isSelected;
        }

        // (X) Cycle Display Mode
        if (k & HidNpadButton_X) {
            if (currentMode == MODE_HEX) currentMode = MODE_DEC;
            else if (currentMode == MODE_DEC) currentMode = MODE_FLOAT;
            else currentMode = MODE_HEX;
        }

        // (Y) Edit Value
        if (k & HidNpadButton_Y) {
            masterList[idx].isSelected = true;
            char buf[64];
            std::string currentValStr = getDisplayValue(masterList[idx].currentHex, currentMode);
            
            std::string guide = "Enter Value (";
            if(currentMode == MODE_FLOAT) guide += "Float)";
            else if(currentMode == MODE_DEC) guide += "Decimal)";
            else guide += "Hex)";

            if(getKeyboardInput(guide.c_str(), currentValStr.c_str(), buf, sizeof(buf))) {
                try {
                    std::string input = buf;
                    if (currentMode == MODE_FLOAT) {
                        float f = std::stof(input);
                        masterList[idx].currentHex = floatToHexStr(f);
                    } else if (currentMode == MODE_DEC) {
                        long long i = std::stoll(input);
                        masterList[idx].currentHex = decToHexStr(i);
                    } else {
                        if(input.rfind("0x", 0) == 0) input = input.substr(2);
                        masterList[idx].currentHex = input;
                    }
                } catch(...) {
                    showBlockingMessage_pe("Invalid Input", {"Could not parse value."});
                }
            }
        }

        // (+) Confirm Option
        if (k & HidNpadButton_Plus) {
            outOption.name = optionName;
            bool hasAny = false;
            for(const auto& c : masterList) {
                if(c.isSelected) {
                    outOption.settings[c.name] = c.currentHex;
                    hasAny = true;
                }
            }
            if(!hasAny) {
                showBlockingMessage_pe("No Selection", {"Please select at least one setting."});
                continue;
            }
            return true;
        }

        if (k & HidNpadButton_B) return false;

        std::string modeStr = "HEX";
        if(currentMode == MODE_DEC) modeStr = "DEC";
        if(currentMode == MODE_FLOAT) modeStr = "FLOAT";

        std::string header = std::string(CONSOLE_CYAN "== Configuring: '" + optionName + "' ==" CONSOLE_RESET "\n") +
                             "Mode: " + modeStr + " (Press X to change)\n" + 
                             "----------------------------------------------------------------";
        
        std::vector<std::string> body;
        int end = std::min((int)masterList.size(), scroll + maxL);
        
        for (int i = scroll; i < end; ++i) {
            std::string prefix = (i == idx) ? "> " : "  ";
            std::string dispName = masterList[i].name;
            if(dispName.length() > 35) dispName = dispName.substr(0, 32) + "...";
            std::string valStr = getDisplayValue(masterList[i].currentHex, currentMode);
            
            char lineBuf[128];
            if (masterList[i].isSelected) {
                snprintf(lineBuf, sizeof(lineBuf), "%s" CONSOLE_GREEN "[X] %-36s [%s]" CONSOLE_RESET, 
                    prefix.c_str(), dispName.c_str(), valStr.c_str());
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%s[ ] %-36s [%s]", 
                    prefix.c_str(), dispName.c_str(), valStr.c_str());
            }
            body.push_back(lineBuf);
        }

        renderFrame(header, body, "----------------------------------------------------------------\n(A) Toggle | (Y) Edit Value | (+) Save | (B) Cancel");
    }
    return false;
}

void runEditorLoop(const DumpInfo& dump, std::string filename, std::vector<ConfigOption> existingOptions) {
    renderFrame("Loading...", {"Parsing game log to get available settings..."}, "");
    ParsedLog log = parseLogFile(dump.fullLogPath);
    if(log.empty()) {
        showBlockingMessage_pe("Error", {"Could not parse log file."});
        return;
    }

    std::vector<EditorCvar> masterList;
    for(auto& p : log) {
        if(p.first == "engineVersion") continue;
        std::string hex = p.second.hexValue;
        if(hex.rfind("0x",0)==0) hex = hex.substr(2);
        masterList.push_back({p.first, p.second.type, hex, hex, false});
    }
    std::sort(masterList.begin(), masterList.end(), [](auto& a, auto& b){ return a.name < b.name; });

    PadState pad; padInitializeDefault(&pad);
    int idx = 0;

    while(appletMainLoop()) {
        padUpdate(&pad); 
        u64 k = padGetButtonsDown(&pad);

        int totalItems = existingOptions.size() + 1;
        
        if (k & HidNpadButton_Down) idx = (idx + 1) % totalItems;
        if (k & HidNpadButton_Up) idx = (idx - 1 + totalItems) % totalItems;

        // (A) Add New Option
        if ((k & HidNpadButton_A) && idx == (int)existingOptions.size()) {
            char optName[64] = "New Option";
            if(getKeyboardInput("Name this Option (e.g. '60 FPS')", "", optName, 64)) {
                
                // Smart Clone Logic
                int cloneIdx = -1;
                if(!existingOptions.empty()) {
                    // Ask user if they want to clone
                    flushInput_pe();
                    int cIdx = 0;
                    while(appletMainLoop()) {
                        padUpdate(&pad);
                        if(padGetButtonsDown(&pad) & HidNpadButton_B) { cloneIdx = -1; break; } // No clone
                        if(padGetButtonsDown(&pad) & HidNpadButton_A) { cloneIdx = cIdx; break; } // Clone selected
                        if(padGetButtonsDown(&pad) & HidNpadButton_Down) cIdx = (cIdx + 1) % existingOptions.size();
                        if(padGetButtonsDown(&pad) & HidNpadButton_Up) cIdx = (cIdx - 1 + existingOptions.size()) % existingOptions.size();

                        std::string h = "Clone settings from existing option?\n(A) Clone Selected | (B) Create Empty\n--------------------------------";
                        std::vector<std::string> b;
                        for(int i=0; i<(int)existingOptions.size(); ++i) {
                            b.push_back(formatListItem(i==cIdx, existingOptions[i].name));
                        }
                        renderFrame(h, b, "");
                    }
                }

                // Prepare Master List
                for(auto& c : masterList) {
                    c.isSelected = false;
                    c.currentHex = c.defaultHex; // Reset to default
                }

                // Apply Clone Data if selected
                if(cloneIdx != -1) {
                    for(auto& c : masterList) {
                        if(existingOptions[cloneIdx].settings.count(c.name)) {
                            c.isSelected = true;
                            c.currentHex = existingOptions[cloneIdx].settings.at(c.name);
                        }
                    }
                }

                ConfigOption newOpt;
                if(configureOption(optName, masterList, newOpt)) {
                    existingOptions.push_back(newOpt);
                }
            }
            flushInput_pe();
        }
        // (A) Edit Existing
        else if ((k & HidNpadButton_A)) {
            // Load existing settings into master list
            for(auto& c : masterList) {
                c.isSelected = false;
                c.currentHex = c.defaultHex;
                if(existingOptions[idx].settings.count(c.name)) {
                    c.isSelected = true;
                    c.currentHex = existingOptions[idx].settings.at(c.name);
                }
            }
            
            ConfigOption editedOpt;
            if(configureOption(existingOptions[idx].name, masterList, editedOpt)) {
                existingOptions[idx] = editedOpt;
            }
            flushInput_pe();
        }
        
        // (Y) Delete Option
        if ((k & HidNpadButton_Y) && idx < (int)existingOptions.size()) {
            existingOptions.erase(existingOptions.begin() + idx);
            if(idx >= (int)existingOptions.size() && idx > 0) idx--;
        }

        // (B) Cancel/Exit
        if (k & HidNpadButton_B) return;

        // (+) Save JSON
        if (k & HidNpadButton_Plus) {
            if(savePresetToFile(filename, existingOptions)) {
                showBlockingMessage_pe("Saved", {"Preset saved successfully."});
                return;
            } else {
                showBlockingMessage_pe("Error", {"Could not save file."});
            }
        }

        std::string header = std::string(CONSOLE_CYAN "== Editing: " + filename + ".json ==" CONSOLE_RESET "\n") +
                             "Game: " + dump.gameName + "\n" +
                             "----------------------------------------------------------------";
        
        std::vector<std::string> body;
        for(int i=0; i<(int)existingOptions.size(); ++i) {
            std::string prefix = (i == idx) ? "> " : "  ";
            char line[128];
            snprintf(line, sizeof(line), "%s%d. %s (%lu settings)", 
                prefix.c_str(), i+1, existingOptions[i].name.c_str(), existingOptions[i].settings.size());
            body.push_back(line);
        }

        std::string prefix = (idx == (int)existingOptions.size()) ? CONSOLE_GREEN "> " : "  ";
        body.push_back("");
        body.push_back(prefix + "[+ Add New Cheat Option]" + CONSOLE_RESET);

        renderFrame(header, body, "----------------------------------------------------------------\n(A) Edit/Add | (Y) Delete Option | (+) Save | (B) Cancel");
    }
}

void createAdvancedPreset(const DumpInfo& dump) {
    flushInput_pe();
    PadState pad; padInitializeDefault(&pad);
    int idx = 0;

    while(appletMainLoop()) {
        std::vector<std::string> presets = scanForPresets();
        
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        int total = presets.size() + 1;
        if (k & HidNpadButton_Down) idx = (idx + 1) % total;
        if (k & HidNpadButton_Up) idx = (idx - 1 + total) % total;
        if (k & HidNpadButton_B) return;

        // (A) Select
        if (k & HidNpadButton_A) {
            // Create New
            if (idx == 0) {
                char buf[64];
                if(getKeyboardInput("Name your Preset File (No Ext)", "MyCustomConfig", buf, 64)) {
                    std::string fname = buf;
                    runEditorLoop(dump, fname, {});
                }
            } 
            // Manage Existing
            else {
                std::string filename = presets[idx-1];
                if(filename.length() > 5) filename = filename.substr(0, filename.length()-5); // remove .json

                // Sub-Menu for Existing Preset
                flushInput_pe();
                int subIdx = 0;
                while(appletMainLoop()) {
                    padUpdate(&pad);
                    if(padGetButtonsDown(&pad) & HidNpadButton_B) break;
                    
                    if(padGetButtonsDown(&pad) & HidNpadButton_Down) subIdx = (subIdx+1)%3;
                    if(padGetButtonsDown(&pad) & HidNpadButton_Up) subIdx = (subIdx-1+3)%3;

                    if(padGetButtonsDown(&pad) & HidNpadButton_A) {
                        // Edit
                        if(subIdx == 0) {
                            std::vector<ConfigOption> opts = loadPresetFromFile(filename);
                            runEditorLoop(dump, filename, opts);
                            break;
                        }
                        // Duplicate
                        else if(subIdx == 1) {
                            char buf[64];
                            if(getKeyboardInput("New Filename", (filename + "_copy").c_str(), buf, 64)) {
                                std::vector<ConfigOption> opts = loadPresetFromFile(filename);
                                savePresetToFile(buf, opts);
                                showBlockingMessage_pe("Duplicated", {"Preset duplicated successfully."});
                                break;
                            }
                        }
                        // Delete
                        else if(subIdx == 2) {
                            std::string path = "sdmc:/config/ue4cheatcreator-proper/presets/" + filename + ".json";
                            std::remove(path.c_str());
                            showBlockingMessage_pe("Deleted", {"Preset deleted."});
                            break;
                        }
                    }

                    std::vector<std::string> ops = {"Edit Preset", "Duplicate Preset", "Delete Preset"};
                    std::vector<std::string> b;
                    for(int i=0; i<3; ++i) b.push_back(formatListItem(i==subIdx, ops[i]));
                    renderFrame("Manage: " + filename, b, "(A) Select | (B) Back");
                }
            }
            flushInput_pe();
        }

        std::string header = std::string(CONSOLE_CYAN "== Preset Manager ==" CONSOLE_RESET "\n") + 
                             "Manage presets for: " + dump.gameName + "\n" +
                             "----------------------------------------------------------------";
        std::vector<std::string> body;
        
        body.push_back(formatListItem(idx == 0, "[+ Create New Preset]"));
        body.push_back("");

        for(int i=0; i<(int)presets.size(); ++i) {
            body.push_back(formatListItem(idx == i+1, presets[i]));
        }

        renderFrame(header, body, "(A) Select | (B) Back");
    }
}