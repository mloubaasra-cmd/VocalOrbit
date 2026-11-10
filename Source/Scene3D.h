#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "StereoSampleFifo.h"
#include "Themes.h"

// 3D stereo spectrum.
//   X axis = stereo position (Left ... Right)
//   Z axis = frequency (low at the front, high at the back, log scale)
//   Y axis = level (dB)
// Every frequency band spawns glowing dots at the position where its energy
// sits in the stereo field. Wider (more decorrelated) content scatters further
// sideways, so as you turn up WIDTH / SPREAD the vocal "cloud" visibly opens up.
//
// Frequency nodes: double-click the floor to grab the frequencies at that spot,
// then drag the node to place those frequencies anywhere from Left to Right.
class Scene3D : public juce::Component, private juce::Timer
{
public:
    Scene3D (StereoSampleFifo& fifoToUse, juce::AudioProcessorValueTreeState& state, int numNodes);
    ~Scene3D() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setTheme (const Theme& t) { theme = t; repaint(); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

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
    void drawNodes (juce::Graphics&);
    void drawNodeInfo (juce::Graphics&);

    // Frequency nodes
    struct NodeParams
    {
        juce::RangedAudioParameter *on = nullptr, *freq = nullptr, *pan = nullptr, *bw = nullptr;
    };
    bool  nodeOn   (int i) const { return nodes[(size_t) i].on->getValue() > 0.5f; }
    float nodeFreq (int i) const { return nodes[(size_t) i].freq->convertFrom0to1 (nodes[(size_t) i].freq->getValue()); }
    float nodePan  (int i) const { return nodes[(size_t) i].pan->convertFrom0to1 (nodes[(size_t) i].pan->getValue()) * 0.01f; }
    float nodeBw   (int i) const { return nodes[(size_t) i].bw->convertFrom0to1 (nodes[(size_t) i].bw->getValue()); }
    static void setParam (juce::RangedAudioParameter* p, float value);
    int  hitTestNode (juce::Point<float> screenPos) const;
    bool screenToFloor (juce::Point<float> screenPos, float& x, float& z) const;
    static float zToFreq (float z) noexcept;
    void addNodeAt (float x, float z);
    void removeNode (int i);
    void clearNodes();

    std::vector<NodeParams> nodes;
    int selectedNode = -1, hoveredNode = -1, draggingNode = -1;
    juce::String statusMessage;
    double statusUntil = 0.0;
    juce::TextButton clearButton { "CLEAR NODES" };

    StereoSampleFifo& fifo;
    Theme theme = getThemes()[0];

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
