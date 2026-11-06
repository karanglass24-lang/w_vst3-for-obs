// Out-of-process VST3 probe. A crashing plugin kills this process, not OBS.
// Usage: w_vst3_scanner <hex-encoded UTF-8 path>
// Output: one "name<TAB>path" line per plugin found in the file. Exit 0 = probed OK.
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

static juce::String clean(juce::String s)
{
	return s.replaceCharacters("\t\r\n", "   ");
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;

	juce::MemoryBlock mb;
	mb.loadFromHexString(argv[1]);
	const juce::String path = juce::String::fromUTF8(static_cast<const char *>(mb.getData()), (int)mb.getSize());
	if (path.isEmpty())
		return 2;

	juce::ScopedJuceInitialiser_GUI init;
	juce::VST3PluginFormat format;
	juce::OwnedArray<juce::PluginDescription> descs;
	format.findAllTypesForFile(descs, path);

	for (auto *d : descs) {
		const juce::String line = clean(d->name) + "\t" + path + "\n";
		std::fputs(line.toRawUTF8(), stdout);
	}
	std::fflush(stdout);
	return descs.isEmpty() ? 3 : 0;
}
