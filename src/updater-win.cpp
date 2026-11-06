// Kept free of JUCE headers so windows.h macros do not clash.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "updater-win.h"

namespace wvst::updater {

namespace {
std::wstring ps_quote(const std::wstring &s)
{
	std::wstring out;
	for (wchar_t c : s) {
		if (c == L'\'')
			out += L'\'';
		out += c;
	}
	return out;
}
} // namespace

bool launch_install_after_exit(const std::wstring &installerPath)
{
	std::wstring cmd = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \"Wait-Process -Id " +
			   std::to_wstring(GetCurrentProcessId()) +
			   L" -ErrorAction SilentlyContinue; Start-Process -FilePath '" + ps_quote(installerPath) +
			   L"' -ArgumentList '/SILENT','/SUPPRESSMSGBOXES' -Verb RunAs\"";

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
			    nullptr, &si, &pi))
		return false;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

} // namespace wvst::updater
