#pragma once
#include <string>

// Update notifier. Checks the latest GitHub release once at startup and, only when the
// user asks, downloads the installer and verifies its SHA-256. The installer is launched
// after OBS exits (the DLL is locked while OBS runs).
namespace wvst::updater {

struct Status {
	enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
	State state = State::Idle;
	std::string latest;  // version of the newest release, e.g. "0.2.0"
	std::string message; // failure reason
};

std::string current_version();
void init();           // starts a background check unless disabled in settings.json
void shutdown();       // joins threads; schedules the install if an update is Ready
Status status();
void start_download(); // user action: download + verify, then install on OBS exit

} // namespace wvst::updater
