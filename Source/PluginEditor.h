#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class DomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DomLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hi, bool down) override;
    juce::Image rustDisc;
};

// Segmented row of chunky rusty buttons bound to a choice parameter (TYPE / CEILING / QUALITY)
class Selector : public juce::Component, public juce::SettableTooltipClient
{
public:
    Selector (juce::RangedAudioParameter& p, juce::StringArray labels, juce::StringArray subLabels = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int getIndex() const { return index; }
    std::function<void()> onChange;
private:
    int segmentAt (juce::Point<int>) const;
    juce::StringArray labels, subs;
    juce::ParameterAttachment att;
    int index = 0, hover = -1;
};

// Big old analog gain-reduction meter with a swinging needle
class VuMeter : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void resized() override { face = {}; }
    void setGr (float grDb, float dt);
    bool active = true;
private:
    void buildFace();
    juce::Image face;
    float pos = 1.0f, vel = 0.0f, glow = 0.0f;
};

// Faceted red jewel pilot lamp; burns brighter the harder she works
class PilotLamp : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    float heat = 0.0f;
};

// Read-outs of everything the automatic compressor is doing
class RulesDisplay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    float thresh = -18, ratio = 1.5f, attack = 10, release = 120, makeup = 0, discipline = 0.3f;
};

// IN / CLIP / OUT meters
class MeterPanel : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    float in = 0, out = 0, clip = 0, hold = 0, matchDb = 0;
    bool clipperOff = false, match = false;
};

// Short-term + integrated LUFS; click to reset the integrated reading
class LoudnessDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onReset) onReset(); }
    std::function<void()> onReset;
    float shortTerm = -100, integrated = -100;
};

// In-window modal dialog (message, confirm, or text entry), drawn over the editor
class DomDialog : public juce::Component
{
public:
    DomDialog (const juce::String& title, const juce::String& message, bool withTextField,
               const juce::String& initialText, juce::StringArray buttons,
               std::function<void (int buttonIndex, const juce::String& text)> onClose);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override {}
private:
    void finish (int index);
    juce::String title, message;
    juce::TextEditor field;
    bool hasField;
    juce::OwnedArray<juce::TextButton> buttons;
    std::function<void (int, const juce::String&)> onClose;
    juce::Rectangle<int> panel;
};

class DominatrixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DominatrixEditor (DominatrixProcessor&);
    ~DominatrixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    // used by the headless snapshot test
    juce::TextButton& getSaveButton() { return saveButton; }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Plate { juce::String title; juce::Rectangle<int> bounds; bool hot = false; };

    Knob& makeKnob (const juce::String& id, const juce::String& text, const juce::String& tip);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void timerCallback() override;
    void buildBackdrop();

    void refreshPresetBox();
    void stepPreset (int delta);
    void promptSave();
    void confirmDelete();
    void showDialog (const juce::String& title, const juce::String& message, bool withTextField, const juce::String& initialText,
                     juce::StringArray buttons, std::function<void (int, const juce::String&)> onResult);
    void showMessage (const juce::String& title, const juce::String& text);

    DominatrixProcessor& proc;
    DomLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *disciplineKnob = nullptr, *satKnob = nullptr, *analogKnob = nullptr, *mixKnob = nullptr, *outKnob = nullptr;
    Selector typeSel, ceilSel, qualitySel;
    juce::ToggleButton matchButton { "MATCH" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAtt;
    juce::TooltipWindow tooltips { this, 700 };
    VuMeter vu;
    PilotLamp lamp;
    RulesDisplay rules;
    MeterPanel meters;
    LoudnessDisplay lufs;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };
    juce::Array<juce::File> userFiles;
    int lastPresetVersion = -1;
    std::unique_ptr<DomDialog> dialog;
    static constexpr int kUserIdBase = 1001;

    std::vector<Plate> plates;
    juce::Rectangle<int> logoArea, badgeArea, crestArea, presetArea, stripeArea, meterArea;
    juce::Image backdrop, logoImage;
    float inMeter = 0, outMeter = 0, clipMeter = 0, clipHold = 0, grSmooth = 0, flicker = 0;
    int holdTimer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DominatrixEditor)
};
