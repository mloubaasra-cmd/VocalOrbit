#include "Scene3D.h"

using namespace juce;

namespace Palette
{
    const Colour bgTop    { 0xff0b1020 };
    const Colour bgBottom { 0xff05070e };
    const Colour grid     { 0xff2a3a5c };
    const Colour text     { 0xff8fa6cc };
    const Colour accent   { 0xff6ee7ff };
    const Colour accent2  { 0xffb388ff };
}

Scene3D::Scene3D (StereoSampleFifo& f) : fifo (f)
{
    histL.assign (fftSize, 0.0f);
    histR.assign (fftSize, 0.0f);
    pullL.assign (8192, 0.0f);
    pullR.assign (8192, 0.0f);
    fftL.assign (2 * fftSize, 0.0f);
    fftR.assign (2 * fftSize, 0.0f);

    bandDb.fill (floorDb);
    bandPan.fill (0.0f);
    bandSpread.fill (0.0f);
    dots.reserve (maxDots + 512);

    setOpaque (true);
    lastTime = Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

Scene3D::~Scene3D() { stopTimer(); }

//==============================================================================
float Scene3D::freqToZ (float hz) noexcept
{
    return jlimit (0.0f, 1.0f, std::log (hz / minHz) / std::log (maxHz / minHz));
}

float Scene3D::dbToY (float db) noexcept
{
    return jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
}

void Scene3D::rebuildBands (double sampleRate)
{
    bandsSampleRate = sampleRate;
    bandLo.assign (numBands, 1);
    bandHi.assign (numBands, 1);

    const double binHz  = sampleRate / fftSize;
    const int    maxBin = fftSize / 2 - 1;
    const double ratio  = (double) maxHz / minHz;

    for (int b = 0; b < numBands; ++b)
    {
        const double fLo = minHz * std::pow (ratio, (double) b / numBands);
        const double fHi = minHz * std::pow (ratio, (double) (b + 1) / numBands);
        const int lo = jlimit (1, maxBin, (int) std::floor (fLo / binHz));
        const int hi = jlimit (lo, maxBin, (int) std::ceil (fHi / binHz) - 1);
        bandLo[(size_t) b] = lo;
        bandHi[(size_t) b] = hi;
    }
}

//==============================================================================
bool Scene3D::pullAudio()
{
    int total = 0;
    double sLR = 0, sLL = 0, sRR = 0;

    for (;;)
    {
        const int got = fifo.pull (pullL.data(), pullR.data(), (int) pullL.size());
        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i)
        {
            sLR += pullL[(size_t) i] * pullR[(size_t) i];
            sLL += pullL[(size_t) i] * pullL[(size_t) i];
            sRR += pullR[(size_t) i] * pullR[(size_t) i];
        }

        if (got >= fftSize)
        {
            std::copy (pullL.begin() + (got - fftSize), pullL.begin() + got, histL.begin());
            std::copy (pullR.begin() + (got - fftSize), pullR.begin() + got, histR.begin());
        }
        else
        {
            std::move (histL.begin() + got, histL.end(), histL.begin());
            std::move (histR.begin() + got, histR.end(), histR.begin());
            std::copy (pullL.begin(), pullL.begin() + got, histL.end() - got);
            std::copy (pullR.begin(), pullR.begin() + got, histR.end() - got);
        }
        total += got;
    }

    if (total > 0 && sLL > 1.0e-9 && sRR > 1.0e-9)
    {
        const float c = (float) (sLR / std::sqrt (sLL * sRR));
        correlation += 0.25f * (c - correlation);
    }

    return total > 0;
}

void Scene3D::analyse()
{
    const double sr = fifo.sampleRate.load();
    if (std::abs (sr - bandsSampleRate) > 0.5)
        rebuildBands (sr);

    std::fill (fftL.begin(), fftL.end(), 0.0f);
    std::fill (fftR.begin(), fftR.end(), 0.0f);
    std::copy (histL.begin(), histL.end(), fftL.begin());
    std::copy (histR.begin(), histR.end(), fftR.begin());
    window.multiplyWithWindowingTable (fftL.data(), (size_t) fftSize);
    window.multiplyWithWindowingTable (fftR.data(), (size_t) fftSize);
    fft.performRealOnlyForwardTransform (fftL.data(), true);
    fft.performRealOnlyForwardTransform (fftR.data(), true);

    const float norm = fftSize * 0.25f; // Hann window, one-sided spectrum -> sine peak amplitude

    for (int b = 0; b < numBands; ++b)
    {
        double pL = 0, pR = 0, pM = 0, pS = 0;

        for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k)
        {
            const float lr = fftL[(size_t) (2 * k)], li = fftL[(size_t) (2 * k + 1)];
            const float rr = fftR[(size_t) (2 * k)], ri = fftR[(size_t) (2 * k + 1)];
            pL += lr * lr + li * li;
            pR += rr * rr + ri * ri;
            const float mr = 0.5f * (lr + rr), mi = 0.5f * (li + ri);
            const float sr2 = 0.5f * (lr - rr), si = 0.5f * (li - ri);
            pM += mr * mr + mi * mi;
            pS += sr2 * sr2 + si * si;
        }

        const float aL = (float) std::sqrt (pL), aR = (float) std::sqrt (pR);
        const float aM = (float) std::sqrt (pM), aS = (float) std::sqrt (pS);

        const float amp = std::sqrt (0.5f * (aL * aL + aR * aR)) / norm;
        const float db  = Decibels::gainToDecibels (amp, floorDb - 20.0f);

        const float pan    = (aR - aL) / (aR + aL + 1.0e-9f);   // -1 = left, +1 = right
        const float spread = aS / (aM + aS + 1.0e-9f);          // 0 = mono, 0.5 = wide, 1 = out of phase

        auto& d = bandDb[(size_t) b];
        d += (db > d ? 0.6f : 0.18f) * (db - d);
        bandPan[(size_t) b]    += 0.3f * (pan - bandPan[(size_t) b]);
        bandSpread[(size_t) b] += 0.3f * (spread - bandSpread[(size_t) b]);
    }
}

void Scene3D::updateDots (float dt, bool hasNewAudio)
{
    // Age and remove
    const float fade = dt / 0.75f;
    for (auto& d : dots)
    {
        d.life -= fade;
        d.p.y  -= dt * 0.04f; // gentle settle
    }
    dots.erase (std::remove_if (dots.begin(), dots.end(), [] (const Dot& d) { return d.life <= 0.0f; }), dots.end());

    if (! hasNewAudio)
    {
        for (auto& d : bandDb) d += 0.15f * (floorDb - d); // fall back to silence
        return;
    }

    // Spawn new dots for each audible band
    for (int b = 0; b < numBands; ++b)
    {
        const float y = dbToY (bandDb[(size_t) b]);
        if (y < 0.06f)
            continue;

        const float spread = bandSpread[(size_t) b];
        const int count = 1 + (int) (spread * 3.0f + y * 1.5f);

        for (int i = 0; i < count; ++i)
        {
            const float jitter = (rng.nextFloat() * 2.0f - 1.0f);
            const float x = jlimit (-1.0f, 1.0f, bandPan[(size_t) b] + jitter * spread * 1.25f
                                                   + (rng.nextFloat() - 0.5f) * 0.03f);
            const float z = ((float) b + rng.nextFloat()) / (float) numBands;

            dots.push_back ({ { x, y * (0.9f + 0.1f * rng.nextFloat()), z },
                              1.0f,
                              1.6f + 3.2f * y,
                              0.08f + 0.62f * z });
        }
    }

    if ((int) dots.size() > maxDots)
        dots.erase (dots.begin(), dots.begin() + ((int) dots.size() - maxDots));
}

void Scene3D::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = jlimit (0.001f, 0.1f, (float) ((now - lastTime) * 0.001));
    lastTime = now;

    const bool hasNew = pullAudio();
    if (hasNew)
        analyse();

    updateDots (dt, hasNew);
    repaint();
}

//==============================================================================
Scene3D::Projected Scene3D::project (Vec3 w) const noexcept
{
    // World: x [-1,1], y [0,1], z [0,1] -> centred cube
    const float px = w.x;
    const float py = w.y * 0.9f - 0.42f;
    const float pz = w.z * 2.0f - 1.0f;

    const float cy = std::cos (yaw),   sy = std::sin (yaw);
    const float x1 =  px * cy + pz * sy;
    const float z1 = -px * sy + pz * cy;

    const float cp = std::cos (pitch), sp = std::sin (pitch);
    const float y2 =  py * cp + z1 * sp;
    const float z2 = -py * sp + z1 * cp;

    const float depth = jmax (0.2f, z2 + camDist);
    const float f = viewScale * 2.6f / depth;

    return { { viewCentre.x + x1 * f, viewCentre.y - y2 * f }, depth, f / viewScale };
}

void Scene3D::resized()
{
    auto b = getLocalBounds().toFloat();
    viewArea   = b.withTrimmedBottom (34.0f);
    viewCentre = viewArea.getCentre().translated (0.0f, -viewArea.getHeight() * 0.02f);
    viewScale  = jmin (viewArea.getWidth(), viewArea.getHeight() * 1.5f) * 0.36f;
}

//==============================================================================
void Scene3D::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (Palette::bgTop, 0, 0, Palette::bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    drawStage (g);
    drawSpine (g);
    drawDots (g);
    drawCorrelation (g, getLocalBounds().toFloat().removeFromBottom (34.0f).reduced (16.0f, 8.0f));

    g.setColour (Palette::text.withAlpha (0.55f));
    g.setFont (FontOptions (11.0f));
    g.drawText ("drag to orbit  |  wheel to zoom  |  double-click to reset",
                getLocalBounds().reduced (12, 8), Justification::topRight);
    g.setColour (Palette::text);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText ("STEREO SPECTRUM 3D", getLocalBounds().reduced (12, 8), Justification::topLeft);
}

void Scene3D::drawStage (Graphics& g)
{
    // Floor plane
    Path floor;
    floor.startNewSubPath (proj (-1, 0, 0));
    floor.lineTo (proj (1, 0, 0));
    floor.lineTo (proj (1, 0, 1));
    floor.lineTo (proj (-1, 0, 1));
    floor.closeSubPath();
    g.setColour (Colour (0xff111a30).withAlpha (0.85f));
    g.fillPath (floor);

    // Back wall (dB grid)
    Path wall;
    wall.startNewSubPath (proj (-1, 0, 1));
    wall.lineTo (proj (1, 0, 1));
    wall.lineTo (proj (1, 1, 1));
    wall.lineTo (proj (-1, 1, 1));
    wall.closeSubPath();
    g.setColour (Colour (0xff0e1528).withAlpha (0.6f));
    g.fillPath (wall);

    g.setFont (FontOptions (10.0f));

    for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
    {
        const float y = dbToY (db);
        g.setColour (Palette::grid.withAlpha (0.45f));
        g.drawLine (Line<float> (proj (-1, y, 1), proj (1, y, 1)), 0.8f);
        g.setColour (Palette::text.withAlpha (0.6f));
        const auto p = proj (1.04f, y, 1);
        g.drawText (String ((int) db) + " dB", Rectangle<float> (p.x + 2, p.y - 6, 44, 12), Justification::left);
    }

    // Stereo position lines (front to back)
    for (float x : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        const bool centre = std::abs (x) < 0.01f;
        g.setColour (centre ? Palette::accent.withAlpha (0.35f) : Palette::grid.withAlpha (0.7f));
        g.drawLine (Line<float> (proj (x, 0, 0), proj (x, 0, 1)), centre ? 1.2f : 0.8f);
    }

    // Frequency lines (left to right)
    struct FL { float hz; const char* name; };
    for (auto fl : { FL { 100, "100" }, FL { 250, "250" }, FL { 500, "500" }, FL { 1000, "1k" },
                     FL { 2000, "2k" }, FL { 4000, "4k" }, FL { 8000, "8k" }, FL { 16000, "16k" } })
    {
        const float z = freqToZ (fl.hz);
        g.setColour (Palette::grid.withAlpha (0.55f));
        g.drawLine (Line<float> (proj (-1, 0, z), proj (1, 0, z)), 0.7f);
        g.setColour (Palette::text.withAlpha (0.75f));
        const auto p = proj (1.08f, 0, z);
        g.drawText (fl.name, Rectangle<float> (p.x - 2, p.y - 6, 34, 12), Justification::left);
    }

    // L / C / R labels at the front edge
    g.setFont (FontOptions (14.0f, Font::bold));
    auto label = [&] (float x, const char* t, Colour c)
    {
        const auto p = proj (x, 0, -0.07f);
        g.setColour (c);
        g.drawText (t, Rectangle<float> (p.x - 20, p.y - 8, 40, 16), Justification::centred);
    };
    label (-1.0f, "L", Palette::text);
    label ( 0.0f, "C", Palette::accent.withAlpha (0.8f));
    label ( 1.0f, "R", Palette::text);
}

void Scene3D::drawSpine (Graphics& g)
{
    // A line through each band's level at its stereo centre of gravity.
    Path spine;
    bool started = false;

    for (int b = 0; b < numBands; ++b)
    {
        const float y = dbToY (bandDb[(size_t) b]);
        const auto p = proj (bandPan[(size_t) b], y, ((float) b + 0.5f) / numBands);
        if (! started) { spine.startNewSubPath (p); started = true; }
        else            spine.lineTo (p);
    }

    g.setColour (Palette::accent.withAlpha (0.12f));
    g.strokePath (spine, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Palette::accent.withAlpha (0.55f));
    g.strokePath (spine, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
}

void Scene3D::drawDots (Graphics& g)
{
    struct Item { Point<float> pt; float depth, r; Colour c; float shadowY; };
    std::vector<Item> items;
    items.reserve (dots.size());

    for (const auto& d : dots)
    {
        const auto pr = project (d.p);
        const float r = d.size * pr.scale * (0.4f + 0.6f * d.life);
        const auto c = Colour::fromHSV (d.hue, 0.65f, 1.0f, jlimit (0.0f, 1.0f, d.life * 0.9f));
        items.push_back ({ pr.pt, pr.depth, r, c, 0.0f });
    }

    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.depth > b.depth; });

    for (const auto& it : items)
    {
        // soft glow + bright core
        g.setColour (it.c.withMultipliedAlpha (0.18f));
        g.fillEllipse (it.pt.x - it.r * 2.2f, it.pt.y - it.r * 2.2f, it.r * 4.4f, it.r * 4.4f);
        g.setColour (it.c);
        g.fillEllipse (it.pt.x - it.r, it.pt.y - it.r, it.r * 2.0f, it.r * 2.0f);
    }
}

void Scene3D::drawCorrelation (Graphics& g, Rectangle<float> area)
{
    g.setFont (FontOptions (10.0f));
    g.setColour (Palette::text);
    g.drawText ("PHASE", area.removeFromLeft (44), Justification::centredLeft);
    g.drawText (String (correlation, 2), area.removeFromRight (40), Justification::centredRight);

    auto bar = area.withSizeKeepingCentre (area.getWidth(), 6.0f);
    g.setColour (Colour (0xff1a2440));
    g.fillRoundedRectangle (bar, 3.0f);

    const float cx = bar.getCentreX();
    const float x  = bar.getX() + (correlation * 0.5f + 0.5f) * bar.getWidth();
    const auto  col = correlation < 0.0f ? Colour (0xffff5c7a) : Palette::accent;

    g.setColour (col.withAlpha (0.7f));
    g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cx, x), bar.getY(), jmax (cx, x), bar.getBottom()));
    g.setColour (Colours::white);
    g.fillRect (x - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f);

    g.setColour (Palette::text.withAlpha (0.6f));
    g.drawText ("-1", bar.translated (0, 8).withWidth (20), Justification::left);
    g.drawText ("+1", bar.translated (0, 8).withTrimmedLeft (bar.getWidth() - 20), Justification::right);
}

//==============================================================================
void Scene3D::mouseDown (const MouseEvent&)
{
    dragStartYaw = yaw;
    dragStartPitch = pitch;
}

void Scene3D::mouseDrag (const MouseEvent& e)
{
    yaw   = dragStartYaw + (float) e.getDistanceFromDragStartX() * 0.008f;
    pitch = jlimit (0.0f, 1.35f, dragStartPitch + (float) e.getDistanceFromDragStartY() * 0.006f);
}

void Scene3D::mouseDoubleClick (const MouseEvent&)
{
    yaw = -0.55f; pitch = 0.42f; camDist = 4.3f;
}

void Scene3D::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    camDist = jlimit (2.2f, 7.0f, camDist - w.deltaY * 1.5f);
}
