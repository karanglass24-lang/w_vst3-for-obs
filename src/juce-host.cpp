#include "juce-host.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace wvst::juce_host {

namespace {
std::thread g_thread;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_running{false};
} // namespace

void start()
{
	if (g_thread.joinable())
		return;
	g_quit = false;
	std::promise<void> ready;
	auto readyFuture = ready.get_future();

	g_thread = std::thread([&ready] {
		// The thread that creates the MessageManager becomes the JUCE message thread.
		juce::ScopedJuceInitialiser_GUI init;
		auto *mm = juce::MessageManager::getInstance();
		g_running = true;
		ready.set_value();
		while (!g_quit)
			mm->runDispatchLoopUntil(20);
		g_running = false;
		mm->runDispatchLoopUntil(200); // flush queued deletes
	});
	readyFuture.wait();
}

void stop()
{
	if (g_thread.joinable()) {
		// Let queued plugin deletions finish before the message loop goes away.
		auto done = std::make_shared<std::promise<void>>();
		auto fut = done->get_future();
		post([done] { done->set_value(); });
		fut.wait_for(std::chrono::seconds(3));
	}
	g_quit = true;
	if (g_thread.joinable())
		g_thread.join();
}

void post(std::function<void()> fn)
{
	// After stop() the MessageManager is gone and callAsync would silently create a new one.
	if (!g_running)
		return;
	juce::MessageManager::callAsync(std::move(fn));
}

} // namespace wvst::juce_host
