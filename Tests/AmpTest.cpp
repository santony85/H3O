// Teste Simple Amp complet sur un vrai fichier .nam, à 48 kHz puis à 44,1 kHz
// (conversion de fréquence). Cible Xcode : AmpTest
//   ./build/Release/AmpTest "/chemin/vers/modele.nam"
#include "Amp/AmpProcessor.h"

#include <cstdio>

namespace
{
    struct Result { double rmsDb; int longestSilence; bool finite; bool resampled; };

    Result run (const juce::File& modelFile, double sampleRate, int blockSize)
    {
        AmpProcessor amp;
        amp.setRateAndBufferSizeDetails (sampleRate, blockSize);
        amp.prepareToPlay (sampleRate, blockSize);

        if (auto* p = amp.parameters.getParameter ("input"))
            p->setValueNotifyingHost (p->convertTo0to1 (InputMode::monoLeft));
        if (auto* p = amp.parameters.getParameter ("cab"))
            p->setValueNotifyingHost (0.0f);   // le modèle seul, sans baffle

        const auto error = amp.loadModel (modelFile);
        if (error.isNotEmpty())
        {
            std::printf ("ÉCHEC : %s\n", error.toRawUTF8());
            std::exit (1);
        }

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        const int total = (int) (sampleRate * 4);
        double sumSq = 0.0;
        long counted = 0;
        int silence = 0, longest = 0;
        bool finite = true;

        for (int start = 0; start < total; start += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = (start + i) / sampleRate;
                buffer.setSample (0, i, (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)));
                buffer.setSample (1, i, 0.0f);
            }

            amp.processBlock (buffer, midi);

            // On ignore la première seconde (préchauffage du modèle)
            if (start < (int) sampleRate)
                continue;

            for (int i = 0; i < blockSize; ++i)
            {
                const float y = buffer.getSample (0, i);
                finite = finite && std::isfinite (y) && std::abs (y - buffer.getSample (1, i)) < 1e-6f;
                sumSq += (double) y * y;
                ++counted;

                silence = std::abs (y) < 1e-6f ? silence + 1 : 0;
                longest = std::max (longest, silence);
            }
        }

        return { 10.0 * std::log10 (sumSq / (double) counted + 1e-20), longest, finite, amp.getModelInfo().resampled };
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (argc < 2)
    {
        std::printf ("Usage : AmpTest fichier.nam\n");
        return 2;
    }

    const juce::File model (juce::String::fromUTF8 (argv[1]));
    int failures = 0;

    const auto r48 = run (model, 48000.0, 512);
    const auto r44 = run (model, 44100.0, 512);
    const auto r44small = run (model, 44100.0, 64);

    auto report = [&failures] (const char* name, const Result& r, double refDb)
    {
        const bool ok = r.finite && r.longestSilence < 8 && std::abs (r.rmsDb - refDb) < 1.5;
        std::printf ("%-28s niveau %6.1f dB  silence max %d éch.  %s  %s\n", name, r.rmsDb, r.longestSilence,
                     r.resampled ? "converti " : "direct   ", ok ? "OK" : "ÉCHEC");
        if (! ok) ++failures;
    };

    report ("48 kHz, blocs de 512", r48, r48.rmsDb);
    report ("44,1 kHz, blocs de 512", r44, r48.rmsDb);
    report ("44,1 kHz, blocs de 64", r44small, r48.rmsDb);

    if (r48.resampled || ! r44.resampled)
    {
        std::printf ("ÉCHEC : la conversion de fréquence ne s'active pas comme prévu\n");
        ++failures;
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
