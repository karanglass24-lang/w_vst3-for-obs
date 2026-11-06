#pragma once
#include <string>
#include <vector>

// Finds installed VST3 plugins by probing each file in the separate w_vst3_scanner.exe.
namespace wvst::scanner {

struct Entry {
	std::string name;
	std::string path; // UTF-8
};

void init();                   // load cache; start a scan if the cache is empty
void shutdown();               // stop and join the scan thread
void rescan_async();           // no-op while a scan is running
bool scanning();
std::vector<Entry> entries();  // sorted by name, copy

} // namespace wvst::scanner
