#include "PluginEditor.h"

using namespace juce;

namespace
{
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

    constexpr int headerHeight   = 52;
    constexpr int controlsHeight = 150;
}

//==============================================================================
OrbitLookAndFeel::OrbitLookAndFeel()
{
    setTheme (getThemes()[0]);
}

void OrbitLookAndFeel::setTheme (const Theme& t)
{
    theme = t;
    setColour (Slider::textBoxTextColourId, t.text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, t.accent.withAlpha (0.4f));
    setColour (Slider::rotarySliderFillColourId, t.accent);
    setColour (Label::textColourId, t.dimText);
    setColour (TextEditor::textColourId, t.text);
    setColour (TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, t.accent);
    setColour (ToggleButton::textColourId, t.text);
    setColour (TextButton::textColourOffId, t.text);
    setColour (TextButton::textColourOnId, t.text);
    setColour (ComboBox::textColourId, t.text);
    setColour (ComboBox::arrowColourId, t.accent);
    setColour (PopupMenu::backgroundColourId, t.panel);
    setColour (PopupMenu::textColourId, t.text);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.3f));
    setColour (PopupMenu::highlightedTextColourId, t.text);
    setColour (TooltipWindow::backgroundColourId, t.panel);
    setColour (TooltipWindow::textColourId, t.text);
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
    g.setGradientFill (ColourGradient (theme.knobTop, c.x, c.y - radius,
                                       theme.knobBottom, c.x, c.y + radius, false));
    g.fillEllipse (Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (c));

    // Track
    Path bgArc;
    bgArc.addCentredArc (c.x, c.y, radius - thick * 0.5f, radius - thick * 0.5f, 0, startAngle, endAngle, true);
    g.setColour (theme.track);
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
    g.setColour (theme.pointer.withAlpha (0.9f));
    g.drawLine (Line<float> (in, tip), jmax (2.0f, radius * 0.07f));
}

void OrbitLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? theme.accent2.withAlpha (0.3f) : theme.track.withAlpha (highlighted ? 0.9f : 0.6f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? theme.accent2 : theme.dimText.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (theme.text);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (b.getButtonText(), r, Justification::centred);
}

void OrbitLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.track.withAlpha (down ? 1.0f : (highlighted ? 0.85f : 0.55f)));
    g.fillRoundedRectangle (r, 5.0f);
}

void OrbitLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox&)
{
    auto r = Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);
    g.setColour (theme.track.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (theme.accent.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    Path arrow;
    const float ax = r.getRight() - 16.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.0f);
    g.setColour (theme.accent);
    g.fillPath (arrow);
}

//==============================================================================
Rectangle<float> ThemePicker::swatchBounds (int index) const
{
    const float size = (float) getHeight() - 6.0f;
    const float gap  = 8.0f;
    return { 3.0f + index * (size + gap), 3.0f, size, size };
}

int ThemePicker::indexAt (Point<float> p) const
{
    for (int i = 0; i < (int) getThemes().size(); ++i)
        if (swatchBounds (i).expanded (3.0f).contains (p))
            return i;
    return -1;
}

void ThemePicker::paint (Graphics& g)
{
    const auto& themes = getThemes();
    const auto& current = themes[(size_t) selected];

    for (int i = 0; i < (int) themes.size(); ++i)
    {
        const auto& t = themes[(size_t) i];
        auto r = swatchBounds (i);

        g.setGradientFill (ColourGradient (t.accent, r.getX(), r.getY(), t.accent2, r.getRight(), r.getBottom(), false));
        g.fillEllipse (r.reduced (3.0f));
        g.setColour (t.bgTop);
        g.drawEllipse (r.reduced (3.0f), 1.5f);

        if (i == selected || i == hovered)
        {
            g.setColour (current.text.withAlpha (i == selected ? 0.95f : 0.4f));
            g.drawEllipse (r.reduced (0.5f), 1.6f);
        }
    }
}

void ThemePicker::mouseDown (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && onPick)
        onPick (i);
}

void ThemePicker::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hovered) { hovered = i; repaint(); }

    setTooltip (i >= 0 ? String (getThemes()[(size_t) i].name) + " theme" : String());
}

//==============================================================================
VocalOrbitEditor::VocalOrbitEditor (VocalOrbitProcessor& p)
    : AudioProcessorEditor (&p), processor (p), scene (p.getVisualFifo(), p.apvts, VocalOrbitProcessor::numNodes)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (scene);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        const auto& def = knobDefs[i];

        k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 18);
        addAndMakeVisible (k.slider);

        k.label.setText (def.name, dontSendNotification);
        k.label.setJustificationType (Justification::centred);
        k.label.setFont (FontOptions (11.5f, Font::bold));
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, def.id, k.slider);
    }

    knobs[0].slider.setTooltip ("Scales the existing stereo (side) content. 100% = unchanged.");
    knobs[1].slider.setTooltip ("Creates stereo width from a mono vocal using a mono-safe delayed side signal.");

    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "monoCheck", monoButton);

    // Presets
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + processor.getProgramName (i), i + 1);
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, dontSendNotification);
    presetBox.onChange = [this] { loadPreset (presetBox.getSelectedId() - 1); };
    presetBox.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (presetBox);

    const int numPresets = processor.getNumPrograms();
    prevButton.onClick = [this, numPresets] { loadPreset ((processor.getCurrentProgram() + numPresets - 1) % numPresets); };
    nextButton.onClick = [this, numPresets] { loadPreset ((processor.getCurrentProgram() + 1) % numPresets); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    // Themes
    themePicker.onPick = [this] (int i) { applyTheme (i); };
    addAndMakeVisible (themePicker);
    applyTheme (jlimit (0, (int) getThemes().size() - 1, (int) processor.apvts.state.getProperty ("theme", 0)));

    setResizable (true, true);
    setResizeLimits (760, 540, 1600, 1200);
    setSize (920, 650);
    startTimerHz (5);
}

VocalOrbitEditor::~VocalOrbitEditor()
{
    setLookAndFeel (nullptr);
}

void VocalOrbitEditor::applyTheme (int index)
{
    themeIndex = index;
    const auto& t = getThemes()[(size_t) index];
    processor.apvts.state.setProperty ("theme", index, nullptr);

    lnf.setTheme (t);
    scene.setTheme (t);
    themePicker.setSelected (index);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i].slider.setColour (Slider::rotarySliderFillColourId, knobDefs[i].hero ? t.accent : t.accent2);
        knobs[i].label.setColour (Label::textColourId, knobDefs[i].hero ? t.text : t.dimText);
    }

    sendLookAndFeelChange();
    repaint();
}

void VocalOrbitEditor::loadPreset (int index)
{
    if (! isPositiveAndBelow (index, processor.getNumPrograms()))
        return;

    processor.setCurrentProgram (index);
    presetBox.setSelectedId (index + 1, dontSendNotification);
}

void VocalOrbitEditor::timerCallback()
{
    // Keep in sync if the host (e.g. FL Studio's preset menu) changes things.
    const int preset = processor.getCurrentProgram();
    if (presetBox.getSelectedId() != preset + 1)
        presetBox.setSelectedId (preset + 1, dontSendNotification);

    const int theme = (int) processor.apvts.state.getProperty ("theme", themeIndex);
    if (theme != themeIndex && isPositiveAndBelow (theme, (int) getThemes().size()))
        applyTheme (theme);
}

void VocalOrbitEditor::paint (Graphics& g)
{
    const auto& t = lnf.getTheme();
    g.fillAll (t.bgBottom);
    g.setGradientFill (ColourGradient (t.bgTop, 0, 0, t.bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.setGradientFill (ColourGradient (t.accent, (float) header.getX(), 0, t.accent2, (float) header.getX() + 180, 0, false));
    g.drawText ("VOCALORBIT", header, Justification::centredLeft);

    g.setColour (t.dimText);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText ("PRESET", presetBox.getBounds().translated (0, -13).withHeight (12), Justification::left);
    g.drawText ("THEME", themePicker.getBounds().translated (0, -13).withHeight (12), Justification::left);

    auto controls = getLocalBounds().removeFromBottom (controlsHeight).reduced (10, 8).toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (controls, 10.0f);
}

void VocalOrbitEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (16, 0);

    // Header: [title] ... [< preset >]  [theme swatches]
    themePicker.setBounds (header.removeFromRight (4 * 30 + 3 * 8 - 18).withSizeKeepingCentre (4 * 22 + 3 * 8 + 6, 28).translated (0, 6));
    header.removeFromRight (24);
    auto presetArea = header.removeFromRight (300).withSizeKeepingCentre (300, 26).translated (0, 6);
    prevButton.setBounds (presetArea.removeFromLeft (26));
    nextButton.setBounds (presetArea.removeFromRight (26));
    presetBox.setBounds (presetArea.reduced (4, 0));

    auto controls = area.removeFromBottom (controlsHeight).reduced (18, 14);
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
