#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DomEngine.h"
#include "Presets.h"
#include "CDLink.h"

class DominatrixProcessor : public juce::AudioProcessor
{
public:
    DominatrixProcessor();
    ~DominatrixProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dominatrix"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;
    dom::Params currentParams() const;

    // Castle Danger Link: The Master can read and set Dominatrix's controls
    using Link = cdlink::PluginLink<juce::AudioProcessorValueTreeState>;
    Link& getLink() { return *link; }

    // metering, read and reset by the editor
    dom::Engine& getEngine() { return engine; }

    // ---- user presets (files shared by every project and DAW)
    static juce::File getUserPresetFolder();
    static juce::Array<juce::File> getUserPresets();
    static juce::File fileForPresetName (const juce::String& name);
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    std::atomic<int> presetVersion { 0 };

private:
    void resetToDefaults();
    std::unique_ptr<Link> link;
    dom::Engine engine;
    int currentProgram = 0, reportedLatency = -1;
    juce::String currentPresetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DominatrixProcessor)
};
