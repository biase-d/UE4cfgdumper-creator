#pragma once

#include <switch.h>
#include <vector>
#include <string>
#include "dmntcht.h"

struct ue4Results {
	const char* iterator;
	bool isFloat = false;
	int default_value_int;
	float default_value_float;
	uint32_t offset;
	uint32_t add;
};

extern DmntCheatProcessMetadata cheatMetadata;
extern u64 mappings_count;
extern MemoryInfo* memoryInfoBuffers;
extern uint8_t utf_encoding;
extern std::vector<ue4Results> ue4_vector;
extern std::string ue4_sdk;
extern bool isUE5;

void initialize_scanner();
void deinitialize_scanner();
bool run_scan();