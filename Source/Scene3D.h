#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "StereoSampleFifo.h"

// 3D stereo spectrum.
//   X axis = stereo position (Left ... Right)
//   Z axis = frequency (low at the front, high at the back, log scale)
//   Y axis = level (dB)
// Every frequency band spawns glowing dots at the position where its energy
// sits in the stereo field. Wider (more decorrelated) content scatters further
// sideways, so as you turn up WIDTH / SPREAD the vocal "cloud" visibly opens up.
class Scene3D : public juce::Component, private juce::Timer
{
public:
    explicit Scene3D (StereoSampleFifo& fifoToUse);
    ~Scene3D() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr int fftOrder  = 11;
    static constexpr int fftSize   = 1 << fftOrder;
    static constexpr int numBands  = 96;
    static constexpr int maxDots   = 3500;
    static constexpr float minHz   = 40.0f;
    static constexpr float maxHz   = 16000.0f;
    static constexpr float floorDb = -78.0f;

    struct Vec3 { float x, y, z; };
    struct Dot  { Vec3 p; float life, size, hue; };
    struct Projected { juce::Point<float> pt; float depth, scale; };

    void timerCallback() override;
    bool pullAudio();
    void analyse();
    void updateDots (float dt, bool hasNewAudio);
    void rebuildBands (double sampleRate);

    Projected project (Vec3 world) const noexcept;
    juce::Point<float> proj (float x, float y, float z) const noexcept { return project ({ x, y, z }).pt; }
    static float freqToZ (float hz) noexcept;
    static float dbToY (float db) noexcept;

    void drawStage (juce::Graphics&);
    void drawDots (juce::Graphics&);
    void drawSpine (juce::Graphics&);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<float>);

    StereoSampleFifo& fifo;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> histL, histR, pullL, pullR, fftL, fftR;
    std::vector<int> bandLo, bandHi;
    double bandsSampleRate = 0.0;

    std::array<float, numBands> bandDb {}, bandPan {}, bandSpread {};

    std::vector<Dot> dots;
    juce::Random rng;

    float correlation = 1.0f;
    double lastTime = 0.0;

    // Camera
    float yaw = -0.55f, pitch = 0.42f, camDist = 4.3f;
    float dragStartYaw = 0.0f, dragStartPitch = 0.0f;

    juce::Rectangle<float> viewArea;
    juce::Point<float> viewCentre;
    float viewScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Scene3D)
};
