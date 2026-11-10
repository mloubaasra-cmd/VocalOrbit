#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

// Lock-free single-producer / single-consumer stereo sample queue.
// The audio thread pushes the processed output, the UI thread pulls it
// to draw the 3D view. If the UI isn't open the queue simply fills and
// further pushes are dropped (cheap, never blocks the audio thread).
class StereoSampleFifo
{
public:
    explicit StereoSampleFifo (int capacity)
        : fifo (capacity), left ((size_t) capacity, 0.0f), right ((size_t) capacity, 0.0f) {}

    void push (const float* l, const float* r, int numSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
        if (n1 > 0) { copy (left.data() + s1, l, n1);      copy (right.data() + s1, r, n1); }
        if (n2 > 0) { copy (left.data() + s2, l + n1, n2); copy (right.data() + s2, r + n1, n2); }
        fifo.finishedWrite (n1 + n2);
    }

    int pull (float* l, float* r, int maxSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxSamples, s1, n1, s2, n2);
        if (n1 > 0) { copy (l, left.data() + s1, n1);      copy (r, right.data() + s1, n1); }
        if (n2 > 0) { copy (l + n1, left.data() + s2, n2); copy (r + n1, right.data() + s2, n2); }
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    std::atomic<double> sampleRate { 44100.0 };

private:
    static void copy (float* dst, const float* src, int n) noexcept
    {
        std::copy (src, src + n, dst);
    }

    juce::AbstractFifo fifo;
    std::vector<float> left, right;
};
