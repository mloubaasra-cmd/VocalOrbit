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

AudioProcessorEditor* VocalOrbitProcessor::createEditor()
{
    return new VocalOrbitEditor (*this);
}

void VocalOrbitProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalOrbitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalOrbitProcessor();
}
