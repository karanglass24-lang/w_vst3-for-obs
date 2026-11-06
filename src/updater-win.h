#pragma once
#include <string>

namespace wvst::updater {

// Starts a detached, hidden helper that waits for this process to exit and then runs the
// installer elevated (the user sees the normal UAC prompt). Returns false if it could not start.
bool launch_install_after_exit(const std::wstring &installerPath);

} // namespace wvst::updater
