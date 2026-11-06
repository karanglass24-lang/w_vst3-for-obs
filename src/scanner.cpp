#include "scanner.h"

#include <obs-module.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace wvst::scanner {

namespace {

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::thread g_thread;
std::atomic<bool> g_scanning{false};
std::atomic<bool> g_stop{false};

constexpr int kProbeTimeoutMs = 20000;

juce::File cache_file()
{
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
		.getChildFile("obs-studio/plugin_config/w_vst3/scan-cache.json");
}

juce::File scanner_exe()
{
	// currentExecutableFile resolves to this DLL when called from inside a module.
	return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
		.getSiblingFile("w_vst3_scanner.exe");
}

void sort_entries(std::vector<Entry> &v)
{
	std::sort(v.begin(), v.end(), [](const Entry &a, const Entry &b) {
		return juce::String(a.name).compareIgnoreCase(juce::String(b.name)) < 0;
	});
}

void load_cache()
{
	auto parsed = juce::JSON::parse(cache_file());
	auto *arr = parsed["plugins"].getArray();
	if (!arr)
		return;
	std::vector<Entry> v;
	for (auto &item : *arr)
		v.push_back({item["name"].toString().toStdString(), item["path"].toString().toStdString()});
	sort_entries(v);
	std::lock_guard<std::mutex> lock(g_mutex);
	g_entries = std::move(v);
}

void save_cache(const std::vector<Entry> &v)
{
	juce::Array<juce::var> arr;
	for (auto &e : v) {
		auto *o = new juce::DynamicObject();
		o->setProperty("name", juce::String::fromUTF8(e.name.c_str()));
		o->setProperty("path", juce::String::fromUTF8(e.path.c_str()));
		arr.add(juce::var(o));
	}
	auto *root = new juce::DynamicObject();
	root->setProperty("plugins", arr);
	auto f = cache_file();
	f.getParentDirectory().createDirectory();
	f.replaceWithText(juce::JSON::toString(juce::var(root)));
}

juce::Array<juce::File> find_candidates()
{
	juce::Array<juce::File> roots;
	roots.add(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
			  .getChildFile("Common Files/VST3"));
	const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
	if (local.isNotEmpty())
		roots.add(juce::File(local).getChildFile("Programs/Common/VST3"));

	juce::Array<juce::File> out;
	for (auto &root : roots) {
		if (!root.isDirectory())
			continue;
		for (auto &f : root.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3")) {
			// skip the binary inside a bundle folder (Foo.vst3/Contents/x86_64-win/Foo.vst3)
			if (f.getParentDirectory().getFullPathName().containsIgnoreCase(".vst3"))
				continue;
			if (!out.contains(f))
				out.add(f);
		}
	}
	return out;
}

// Returns true if the probe succeeded; fills `found`.
bool probe(const juce::File &exe, const juce::File &plugin, std::vector<Entry> &found)
{
	const auto utf8 = plugin.getFullPathName().toRawUTF8();
	const auto hex = juce::String::toHexString(utf8, (int)std::strlen(utf8), 0);

	juce::ChildProcess proc;
	if (!proc.start(juce::StringArray{exe.getFullPathName(), hex}, juce::ChildProcess::wantStdOut))
		return false;

	const auto deadline = juce::Time::getMillisecondCounter() + kProbeTimeoutMs;
	while (proc.isRunning()) {
		if (g_stop || juce::Time::getMillisecondCounter() > deadline) {
			proc.kill();
			return false;
		}
		juce::Thread::sleep(30);
	}
	const auto output = proc.readAllProcessOutput();
	if (proc.getExitCode() != 0)
		return false;

	for (auto &line : juce::StringArray::fromLines(output)) {
		auto parts = juce::StringArray::fromTokens(line, "\t", "");
		if (parts.size() == 2)
			found.push_back({parts[0].toStdString(), parts[1].toStdString()});
	}
	return !found.empty();
}

void scan_worker()
{
	const auto exe = scanner_exe();
	if (!exe.existsAsFile()) {
		blog(LOG_WARNING, "[w_vst3] scanner not found: %s", exe.getFullPathName().toRawUTF8());
		g_scanning = false;
		return;
	}

	std::vector<Entry> result;
	int failed = 0;
	const auto files = find_candidates();
	blog(LOG_INFO, "[w_vst3] scanning %d VST3 file(s)", files.size());

	for (auto &f : files) {
		if (g_stop)
			break;
		std::vector<Entry> found;
		// A cold plugin can fail its first load; retry once before giving up.
		if (!probe(exe, f, found) && (g_stop || !probe(exe, f, found))) {
			++failed;
			blog(LOG_WARNING, "[w_vst3] skipped (probe failed): %s", f.getFullPathName().toRawUTF8());
			continue;
		}
		// One entry per file; use the first plugin name in it.
		result.push_back(found.front());
	}

	if (!g_stop) {
		sort_entries(result);
		save_cache(result);
		std::lock_guard<std::mutex> lock(g_mutex);
		g_entries = result;
		blog(LOG_INFO, "[w_vst3] scan done: %d ok, %d failed", (int)result.size(), failed);
	}
	g_scanning = false;
}

} // namespace

void init()
{
	load_cache();
	bool empty;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		empty = g_entries.empty();
	}
	if (empty)
		rescan_async();
}

void rescan_async()
{
	bool expected = false;
	if (!g_scanning.compare_exchange_strong(expected, true))
		return;
	if (g_thread.joinable())
		g_thread.join();
	g_stop = false;
	g_thread = std::thread(scan_worker);
}

void shutdown()
{
	g_stop = true;
	if (g_thread.joinable())
		g_thread.join();
}

bool scanning()
{
	return g_scanning;
}

std::vector<Entry> entries()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_entries;
}

} // namespace wvst::scanner
