#pragma once
#include <functional>

// Runs a JUCE MessageManager on its own thread. OBS has no JUCE message loop,
// but VST3 hosting (load, prepare, editor, delete) must happen on a message thread.
namespace wvst::juce_host {

void start();
void stop();
// Queue work onto the JUCE message thread. Safe from any thread.
void post(std::function<void()> fn);

} // namespace wvst::juce_host
