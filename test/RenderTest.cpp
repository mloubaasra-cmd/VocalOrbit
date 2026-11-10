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
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
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
    for (int i = 0; i < p.getNumPrograms(); ++i) std::printf ("%2d %s\n", i + 1, p.getProgramName (i).toRawUTF8());
    ed.reset(); return 0;
}
