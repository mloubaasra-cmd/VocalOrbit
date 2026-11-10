#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Scene3D.h"
#include "Themes.h"

class OrbitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OrbitLookAndFeel();
    void setTheme (const Theme&);
    const Theme& getTheme() const noexcept { return theme; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

private:
    Theme theme = getThemes()[0];
};

// Row of four round colour swatches used to pick a theme.
class ThemePicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onPick;
    void setSelected (int index) { selected = index; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

private:
    juce::Rectangle<float> swatchBounds (int index) const;
    int indexAt (juce::Point<float>) const;
    int selected = 0, hovered = -1;
};

class VocalOrbitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalOrbitEditor (VocalOrbitProcessor&);
    ~VocalOrbitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTheme (int index);
    void loadPreset (int index);

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

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    ThemePicker themePicker;
    int themeIndex = 0;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalOrbitEditor)
};
