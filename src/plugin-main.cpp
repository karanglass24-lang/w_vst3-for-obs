#include <obs-module.h>

#include "juce-host.h"
#include "scanner.h"
#include "updater.h"

OBS_DECLARE_MODULE()

extern struct obs_source_info w_vst3_filter_info;

MODULE_EXPORT const char *obs_module_description(void)
{
	return "w_vst3: host a VST3 plugin as an OBS audio filter";
}

bool obs_module_load(void)
{
	wvst::juce_host::start();
	wvst::scanner::init();
	wvst::updater::init();
	obs_register_source(&w_vst3_filter_info);
	blog(LOG_INFO, "[w_vst3] loaded");
	return true;
}

void obs_module_unload(void)
{
	wvst::updater::shutdown();
	wvst::scanner::shutdown();
	wvst::juce_host::stop();
	blog(LOG_INFO, "[w_vst3] unloaded");
}
