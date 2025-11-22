#pragma once

#include <string>
#include <vector>
#include <switch.h>
#include "common_types.hpp"
#include "cheat_generator.hpp"

struct UIMenuItem {
    std::string label;
    std::string value;
};


void renderFrame(const std::string& header, const std::vector<std::string>& bodyLines, const std::string& footer);
std::string formatListItem(bool isSelected, const std::string& label, const std::string& value = "");
void enterManageMode();
void runDefaultScan();
std::vector<DumpInfo> scanForDumps();
std::vector<std::string> scanForPresets();
ParsedLog parseLogFile(const std::string& path); 
void generateCheatsFromConfig(const ParsedLog& log, const std::string& configPath, const std::string& outPath, bool useCategories, const std::string& defaultIndicator);