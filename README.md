# w_vst3

Minimal OBS Studio plugin that hosts a single VST3 effect as an audio filter.

Status: M3. The filter scans installed VST3 plugins (out-of-process), lets you pick one from a list, opens its editor window, and saves/restores plugin state in the OBS profile.

Install: copy `w_vst3.dll` and `w_vst3_scanner.exe` to `C:\ProgramData\obs-studio\plugins\w_vst3\`.

## Scope

- Audio filter "w_vst3 (VST3 Host)" on any OBS audio source
- Scan and select installed VST3 plugins, open the plugin editor, bypass
- Plugin state saved in the OBS profile/scene
- Windows x64 only

Out of scope: plugin chains, MIDI, sidechain, device I/O, macOS/Linux.

## Build (Windows)

Requirements: Visual Studio 2022 Build Tools (C++ workload), CMake 3.24+, Git, OBS Studio 33.x installed.

```powershell
# from an "x64 Native Tools Command Prompt for VS 2022" PowerShell
pwsh scripts/setup-libobs.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Copy `build/Release/w_vst3.dll` to `C:\Program Files\obs-studio\obs-plugins\64bit\`.

## Documentation

- [User guide (English)](docs/USAGE.md)
- [사용 설명서 (한국어)](docs/USAGE.ko.md)

## Installer

CI builds `w_vst3-<version>-windows-x64-setup.exe` with Inno Setup (`installer/w_vst3.iss`) and attaches it to GitHub releases for `v*` tags. Close OBS before installing.

## Known limitations

- Plugin latency is not compensated. A plugin that reports high latency (for example iZotope RX De-click, about 0.7 s) delays the source by that amount; OBS filters cannot report latency. Use the source's Sync Offset if needed.
- Only the first two channels are processed (mono or stereo).
- Plugins are listed from the standard VST3 folders only (`Common Files\VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`).
- Hosting is in-process: a plugin that crashes at run time takes OBS down with it. Scanning is out-of-process.

## Roadmap

- [x] M0 skeleton, JUCE message loop PoC
- [x] M1 load one fixed VST3 and process audio
- [x] M2 plugin scan (separate process), picker, editor window
- [x] M3 state save/restore, bypass, safe teardown
- [x] M4 installer script, CI workflow (not yet run on GitHub)

## License

AGPL-3.0 (see LICENSE). Built with JUCE (AGPL-3.0 option).
