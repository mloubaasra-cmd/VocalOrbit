#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "StereoSampleFifo.h"

class VocalOrbitProcessor : public juce::AudioProcessor
{
public:
    VocalOrbitProcessor();
    ~VocalOrbitProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    // Factory presets (also shown in FL Studio's preset menu)
    struct Preset
    {
        const char* name;
        float width, spread, spreadTime, bassFocus, pan, mix, output;
    };
    static const std::vector<Preset>& getPresets();

    int getNumPrograms() override { return (int) getPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    StereoSampleFifo& getVisualFifo() noexcept { return visualFifo; }

private:
    StereoSampleFifo visualFifo { 1 << 15 };

    std::atomic<float>* pWidth      = nullptr;
    std::atomic<float>* pSpread     = nullptr;
    std::atomic<float>* pSpreadTime = nullptr;
    std::atomic<float>* pBassFocus  = nullptr;
    std::atomic<float>* pPan        = nullptr;
    std::atomic<float>* pMix        = nullptr;
    std::atomic<float>* pOutput     = nullptr;
    std::atomic<float>* pMonoCheck  = nullptr;

    juce::SmoothedValue<float> sWidth, sSpread, sSpreadTime, sPan, sMix, sGain;

    // Delayed copy of the mid signal: becomes artificial "side" content.
    // L = M + D, R = M - D  ->  wide on stereo, cancels perfectly in mono.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> spreadDelay { 4096 };
    // High-pass on the side signal keeps the low end centred ("bass focus").
    juce::dsp::StateVariableTPTFilter<float> sideHighPass;

    double currentSampleRate = 44100.0;
    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalOrbitProcessor)
};
