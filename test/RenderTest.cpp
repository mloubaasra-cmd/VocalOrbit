// Feeds a synthetic mono "vocal" through the plugin and saves editor screenshots.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
using namespace juce;
static void runAudio (VocalOrbitProcessor& p, float seconds, double& phase, Random& rng)
{
    const double sr = 48000; const int bs = 512;
    AudioBuffer<float> buf (2, bs); MidiBuffer midi;
    for (int b = 0; b < (int) (seconds * sr / bs); ++b) {
        for (int i = 0; i < bs; ++i) {
            phase += 220.0 / sr; float v = 0;
            for (int h = 1; h < 30; ++h) v += (float) std::sin (2 * MathConstants<double>::pi * phase * h) * 0.25f / (float) h * (h % 3 == 0 ? 1.6f : 1.0f);
            v += (rng.nextFloat() - 0.5f) * 0.02f;
            buf.setSample (0, i, v); buf.setSample (1, i, v);
        }
        p.processBlock (buf, midi);
    }
}
static void setP (VocalOrbitProcessor& p, const String& id, float v)
{
    auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
// Plays a mono sine and returns RMS of L and R after the plugin.
static std::pair<double,double> sineTest (VocalOrbitProcessor& p, double hz)
{
    const double sr = 48000; const int bs = 512; AudioBuffer<float> buf (2, bs); MidiBuffer midi;
    double ph = 0, sl = 0, sr2 = 0; int count = 0;
    for (int b = 0; b < 200; ++b) {
        for (int i = 0; i < bs; ++i) { float v = 0.5f * (float) std::sin (ph); ph += 2 * MathConstants<double>::pi * hz / sr; buf.setSample (0, i, v); buf.setSample (1, i, v); }
        p.processBlock (buf, midi);
        if (b > 100) for (int i = 0; i < bs; ++i) { sl += buf.getSample (0, i) * buf.getSample (0, i); sr2 += buf.getSample (1, i) * buf.getSample (1, i); ++count; }
    }
    return { std::sqrt (sl / count), std::sqrt (sr2 / count) };
}
static void dspTests()
{
    VocalOrbitProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
    setP (p, "spread", 0); setP (p, "bassFocus", 20);
    auto base = sineTest (p, 1000);
    setP (p, "node1_freq", 1000); setP (p, "node1_pan", 0); setP (p, "node1_on", 1);
    auto centred = sineTest (p, 1000);
    setP (p, "node1_pan", -100);
    auto left = sineTest (p, 1000);
    auto other = sineTest (p, 6000);   // a frequency far away from the node
    std::printf ("no node      L=%.4f R=%.4f\n", base.first, base.second);
    std::printf ("node centre  L=%.4f R=%.4f\n", centred.first, centred.second);
    std::printf ("node hard L  L=%.4f R=%.4f  (1 kHz moved)\n", left.first, left.second);
    std::printf ("6 kHz other  L=%.4f R=%.4f  (should stay centred)\n", other.first, other.second);
}
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    dspTests();
    VocalOrbitProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (920, 640);
    double phase = 0; Random rng (1);
    auto shot = [&] (const char* name, int preset, int theme) {
        p.setCurrentProgram (preset);
        p.apvts.state.setProperty ("theme", theme, nullptr);
        for (int f = 0; f < 25; ++f) { runAudio (p, 1.0f / 30, phase, rng); MessageManager::getInstance()->runDispatchLoopUntil (34); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        File out (String (argv[1]) + "/" + name); out.deleteFile();
        FileOutputStream os (out); PNGImageFormat().writeImageToStream (img, os);
    };
    shot ("theme-blue.png",   3, 0);   // Wide Pop Lead
    shot ("theme-pink.png",   7, 1);   // Huge Chorus Stack
    shot ("theme-purple.png", 4, 2);   // R&B Silk
    shot ("theme-tan.png",   13, 3);   // Harmony Left
    shot ("preset-body-right-air-left.png", 16, 0);
    shot ("preset-orbit-spread.png", 24, 2);
    shot ("preset-back-to-init.png", 0, 0);   // must clear the nodes again
    for (int i = 0; i < p.getNumPrograms(); ++i) std::printf ("%2d %s\n", i + 1, p.getProgramName (i).toRawUTF8());
    ed.reset(); return 0;
}
