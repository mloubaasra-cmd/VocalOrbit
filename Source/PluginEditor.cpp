#include "PluginEditor.h"

using namespace juce;

namespace
{
    const Colour bg       { 0xff080c18 };
    const Colour panel    { 0xff10172a };
    const Colour track    { 0xff1d2844 };
    const Colour accent   { 0xff6ee7ff };
    const Colour accent2  { 0xffb388ff };
    const Colour textCol  { 0xffc9d6f0 };
    const Colour dimText  { 0xff7f93b8 };

    struct KnobDef { const char* id; const char* name; bool hero; };
    const std::array<KnobDef, 7> knobDefs {{
        { "width",      "WIDTH",       true  },
        { "spread",     "SPREAD",      true  },
        { "spreadTime", "SPREAD TIME", false },
        { "bassFocus",  "BASS FOCUS",  false },
        { "pan",        "PAN",         false },
        { "mix",        "MIX",         false },
        { "output",     "OUTPUT",      false },
    }};
}

//==============================================================================
OrbitLookAndFeel::OrbitLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, textCol);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::rotarySliderFillColourId, accent);
    setColour (Label::textColourId, dimText);
    setColour (ToggleButton::textColourId, textCol);
}

void OrbitLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, Slider& s)
{
    auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = jmax (3.0f, radius * 0.14f);
    const auto  fill  = s.findColour (Slider::rotarySliderFillColourId);

    // Knob body
    g.setGradientFill (ColourGradient (Colour (0xff1f2a48), c.x, c.y - radius,
                                       Colour (0xff0b1122), c.x, c.y + radius, false));
    g.fillEllipse (Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (c));

    // Track
    Path bgArc;
    bgArc.addCentredArc (c.x, c.y, radius - thick * 0.5f, radius - thick * 0.5f, 0, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (bgArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    // Value arc (bipolar params start from the middle)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    Path valArc;
    valArc.addCentredArc (c.x, c.y, radius - thick * 0.5f, radius - thick * 0.5f, 0,
                          jmin (from, angle), jmax (from, angle), true);
    g.setColour (fill.withAlpha (0.25f));
    g.strokePath (valArc, PathStrokeType (thick * 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (fill);
    g.strokePath (valArc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    // Pointer
    const auto tip = c.getPointOnCircumference (radius * 0.62f, angle);
    const auto in  = c.getPointOnCircumference (radius * 0.25f, angle);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.drawLine (Line<float> (in, tip), jmax (2.0f, radius * 0.07f));
}

void OrbitLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent2.withAlpha (0.25f) : track.withAlpha (highlighted ? 0.9f : 0.6f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? accent2 : dimText.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? Colours::white : textCol);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (b.getButtonText(), r, Justification::centred);
}

//==============================================================================
VocalOrbitEditor::VocalOrbitEditor (VocalOrbitProcessor& p)
    : AudioProcessorEditor (&p), processor (p), scene (p.getVisualFifo())
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (scene);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        const auto& def = knobDefs[i];

        k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 18);
        k.slider.setColour (Slider::rotarySliderFillColourId, def.hero ? accent : accent2);
        addAndMakeVisible (k.slider);

        k.label.setText (def.name, dontSendNotification);
        k.label.setJustificationType (Justification::centred);
        k.label.setFont (FontOptions (11.5f, Font::bold));
        k.label.setColour (Label::textColourId, def.hero ? textCol : dimText);
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, def.id, k.slider);
    }

    knobs[0].slider.setTooltip ("Scales the existing stereo (side) content. 100% = unchanged.");
    knobs[1].slider.setTooltip ("Creates stereo width from a mono vocal using a mono-safe delayed side signal.");

    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "monoCheck", monoButton);

    setResizable (true, true);
    setResizeLimits (720, 520, 1600, 1200);
    setSize (920, 640);
}

VocalOrbitEditor::~VocalOrbitEditor()
{
    setLookAndFeel (nullptr);
}

void VocalOrbitEditor::paint (Graphics& g)
{
    g.fillAll (bg);

    auto header = getLocalBounds().removeFromTop (44).reduced (16, 0);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.setGradientFill (ColourGradient (accent, (float) header.getX(), 0, accent2, (float) header.getX() + 180, 0, false));
    g.drawText ("VOCALORBIT", header, Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (FontOptions (12.0f));
    g.drawText ("vocal widener  v1.0", header, Justification::centredRight);

    auto controls = getLocalBounds().removeFromBottom (150).reduced (10, 8).toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (controls, 10.0f);
}

void VocalOrbitEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (44);
    auto controls = area.removeFromBottom (150).reduced (18, 14);
    scene.setBounds (area.reduced (10, 0));

    auto buttonArea = controls.removeFromRight (120);
    monoButton.setBounds (buttonArea.withSizeKeepingCentre (110, 34));

    const int w = controls.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = controls.removeFromLeft (w);
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell);
    }
}
