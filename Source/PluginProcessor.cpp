#include "PluginProcessor.h"
#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

DominatrixProcessor::DominatrixProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Dominatrix", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DominatrixProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    auto pctAttr = AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "discipline", 1 }, "Discipline", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, pctAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "saturation", 1 }, "Saturation", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f, pctAttr));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "satType", 1 }, "Saturation Type", StringArray { "Tape", "Tube", "Iron", "Console", "Whip" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "analog", 1 }, "Analog", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, pctAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pctAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "outputDb", 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "ceiling", 1 }, "Ceiling", StringArray { "-1 dB", "-6 dB", "Off" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Quality", StringArray { "2x", "4x", "8x" }, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Match (Level-Matched Listening)", false));
    return { p.begin(), p.end() };
}

dom::Params DominatrixProcessor::currentParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    dom::Params p;
    p.discipline = v ("discipline") * 0.01f;
    p.saturation = v ("saturation") * 0.01f;
    p.satType = (int) v ("satType");
    p.analog = v ("analog") * 0.01f;
    p.mix = v ("mix") * 0.01f;
    p.outputDb = v ("outputDb");
    p.ceiling = (int) v ("ceiling");
    p.quality = (int) v ("quality");
    p.match = v ("match") > 0.5f;
    return p;
}

bool DominatrixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void DominatrixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    const auto p = currentParams();
    engine.setParams (p);
    reportedLatency = engine.latencyFor (p.quality);
    setLatencySamples (reportedLatency);
}

void DominatrixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    const int outCh = juce::jmin (2, buffer.getNumChannels());
    if (inCh == 1 && outCh > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n); // mono in -> stereo out

    engine.setParams (currentParams());
    engine.process (buffer.getArrayOfWritePointers(), outCh, n);
    if (engine.getLatency() != reportedLatency)
    {
        reportedLatency = engine.getLatency();
        setLatencySamples (reportedLatency);
    }
    for (int ch = outCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
}

//==============================================================================
int DominatrixProcessor::getNumPrograms() { return (int) dom::factoryPresets().size(); }

const juce::String DominatrixProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? dom::factoryPresets()[(size_t) index].name : "";
}

void DominatrixProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != "match")       // MATCH is a listening switch, presets leave it alone
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

void DominatrixProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    currentPresetName = dom::factoryPresets()[(size_t) index].name;
    resetToDefaults();
    for (auto& [id, v] : dom::factoryPresets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    ++presetVersion;
}

juce::File DominatrixProcessor::getUserPresetFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory); // ~/Library on macOS, %APPDATA% on Windows
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Castle Danger").getChildFile ("Dominatrix").getChildFile ("Presets");
}

juce::Array<juce::File> DominatrixProcessor::getUserPresets()
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.dominatrix");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}

juce::File DominatrixProcessor::fileForPresetName (const juce::String& name)
{
    return getUserPresetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".dominatrix");
}

bool DominatrixProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) return false;
    const auto file = fileForPresetName (name);
    if (! file.getParentDirectory().createDirectory()) return false;

    juce::XmlElement xml ("DominatrixPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("pluginVersion", JucePlugin_VersionString);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == "match") continue; // never save a listening-only switch into a preset
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    if (! xml.writeTo (file)) return false;
    currentPresetName = name;
    ++presetVersion;
    return true;
}

bool DominatrixProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("DominatrixPreset")) return false;
    resetToDefaults(); // so parameters added in later versions behave sensibly
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (auto* rp = apvts.getParameter (e->getStringAttribute ("id")))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
    currentPresetName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    ++presetVersion;
    return true;
}

void DominatrixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void DominatrixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = vt.getProperty ("program", 0);
            currentPresetName = vt.getProperty ("presetName", getProgramName (currentProgram)).toString();
            apvts.replaceState (vt);
            ++presetVersion;
        }
}

juce::AudioProcessorEditor* DominatrixProcessor::createEditor() { return new DominatrixEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DominatrixProcessor(); }
