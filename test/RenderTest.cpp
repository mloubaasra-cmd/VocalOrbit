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
    auto shot = [&] (const char* name, float spread, float width) {
        p.apvts.getParameter ("spread")->setValueNotifyingHost (p.apvts.getParameterRange ("spread").convertTo0to1 (spread));
        p.apvts.getParameter ("width")->setValueNotifyingHost (p.apvts.getParameterRange ("width").convertTo0to1 (width));
        for (int f = 0; f < 25; ++f) { runAudio (p, 1.0f / 30, phase, rng); MessageManager::getInstance()->runDispatchLoopUntil (34); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        File out (String (argv[1]) + "/" + name); out.deleteFile();
        FileOutputStream os (out); PNGImageFormat().writeImageToStream (img, os);
    };
    shot ("mono.png", 0, 100);
    shot ("wide.png", 85, 150);
    ed.reset(); return 0;
}
