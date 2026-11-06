// Hosts one VST3 as an OBS audio filter: load, process, editor window, state save/restore.
#include <obs-module.h>
#include <media-io/audio-io.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "juce-host.h"
#include "scanner.h"
#include "updater.h"

namespace {

constexpr int kMaxBlock = 4096;

const char *kDefaultPath = "C:\\Program Files\\Common Files\\VST3\\iZotope\\RX 11 Voice De-noise.vst3";

// ---- Message-thread objects ------------------------------------------------

class EditorWindow : public juce::DocumentWindow {
public:
	EditorWindow(juce::AudioProcessorEditor *editor, const juce::String &title, std::function<void()> onClose)
		: juce::DocumentWindow(title, juce::Colours::darkgrey,
				       juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
		  closeCb(std::move(onClose))
	{
		setUsingNativeTitleBar(true);
		setContentOwned(editor, true); // window sizes itself to the editor
		setResizable(editor->isResizable(), false);
		centreWithSize(getWidth(), getHeight());
		setAlwaysOnTop(true); // otherwise it opens behind the OBS windows
		setVisible(true);
		toFront(true);
	}

	void closeButtonPressed() override { closeCb(); }

private:
	std::function<void()> closeCb;
};

struct Shared;

class StatePoller : private juce::Timer {
public:
	explicit StatePoller(Shared &s) : owner(s) { startTimer(2000); }
	~StatePoller() override { stopTimer(); }

private:
	void timerCallback() override;
	Shared &owner;
};

// State shared between the OBS filter, the audio thread and the JUCE message thread.
struct Shared {
	std::atomic<juce::AudioPluginInstance *> plugin{nullptr};
	std::atomic<bool> busy{false}; // audio thread is inside processBlock
	std::atomic<bool> dead{false}; // filter destroyed
	std::atomic<bool> bypass{false};
	int channels = 2;

	std::mutex stateMutex; // guards stateCache (message thread writes, OBS thread reads)
	std::string stateCache;

	// Message thread only:
	std::unique_ptr<EditorWindow> window;
	std::unique_ptr<StatePoller> poller;

	void refreshState()
	{
		auto *p = plugin.load();
		if (!p)
			return;
		juce::MemoryBlock mb;
		p->getStateInformation(mb);
		std::string b64 = mb.toBase64Encoding().toStdString();
		std::lock_guard<std::mutex> lock(stateMutex);
		stateCache = std::move(b64);
	}

	// Replaces the active plugin and deletes the old one. Message thread only.
	void install(juce::AudioPluginInstance *next)
	{
		refreshState();
		window.reset(); // editor must die before its processor
		poller.reset();
		auto *old = plugin.exchange(next);
		while (busy.load())
			std::this_thread::yield();
		if (old) {
			old->releaseResources();
			delete old;
		}
		if (next) {
			refreshState();
			poller = std::make_unique<StatePoller>(*this);
		}
	}

	void openEditor(const std::shared_ptr<Shared> &self)
	{
		if (dead)
			return;
		if (window) {
			window->setVisible(true);
			window->toFront(true);
			return;
		}
		auto *p = plugin.load();
		if (!p || !p->hasEditor()) {
			blog(LOG_INFO, "[w_vst3] no plugin loaded or plugin has no editor");
			return;
		}
		auto *editor = p->createEditorIfNeeded();
		if (!editor)
			return;
		window = std::make_unique<EditorWindow>(editor, p->getName(), [self] {
			wvst::juce_host::post([self] {
				self->refreshState();
				self->window.reset();
			});
		});
		blog(LOG_INFO, "[w_vst3] editor window created %dx%d visible=%d onDesktop=%d", window->getWidth(),
		     window->getHeight(), (int)window->isVisible(), (int)window->isOnDesktop());
	}
};

void StatePoller::timerCallback()
{
	owner.refreshState();
}

struct FilterData {
	obs_source_t *context = nullptr;
	std::shared_ptr<Shared> shared = std::make_shared<Shared>();
	std::string loadedPath;
};

void load_plugin(std::shared_ptr<Shared> shared, std::string path, std::string state, double sampleRate,
		 int channels)
{
	wvst::juce_host::post([shared, path, state, sampleRate, channels] {
		if (shared->dead)
			return;

		juce::VST3PluginFormat format;
		juce::OwnedArray<juce::PluginDescription> descs;
		format.findAllTypesForFile(descs, juce::String::fromUTF8(path.c_str()));
		if (descs.isEmpty()) {
			blog(LOG_WARNING, "[w_vst3] no VST3 found at: %s", path.c_str());
			return;
		}

		juce::String error;
		std::unique_ptr<juce::AudioPluginInstance> inst(
			format.createInstanceFromDescription(*descs[0], sampleRate, kMaxBlock, error));
		if (!inst) {
			blog(LOG_WARNING, "[w_vst3] load failed: %s", error.toRawUTF8());
			return;
		}

		const int n = channels >= 2 ? 2 : 1;
		const auto set = n == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
		juce::AudioProcessor::BusesLayout layout;
		layout.inputBuses.add(set);
		layout.outputBuses.add(set);
		if (!inst->setBusesLayout(layout))
			blog(LOG_WARNING, "[w_vst3] %s rejected %d-ch layout, using its default", descs[0]->name.toRawUTF8(), n);

		inst->setNonRealtime(false);
		inst->prepareToPlay(sampleRate, kMaxBlock);

		if (!state.empty()) {
			juce::MemoryBlock mb;
			if (mb.fromBase64Encoding(juce::String(state))) {
				inst->setStateInformation(mb.getData(), (int)mb.getSize());
				blog(LOG_INFO, "[w_vst3] restored state (%d bytes)", (int)mb.getSize());
			} else {
				blog(LOG_WARNING, "[w_vst3] stored state is not valid base64, ignored");
			}
		}

		blog(LOG_INFO, "[w_vst3] loaded '%s' in=%d out=%d latency=%d samples editor=%s", descs[0]->name.toRawUTF8(),
		     inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), inst->getLatencySamples(),
		     inst->hasEditor() ? "yes" : "no");

		if (shared->dead) {
			inst->releaseResources();
			return; // unique_ptr deletes on message thread
		}
		shared->install(inst.release());
	});
}

void request_load(FilterData *d, const std::string &path, const std::string &state)
{
	if (path.empty() || path == d->loadedPath)
		return;
	d->loadedPath = path;
	audio_t *audio = obs_get_audio();
	const double sr = audio ? audio_output_get_sample_rate(audio) : 48000.0;
	const int ch = audio ? (int)audio_output_get_channels(audio) : 2;
	d->shared->channels = ch;
	load_plugin(d->shared, path, state, sr, ch);
}

// ---- OBS callbacks ---------------------------------------------------------

const char *filter_name(void *)
{
	return "w_vst3 (VST3 Host)";
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new FilterData();
	d->context = source;
	d->shared->bypass = obs_data_get_bool(settings, "bypass");
	request_load(d, obs_data_get_string(settings, "plugin_path"), obs_data_get_string(settings, "state"));
	return d;
}

void filter_destroy(void *data)
{
	auto *d = static_cast<FilterData *>(data);
	auto shared = d->shared;
	shared->dead = true;
	wvst::juce_host::post([shared] { shared->install(nullptr); });
	delete d;
}

void filter_update(void *data, obs_data_t *settings)
{
	auto *d = static_cast<FilterData *>(data);
	d->shared->bypass = obs_data_get_bool(settings, "bypass");
	const std::string path = obs_data_get_string(settings, "plugin_path");
	if (path != d->loadedPath && !d->loadedPath.empty()) {
		// Different plugin: the stored state belongs to the old one.
		obs_data_set_string(settings, "state", "");
		{
			std::lock_guard<std::mutex> lock(d->shared->stateMutex);
			d->shared->stateCache.clear();
		}
		request_load(d, path, "");
	} else {
		request_load(d, path, obs_data_get_string(settings, "state"));
	}
}

void filter_save(void *data, obs_data_t *settings)
{
	auto *d = static_cast<FilterData *>(data);
	auto shared = d->shared;

	// Ask the message thread for a fresh snapshot; don't wait long if it is busy loading.
	auto done = std::make_shared<std::promise<void>>();
	auto fut = done->get_future();
	wvst::juce_host::post([shared, done] {
		shared->refreshState();
		done->set_value();
	});
	fut.wait_for(std::chrono::milliseconds(300));

	std::lock_guard<std::mutex> lock(shared->stateMutex);
	if (!shared->stateCache.empty())
		obs_data_set_string(settings, "state", shared->stateCache.c_str());
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "bypass", false);
	const char *env = std::getenv("W_VST3_PATH");
	obs_data_set_default_string(settings, "plugin_path", env && *env ? env : kDefaultPath);
}

bool rescan_clicked(obs_properties_t *, obs_property_t *, void *)
{
	wvst::scanner::rescan_async();
	return false;
}

bool update_clicked(obs_properties_t *, obs_property_t *, void *)
{
	wvst::updater::start_download();
	return true; // refresh the properties view so the new status shows
}

// A disabled button is used as a read-only status line.
void add_status_line(obs_properties_t *props, const std::string &text)
{
	obs_property_t *p = obs_properties_add_button(props, "update_status", text.c_str(), nullptr);
	obs_property_set_enabled(p, false);
}

void add_update_ui(obs_properties_t *props)
{
	using State = wvst::updater::Status::State;
	const auto st = wvst::updater::status();
	const std::string cur = wvst::updater::current_version();
	switch (st.state) {
	case State::Available:
		add_status_line(props, "Update available: v" + st.latest + " (current v" + cur + ")");
		obs_properties_add_button(props, "update_download", "Download update (installs when OBS closes)",
					  update_clicked);
		break;
	case State::Downloading:
		add_status_line(props, "Downloading update v" + st.latest + "... reopen this window to check");
		break;
	case State::Ready:
		add_status_line(props, "Update v" + st.latest + " is ready. It installs when OBS closes.");
		break;
	case State::Failed:
		add_status_line(props, "Update failed: " + st.message);
		obs_properties_add_button(props, "update_download", "Retry download", update_clicked);
		break;
	default:
		break;
	}
}

bool open_editor_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *d = static_cast<FilterData *>(data);
	auto shared = d->shared;
	blog(LOG_INFO, "[w_vst3] open editor clicked");
	wvst::juce_host::post([shared] {
		blog(LOG_INFO, "[w_vst3] openEditor on message thread");
		shared->openEditor(shared);
	});
	return false;
}

obs_properties_t *filter_properties(void *data)
{
	auto *d = static_cast<FilterData *>(data);
	obs_properties_t *props = obs_properties_create();

	add_update_ui(props);

	obs_property_t *list = obs_properties_add_list(props, "plugin_path", "VST3 plugin", OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	bool currentListed = false;
	for (auto &e : wvst::scanner::entries()) {
		obs_property_list_add_string(list, e.name.c_str(), e.path.c_str());
		if (d && e.path == d->loadedPath)
			currentListed = true;
	}
	if (d && !d->loadedPath.empty() && !currentListed) {
		const auto name = juce::File(juce::String::fromUTF8(d->loadedPath.c_str())).getFileNameWithoutExtension();
		obs_property_list_insert_string(list, 0, name.toRawUTF8(), d->loadedPath.c_str());
	}

	obs_properties_add_button(props, "rescan", "Rescan plugins (reopen this window to see results)",
				  rescan_clicked);
	obs_properties_add_button(props, "open_editor", "Open plugin editor", open_editor_clicked);
	obs_properties_add_bool(props, "bypass", "Bypass");
	return props;
}

// Runs on the OBS audio thread: no allocation, no locks.
struct obs_audio_data *filter_audio(void *data, struct obs_audio_data *audio)
{
	auto *d = static_cast<FilterData *>(data);
	Shared &s = *d->shared;
	if (s.bypass.load(std::memory_order_relaxed))
		return audio;

	s.busy.store(true);
	if (auto *p = s.plugin.load()) {
		juce::ScopedNoDenormals noDenormals;
		const int n = s.channels >= 2 ? 2 : 1;
		float *chans[2] = {reinterpret_cast<float *>(audio->data[0]),
				   n > 1 ? reinterpret_cast<float *>(audio->data[1]) : nullptr};
		juce::MidiBuffer midi; // empty; no allocation when unused

		for (int offset = 0; offset < (int)audio->frames; offset += kMaxBlock) {
			const int num = juce::jmin(kMaxBlock, (int)audio->frames - offset);
			float *ptrs[2] = {chans[0] + offset, n > 1 ? chans[1] + offset : nullptr};
			juce::AudioBuffer<float> buf(ptrs, n, num);
			p->processBlock(buf, midi);
		}
	}
	s.busy.store(false);
	return audio;
}

} // namespace

struct obs_source_info w_vst3_filter_info = [] {
	obs_source_info i = {};
	i.id = "w_vst3_filter";
	i.type = OBS_SOURCE_TYPE_FILTER;
	i.output_flags = OBS_SOURCE_AUDIO;
	i.get_name = filter_name;
	i.create = filter_create;
	i.destroy = filter_destroy;
	i.update = filter_update;
	i.save = filter_save;
	i.get_defaults = filter_defaults;
	i.get_properties = filter_properties;
	i.filter_audio = filter_audio;
	return i;
}();
