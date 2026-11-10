#include "Scene3D.h"

using namespace juce;


Scene3D::Scene3D (StereoSampleFifo& f, AudioProcessorValueTreeState& state, int numNodes) : fifo (f)
{
    for (int i = 0; i < numNodes; ++i)
    {
        auto id = [i] (const char* what) { return "node" + String (i + 1) + "_" + what; };
        nodes.push_back ({ state.getParameter (id ("on")), state.getParameter (id ("freq")),
                           state.getParameter (id ("pan")), state.getParameter (id ("bw")) });
        jassert (nodes.back().on && nodes.back().freq && nodes.back().pan && nodes.back().bw);
    }

    clearButton.onClick = [this] { clearNodes(); };
    clearButton.setTooltip ("Remove all frequency nodes");
    addAndMakeVisible (clearButton);

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
                              z });
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
    clearButton.setBounds (getWidth() - 112, 6, 100, 22);
}

//==============================================================================
void Scene3D::paint (Graphics& g)
{
    g.setGradientFill (ColourGradient (theme.bgTop, 0, 0, theme.bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    drawStage (g);
    drawSpine (g);
    drawDots (g);
    drawNodes (g);
    drawNodeInfo (g);
    drawCorrelation (g, getLocalBounds().toFloat().removeFromBottom (34.0f).reduced (16.0f, 8.0f));

    g.setColour (theme.text.withAlpha (0.55f));
    g.setFont (FontOptions (11.0f));
    g.drawText ("double-click floor: add node  |  drag node: move (Shift = left/right only)  |  wheel on node: band width  |  right-click node: remove",
                getLocalBounds().withTrimmedBottom (34).reduced (12, 4), Justification::bottomLeft);
    g.drawText ("drag empty space: orbit  |  wheel: zoom  |  double-click outside floor: reset view",
                getLocalBounds().withTrimmedBottom (34).reduced (12, 4).translated (0, -14), Justification::bottomLeft);
    g.setColour (theme.text);
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
    g.setColour (theme.floor.withAlpha (0.9f));
    g.fillPath (floor);

    // Back wall (dB grid)
    Path wall;
    wall.startNewSubPath (proj (-1, 0, 1));
    wall.lineTo (proj (1, 0, 1));
    wall.lineTo (proj (1, 1, 1));
    wall.lineTo (proj (-1, 1, 1));
    wall.closeSubPath();
    g.setColour (theme.wall.withAlpha (0.7f));
    g.fillPath (wall);

    g.setFont (FontOptions (10.0f));

    for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
    {
        const float y = dbToY (db);
        g.setColour (theme.grid.withAlpha (0.45f));
        g.drawLine (Line<float> (proj (-1, y, 1), proj (1, y, 1)), 0.8f);
        g.setColour (theme.text.withAlpha (0.6f));
        const auto p = proj (1.04f, y, 1);
        g.drawText (String ((int) db) + " dB", Rectangle<float> (p.x + 2, p.y - 6, 44, 12), Justification::left);
    }

    // Stereo position lines (front to back)
    for (float x : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        const bool centre = std::abs (x) < 0.01f;
        g.setColour (centre ? theme.accent.withAlpha (0.35f) : theme.grid.withAlpha (0.7f));
        g.drawLine (Line<float> (proj (x, 0, 0), proj (x, 0, 1)), centre ? 1.2f : 0.8f);
    }

    // Frequency lines (left to right)
    struct FL { float hz; const char* name; };
    for (auto fl : { FL { 100, "100" }, FL { 250, "250" }, FL { 500, "500" }, FL { 1000, "1k" },
                     FL { 2000, "2k" }, FL { 4000, "4k" }, FL { 8000, "8k" }, FL { 16000, "16k" } })
    {
        const float z = freqToZ (fl.hz);
        g.setColour (theme.grid.withAlpha (0.55f));
        g.drawLine (Line<float> (proj (-1, 0, z), proj (1, 0, z)), 0.7f);
        g.setColour (theme.text.withAlpha (0.75f));
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
    label (-1.0f, "L", theme.text);
    label ( 0.0f, "C", theme.accent.withAlpha (0.8f));
    label ( 1.0f, "R", theme.text);
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

    g.setColour (theme.accent.withAlpha (0.12f));
    g.strokePath (spine, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (theme.accent.withAlpha (0.55f));
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
        const auto c = dotColour (theme, d.p.z).withAlpha (jlimit (0.0f, 1.0f, d.life * 0.9f));
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
    g.setColour (theme.text);
    g.drawText ("PHASE", area.removeFromLeft (44), Justification::centredLeft);
    g.drawText (String (correlation, 2), area.removeFromRight (40), Justification::centredRight);

    auto bar = area.withSizeKeepingCentre (area.getWidth(), 6.0f);
    g.setColour (theme.track);
    g.fillRoundedRectangle (bar, 3.0f);

    const float cx = bar.getCentreX();
    const float x  = bar.getX() + (correlation * 0.5f + 0.5f) * bar.getWidth();
    const auto  col = correlation < 0.0f ? theme.warning : theme.accent;

    g.setColour (col.withAlpha (0.7f));
    g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cx, x), bar.getY(), jmax (cx, x), bar.getBottom()));
    g.setColour (theme.pointer);
    g.fillRect (x - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f);

    g.setColour (theme.text.withAlpha (0.6f));
    g.drawText ("-1", bar.translated (0, 8).withWidth (20), Justification::left);
    g.drawText ("+1", bar.translated (0, 8).withTrimmedLeft (bar.getWidth() - 20), Justification::right);
}

//==============================================================================
float Scene3D::zToFreq (float z) noexcept
{
    return minHz * std::pow (maxHz / minHz, jlimit (0.0f, 1.0f, z));
}

void Scene3D::setParam (RangedAudioParameter* p, float value)
{
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

bool Scene3D::screenToFloor (Point<float> s, float& x, float& z) const
{
    // Inverse of project(): cast a ray from the camera and intersect it with the floor (y = 0).
    const float k = viewScale * 2.6f;
    const float u =  (s.x - viewCentre.x) / k;
    const float v = -(s.y - viewCentre.y) / k;

    const float cp = std::cos (pitch), sp = std::sin (pitch);
    const float cy = std::cos (yaw),   sy = std::sin (yaw);

    const float floorPy = -0.42f;                 // world y = 0 in centred coordinates
    const float denom = v * cp - sp;
    if (std::abs (denom) < 1.0e-5f)
        return false;

    const float t = (floorPy - camDist * sp) / denom;   // depth along the ray
    if (t <= 0.2f)
        return false;

    const float x1 = u * t, y2 = v * t, z2 = t - camDist;
    const float z1 = y2 * sp + z2 * cp;
    const float px = x1 * cy - z1 * sy;
    const float pz = x1 * sy + z1 * cy;

    x = px;
    z = (pz + 1.0f) * 0.5f;
    return true;
}

int Scene3D::hitTestNode (Point<float> s) const
{
    int best = -1;
    float bestDist = 12.0f;

    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        if (! nodeOn (i))
            continue;

        const float x = nodePan (i), z = freqToZ (nodeFreq (i));
        const Line<float> pole (proj (x, 0.0f, z), proj (x, 1.0f, z));
        Point<float> nearest;
        const float d = pole.getDistanceFromPoint (s, nearest);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void Scene3D::addNodeAt (float x, float z)
{
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        if (nodeOn (i))
            continue;

        auto& n = nodes[(size_t) i];
        setParam (n.freq, zToFreq (z));
        setParam (n.pan, jlimit (-1.0f, 1.0f, x) * 100.0f);
        setParam (n.bw, 1.0f);
        setParam (n.on, 1.0f);
        selectedNode = i;
        return;
    }

    statusMessage = "All " + String ((int) nodes.size()) + " nodes are in use - right-click one to remove it";
    statusUntil = Time::getMillisecondCounterHiRes() + 2500.0;
}

void Scene3D::removeNode (int i)
{
    if (isPositiveAndBelow (i, (int) nodes.size()))
    {
        setParam (nodes[(size_t) i].on, 0.0f);
        setParam (nodes[(size_t) i].pan, 0.0f);
    }
    if (selectedNode == i) selectedNode = -1;
    if (hoveredNode == i)  hoveredNode = -1;
}

void Scene3D::clearNodes()
{
    for (int i = 0; i < (int) nodes.size(); ++i)
        if (nodeOn (i))
            removeNode (i);
    selectedNode = -1;
}

//==============================================================================
void Scene3D::drawNodes (Graphics& g)
{
    int used = 0;
    for (int i = 0; i < (int) nodes.size(); ++i)
        used += nodeOn (i) ? 1 : 0;
    const bool compactLabels = used > 3;

    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        if (! nodeOn (i))
            continue;

        const float x  = nodePan (i);
        const float f  = nodeFreq (i);
        const float bw = nodeBw (i);
        const float z  = freqToZ (f);
        const float zLo = freqToZ (f / std::pow (2.0f, bw * 0.5f));
        const float zHi = freqToZ (f * std::pow (2.0f, bw * 0.5f));
        const bool  active = (i == selectedNode || i == hoveredNode || i == draggingNode);
        const auto  col = dotColour (theme, z);

        // Band footprint on the floor: the range of frequencies this node moves
        Path band;
        band.startNewSubPath (proj (x - 0.07f, 0.0f, zLo));
        band.lineTo (proj (x + 0.07f, 0.0f, zLo));
        band.lineTo (proj (x + 0.07f, 0.0f, zHi));
        band.lineTo (proj (x - 0.07f, 0.0f, zHi));
        band.closeSubPath();
        g.setColour (col.withAlpha (active ? 0.45f : 0.25f));
        g.fillPath (band);
        g.setColour (col.withAlpha (0.8f));
        g.strokePath (band, PathStrokeType (1.0f));

        // Guide from the centre line to the node so you can see how far it was moved
        g.setColour (col.withAlpha (0.5f));
        Path guide;
        guide.startNewSubPath (proj (0.0f, 0.0f, z));
        guide.lineTo (proj (x, 0.0f, z));
        const float dashes[] { 4.0f, 4.0f };
        PathStrokeType (1.0f).createDashedStroke (guide, guide, dashes, 2);
        g.fillPath (guide);

        // Pole + handles
        const auto base = proj (x, 0.0f, z), top = proj (x, 1.0f, z);
        g.setColour (col.withAlpha (active ? 0.9f : 0.55f));
        g.drawLine (Line<float> (base, top), active ? 2.4f : 1.4f);

        const float r = active ? 7.0f : 5.5f;
        g.setColour (col.withAlpha (0.3f));
        g.fillEllipse (Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (base));
        g.setColour (col);
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (base));
        g.setColour (theme.pointer);
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (base), active ? 2.0f : 1.2f);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (top));

        // Label above the pole
        const String label = String (i + 1) + ":  " + nodes[(size_t) i].freq->getCurrentValueAsText()
                             + "  " + nodes[(size_t) i].pan->getCurrentValueAsText();
        g.setFont (FontOptions (11.0f, Font::bold));

        if (compactLabels && ! active)
        {
            const auto badge = Rectangle<float> (18.0f, 16.0f).withCentre (top.translated (0.0f, -12.0f));
            g.setColour (theme.panel.withAlpha (0.85f));
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (theme.dimText);
            g.drawText (String (i + 1), badge, Justification::centred);
            continue;
        }

        const auto box = Rectangle<float> (110.0f, 18.0f).withCentre (top.translated (0.0f, -14.0f));
        g.setColour (theme.panel.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (active ? theme.text : theme.dimText);
        g.drawText (label, box, Justification::centred);
    }
}

void Scene3D::drawNodeInfo (Graphics& g)
{
    int used = 0;
    for (int i = 0; i < (int) nodes.size(); ++i)
        used += nodeOn (i) ? 1 : 0;

    auto area = getLocalBounds().reduced (12, 8).withTrimmedTop (18).toFloat();
    g.setFont (FontOptions (11.0f));
    g.setColour (theme.dimText);
    g.drawText ("NODES " + String (used) + "/" + String ((int) nodes.size()), area.removeFromTop (14), Justification::topLeft);

    const int show = draggingNode >= 0 ? draggingNode : (hoveredNode >= 0 ? hoveredNode : selectedNode);
    if (show >= 0 && nodeOn (show))
    {
        const auto& n = nodes[(size_t) show];
        g.setColour (theme.text);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("Node " + String (show + 1) + ":  " + n.freq->getCurrentValueAsText()
                      + "   position " + n.pan->getCurrentValueAsText()
                      + "   band " + n.bw->getCurrentValueAsText(),
                    area.removeFromTop (16), Justification::topLeft);
    }

    if (Time::getMillisecondCounterHiRes() < statusUntil)
    {
        g.setColour (theme.warning);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (statusMessage, area.removeFromTop (16), Justification::topLeft);
    }
}

//==============================================================================
void Scene3D::mouseDown (const MouseEvent& e)
{
    const int hit = hitTestNode (e.position);

    if (hit >= 0 && e.mods.isPopupMenu())
    {
        removeNode (hit);
        return;
    }

    if (hit >= 0)
    {
        selectedNode = draggingNode = hit;
        nodes[(size_t) hit].freq->beginChangeGesture();
        nodes[(size_t) hit].pan->beginChangeGesture();
        setMouseCursor (MouseCursor::DraggingHandCursor);
        return;
    }

    selectedNode = -1;
    dragStartYaw = yaw;
    dragStartPitch = pitch;
}

void Scene3D::mouseDrag (const MouseEvent& e)
{
    if (draggingNode >= 0)
    {
        float x = 0.0f, z = 0.0f;
        if (screenToFloor (e.position, x, z))
        {
            auto& n = nodes[(size_t) draggingNode];
            n.pan->setValueNotifyingHost (n.pan->convertTo0to1 (jlimit (-1.0f, 1.0f, x) * 100.0f));
            if (! e.mods.isShiftDown())
                n.freq->setValueNotifyingHost (n.freq->convertTo0to1 (zToFreq (z)));
        }
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    yaw   = dragStartYaw + (float) e.getDistanceFromDragStartX() * 0.008f;
    pitch = jlimit (0.0f, 1.35f, dragStartPitch + (float) e.getDistanceFromDragStartY() * 0.006f);
}

void Scene3D::mouseUp (const MouseEvent&)
{
    if (draggingNode >= 0)
    {
        nodes[(size_t) draggingNode].freq->endChangeGesture();
        nodes[(size_t) draggingNode].pan->endChangeGesture();
        draggingNode = -1;
        setMouseCursor (hoveredNode >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }
}

void Scene3D::mouseMove (const MouseEvent& e)
{
    const int hit = hitTestNode (e.position);
    if (hit != hoveredNode)
    {
        hoveredNode = hit;
        setMouseCursor (hit >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }
}

void Scene3D::mouseExit (const MouseEvent&)
{
    hoveredNode = -1;
}

void Scene3D::mouseDoubleClick (const MouseEvent& e)
{
    if (hitTestNode (e.position) >= 0)
        return; // double-clicking a node does nothing (avoids accidental duplicates)

    float x = 0.0f, z = 0.0f;
    if (screenToFloor (e.position, x, z) && x >= -1.05f && x <= 1.05f && z >= -0.02f && z <= 1.02f)
    {
        addNodeAt (x, z);
        return;
    }

    yaw = -0.55f; pitch = 0.42f; camDist = 4.3f;
}

void Scene3D::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int hit = hitTestNode (e.position);
    if (hit >= 0)
    {
        auto* bw = nodes[(size_t) hit].bw;
        const float current = bw->convertFrom0to1 (bw->getValue());
        bw->beginChangeGesture();
        setParam (bw, jlimit (0.2f, 3.0f, current + (w.deltaY > 0 ? 0.1f : -0.1f)));
        bw->endChangeGesture();
        selectedNode = hit;
        return;
    }

    camDist = jlimit (2.2f, 7.0f, camDist - w.deltaY * 1.5f);
}
