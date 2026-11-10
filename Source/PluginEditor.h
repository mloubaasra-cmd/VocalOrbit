#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Scene3D.h"

class OrbitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OrbitLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class VocalOrbitEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalOrbitEditor (VocalOrbitProcessor&);
    ~VocalOrbitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    VocalOrbitProcessor& processor;
    OrbitLookAndFeel lnf;
    Scene3D scene;

    std::array<Knob, 7> knobs;
    juce::ToggleButton monoButton { "MONO CHECK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalOrbitEditor)
};
