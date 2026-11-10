#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

VocalOrbitProcessor::VocalOrbitProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    pWidth      = apvts.getRawParameterValue ("width");
    pSpread     = apvts.getRawParameterValue ("spread");
    pSpreadTime = apvts.getRawParameterValue ("spreadTime");
    pBassFocus  = apvts.getRawParameterValue ("bassFocus");
    pPan        = apvts.getRawParameterValue ("pan");
    pMix        = apvts.getRawParameterValue ("mix");
    pOutput     = apvts.getRawParameterValue ("output");
    pMonoCheck  = apvts.getRawParameterValue ("monoCheck");

    for (int i = 0; i < numNodes; ++i)
    {
        auto& n = nodeStates[(size_t) i];
        n.on   = apvts.getRawParameterValue (nodeParamId (i, "on"));
        n.freq = apvts.getRawParameterValue (nodeParamId (i, "freq"));
        n.pan  = apvts.getRawParameterValue (nodeParamId (i, "pan"));
        n.bw   = apvts.getRawParameterValue (nodeParamId (i, "bw"));
    }
}

AudioProcessorValueTreeState::ParameterLayout VocalOrbitProcessor::createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes()
                 .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width",
                    NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, pct));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "spread", 1 }, "Spread",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, pct));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "spreadTime", 1 }, "Spread Time",
                    NormalisableRange<float> (2.0f, 35.0f, 0.01f, 0.6f), 14.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (
                        [] (float v, int) { return String (v, 1) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bassFocus", 1 }, "Bass Focus",
                    NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.4f), 120.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (
                        [] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pan", 1 }, "Pan",
                    NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                    {
                        auto r = roundToInt (v);
                        return r == 0 ? String ("C") : (r < 0 ? "L" + String (-r) : "R" + String (r));
                    })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                    NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (
                        [] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "monoCheck", 1 }, "Mono Check", false));

    // Frequency nodes
    const NormalisableRange<float> freqRange (40.0f, 16000.0f,
        [] (float start, float end, float v) { return start * std::pow (end / start, v); },
        [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); });

    auto freqText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz";
    });
    auto panText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        auto r = roundToInt (v);
        return r == 0 ? String ("C") : (r < 0 ? "L" + String (-r) : "R" + String (r));
    });
    auto bwText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return String (v, 1) + " oct";
    });

    const float defaultFreqs[numNodes] { 150, 300, 600, 1200, 2400, 4800, 8000, 12000 };

    for (int i = 0; i < numNodes; ++i)
    {
        const String n = "Node " + String (i + 1) + " ";
        layout.add (std::make_unique<AudioParameterBool>  (ParameterID { nodeParamId (i, "on"), 1 }, n + "On", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { nodeParamId (i, "freq"), 1 }, n + "Freq",
                                                           freqRange, defaultFreqs[i], freqText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { nodeParamId (i, "pan"), 1 }, n + "Position",
                                                           NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, panText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { nodeParamId (i, "bw"), 1 }, n + "Band Width",
                                                           NormalisableRange<float> (0.2f, 3.0f, 0.01f), 1.0f, bwText));
    }

    return layout;
}

bool VocalOrbitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

void VocalOrbitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    visualFifo.sampleRate = sampleRate;

    dsp::ProcessSpec spec { sampleRate, (uint32) samplesPerBlock, 1 };

    spreadDelay.setMaximumDelayInSamples ((int) std::ceil (0.05 * sampleRate) + 4);
    spreadDelay.prepare (spec);
    spreadDelay.reset();

    sideHighPass.setType (dsp::StateVariableTPTFilterType::highpass);
    sideHighPass.prepare (spec);
    sideHighPass.reset();

    const double rampSecs = 0.03;
    for (auto* s : { &sWidth, &sSpread, &sSpreadTime, &sPan, &sMix, &sGain })
        s->reset (sampleRate, rampSecs);

    sSpreadTime.reset (sampleRate, 0.15); // slower ramp avoids zipper/pitch artefacts

    dsp::ProcessSpec stereoSpec { sampleRate, (uint32) samplesPerBlock, 2 };
    for (auto& n : nodeStates)
    {
        n.filter.setType (dsp::StateVariableTPTFilterType::bandpass);
        n.filter.prepare (stereoSpec);
        n.filter.reset();
        n.amount.reset (sampleRate, 0.05);
        n.panSm.reset (sampleRate, 0.05);
        n.amount.setCurrentAndTargetValue (n.on->load() > 0.5f ? 1.0f : 0.0f);
        n.panSm.setCurrentAndTargetValue (n.pan->load() * 0.01f);
    }

    sWidth.setCurrentAndTargetValue      (pWidth->load() * 0.01f);
    sSpread.setCurrentAndTargetValue     (pSpread->load() * 0.01f);
    sSpreadTime.setCurrentAndTargetValue (pSpreadTime->load());
    sPan.setCurrentAndTargetValue        (pPan->load() * 0.01f);
    sMix.setCurrentAndTargetValue        (pMix->load() * 0.01f);
    sGain.setCurrentAndTargetValue       (Decibels::decibelsToGain (pOutput->load()));
}

void VocalOrbitProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    if (numOut < 2 || n == 0)
        return;

    if (numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n); // mono input -> duplicate to both sides

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    sWidth.setTargetValue      (pWidth->load() * 0.01f);
    sSpread.setTargetValue     (pSpread->load() * 0.01f * 0.7f); // 100% -> 0.7 keeps combs musical
    sSpreadTime.setTargetValue (pSpreadTime->load());
    sPan.setTargetValue        (pPan->load() * 0.01f);
    sMix.setTargetValue        (pMix->load() * 0.01f);
    sGain.setTargetValue       (Decibels::decibelsToGain (pOutput->load()));
    sideHighPass.setCutoffFrequency (pBassFocus->load());

    // Update node filters once per block
    std::array<float, numNodes> nodeNorm {}; // 1/Q: turns the band-pass into a unity-gain band extractor
    bool anyNode = false;
    for (size_t k = 0; k < nodeStates.size(); ++k)
    {
        auto& nd = nodeStates[k];
        nd.amount.setTargetValue (nd.on->load() > 0.5f ? 1.0f : 0.0f);
        nd.panSm.setTargetValue (nd.pan->load() * 0.01f);

        const float octaves = jlimit (0.2f, 3.0f, nd.bw->load());
        const float r = std::pow (2.0f, octaves);
        const float q = std::sqrt (r) / (r - 1.0f);
        nd.filter.setResonance (q);
        nd.filter.setCutoffFrequency (jlimit (20.0f, (float) (currentSampleRate * 0.45), nd.freq->load()));
        nodeNorm[k] = 1.0f / q;
        anyNode = anyNode || nd.amount.isSmoothing() || nd.amount.getTargetValue() > 0.0f;
    }

    const bool monoCheck = pMonoCheck->load() > 0.5f;
    const float msToSamples = (float) (currentSampleRate * 0.001);

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];

        const float mid  = 0.5f * (dryL + dryR);
        const float side = 0.5f * (dryL - dryR);

        // Artificial side signal from a short delay of the mid (Haas-style, mono-safe).
        const float delayed = spreadDelay.popSample (0, sSpreadTime.getNextValue() * msToSamples);
        spreadDelay.pushSample (0, mid);

        float newSide = side * sWidth.getNextValue() + delayed * sSpread.getNextValue();
        newSide = sideHighPass.processSample (0, newSide);

        float outL = mid + newSide;
        float outR = mid - newSide;

        // Frequency nodes: lift a band out, place it at the chosen stereo position, put it back.
        if (anyNode)
        {
            for (size_t k = 0; k < nodeStates.size(); ++k)
            {
                auto& nd = nodeStates[k];
                const float bandL = nd.filter.processSample (0, outL) * nodeNorm[k];
                const float bandR = nd.filter.processSample (1, outR) * nodeNorm[k];
                const float amt = nd.amount.getNextValue();
                const float p   = nd.panSm.getNextValue();
                if (amt <= 0.0f)
                    continue;

                const float bandMid  = 0.5f * (bandL + bandR);
                const float bandSide = 0.5f * (bandL - bandR);
                const float angle = (p + 1.0f) * MathConstants<float>::pi * 0.25f; // constant-power pan
                const float gL = std::cos (angle) * MathConstants<float>::sqrt2;
                const float gR = std::sin (angle) * MathConstants<float>::sqrt2;

                const float placedL = bandMid * gL + bandSide;
                const float placedR = bandMid * gR - bandSide;

                outL += amt * (placedL - bandL);
                outR += amt * (placedR - bandR);
            }
        }

        // Balance-style pan (no level boost at the extremes).
        const float pan = sPan.getNextValue();
        outL *= jmin (1.0f, 1.0f - pan);
        outR *= jmin (1.0f, 1.0f + pan);

        const float mix = sMix.getNextValue();
        outL = dryL + (outL - dryL) * mix;
        outR = dryR + (outR - dryR) * mix;

        const float g = sGain.getNextValue();
        outL *= g;
        outR *= g;

        if (monoCheck)
            outL = outR = 0.5f * (outL + outR);

        L[i] = outL;
        R[i] = outR;
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    visualFifo.push (L, R, n);
}

const std::vector<VocalOrbitProcessor::Preset>& VocalOrbitProcessor::getPresets()
{
    //                      name                  width spread time  bass  pan   mix   out
    static const std::vector<Preset> presets {
        { "Init",                 100.0f,  0.0f, 14.0f, 120.0f,   0.0f, 100.0f,  0.0f },
        { "Subtle Lead",          110.0f, 20.0f, 12.0f, 150.0f,   0.0f, 100.0f,  0.0f },
        { "Pop Lead",             120.0f, 35.0f, 14.0f, 150.0f,   0.0f, 100.0f, -0.5f },
        { "Wide Pop Lead",        140.0f, 50.0f, 16.0f, 160.0f,   0.0f, 100.0f, -1.0f },
        { "R&B Silk",             130.0f, 40.0f, 18.0f, 180.0f,   0.0f, 100.0f, -0.5f },
        { "Rap Upfront",           95.0f, 12.0f,  9.0f, 200.0f,   0.0f, 100.0f,  0.0f },
        { "Trap Ad-libs",         170.0f, 75.0f, 11.0f, 220.0f,   0.0f, 100.0f, -1.5f },
        { "Huge Chorus Stack",    180.0f, 70.0f, 22.0f, 160.0f,   0.0f, 100.0f, -1.5f },
        { "Backing Vocals Wide",  200.0f, 85.0f, 25.0f, 200.0f,   0.0f, 100.0f, -2.0f },
        { "Tight Doubler",        100.0f, 55.0f,  8.0f, 150.0f,   0.0f, 100.0f, -1.0f },
        { "Ambient Haze",         160.0f, 90.0f, 32.0f, 250.0f,   0.0f,  85.0f, -2.0f },
        { "Lo-Fi Narrow",          60.0f, 10.0f,  6.0f, 300.0f,   0.0f, 100.0f,  0.0f },
        { "Mono Focus",             0.0f,  0.0f, 14.0f,  20.0f,   0.0f, 100.0f,  0.0f },
        { "Harmony Left",         130.0f, 45.0f, 15.0f, 180.0f, -40.0f, 100.0f, -1.0f },
        { "Harmony Right",        130.0f, 45.0f, 15.0f, 180.0f,  40.0f, 100.0f, -1.0f },
    };
    return presets;
}

void VocalOrbitProcessor::setCurrentProgram (int index)
{
    const auto& presets = getPresets();
    if (! isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentPreset = index;
    const auto& pr = presets[(size_t) index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set ("width",      pr.width);
    set ("spread",     pr.spread);
    set ("spreadTime", pr.spreadTime);
    set ("bassFocus",  pr.bassFocus);
    set ("pan",        pr.pan);
    set ("mix",        pr.mix);
    set ("output",     pr.output);
}

const String VocalOrbitProcessor::getProgramName (int index)
{
    const auto& presets = getPresets();
    return isPositiveAndBelow (index, (int) presets.size()) ? String (presets[(size_t) index].name) : String();
}

AudioProcessorEditor* VocalOrbitProcessor::createEditor()
{
    return new VocalOrbitEditor (*this);
}

void VocalOrbitProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalOrbitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            currentPreset = jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0));
            apvts.replaceState (state);
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalOrbitProcessor();
}
