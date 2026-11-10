#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

// Colour themes. Every colour in the UI and the 3D view comes from here.
struct Theme
{
    const char* name;
    juce::Colour bgTop, bgBottom;      // background gradient
    juce::Colour panel;                // control panel
    juce::Colour track;                // knob tracks, meter background
    juce::Colour knobTop, knobBottom;  // knob body gradient
    juce::Colour pointer;              // knob pointer, phase marker
    juce::Colour accent, accent2;      // main / secondary highlight
    juce::Colour text, dimText;
    juce::Colour grid, floor, wall;    // 3D stage
    juce::Colour dotLow, dotMid, dotHigh; // dot colours: lows -> mids -> highs
    juce::Colour warning;              // negative phase
};

inline const std::array<Theme, 4>& getThemes()
{
    using C = juce::Colour;
    static const std::array<Theme, 4> themes {{
        { "Blue",
          C (0xff0b1020), C (0xff05070e), C (0xff10172a), C (0xff1d2844),
          C (0xff1f2a48), C (0xff0b1122), C (0xfff2f6ff),
          C (0xff6ee7ff), C (0xff4f8cff), C (0xffc9d6f0), C (0xff7f93b8),
          C (0xff2a3a5c), C (0xff111a30), C (0xff0e1528),
          C (0xffffb36b), C (0xff6ee7ff), C (0xff5b6dff), C (0xffff5c7a) },

        { "Pink",
          C (0xff1c0a17), C (0xff0c040a), C (0xff261020), C (0xff3d1a35),
          C (0xff3d1a35), C (0xff160812), C (0xfffff0f7),
          C (0xffff6fb5), C (0xffffa3d1), C (0xfff6d6e8), C (0xffb98aa6),
          C (0xff4f2647), C (0xff2a1226), C (0xff22101f),
          C (0xffffc08a), C (0xffff6fb5), C (0xffc77dff), C (0xffff4d4d) },

        { "Purple",
          C (0xff140c26), C (0xff07050f), C (0xff1a1230), C (0xff2e2254),
          C (0xff2e2254), C (0xff0e0a1d), C (0xfff4efff),
          C (0xffb388ff), C (0xff7c5cff), C (0xffdcd2f5), C (0xff9a8cc4),
          C (0xff3c2e6a), C (0xff1b1438), C (0xff16102e),
          C (0xffff8fd1), C (0xffb388ff), C (0xff6ee7ff), C (0xffff5c7a) },

        { "Tan",
          C (0xfff1e7d6), C (0xffe0d0b6), C (0xffeadcc4), C (0xffd3c0a0),
          C (0xfffbf5ea), C (0xffd9c7a9), C (0xff3d2e20),
          C (0xffb0733a), C (0xff7a5536), C (0xff3d2e20), C (0xff86705a),
          C (0xffc2ad8b), C (0xffe6d7bd), C (0xffece0cb),
          C (0xffc0502a), C (0xffb0833f), C (0xff4d7a70), C (0xffc0392b) },
    }};
    return themes;
}

inline juce::Colour dotColour (const Theme& t, float z) noexcept
{
    z = juce::jlimit (0.0f, 1.0f, z);
    return z < 0.5f ? t.dotLow.interpolatedWith (t.dotMid, z * 2.0f)
                    : t.dotMid.interpolatedWith (t.dotHigh, (z - 0.5f) * 2.0f);
}
