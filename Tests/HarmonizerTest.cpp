// Teste H3O Harmonizer complet en mode Diatonic (paramètres, presets, traitement par blocs).
// Cible Xcode : HarmonizerTest
#include "Harmonizer/HarmonizerProcessor.h"

#include <chrono>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* text)
    {
        std::printf ("%-78s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    void set (HarmonizerProcessor& h, const char* id, float value)
    {
        auto* p = h.parameters.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float tone (double f, double t)
    {
        double y = 0.0;
        for (int k = 1; k <= 6; ++k)
            y += std::sin (2.0 * juce::MathConstants<double>::pi * f * k * t) / k;
        return (float) (0.3 * y);
    }

    double energyAt (const std::vector<float>& x, size_t from, size_t to, double freq, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr, coeff = 2.0 * std::cos (w);
        const size_t len = to - from;
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * double (i) / double (len - 1));
            const double s0 = x[from + i] * hann + coeff * s1 - s2;
            s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    double measure (const std::vector<float>& x, size_t from, size_t to, double around, double sr)
    {
        double best = 0.0, bestE = -1.0;
        for (double c = -150.0; c <= 150.0; c += 0.5)
        {
            const double f = around * std::pow (2.0, c / 1200.0);
            const double e = energyAt (x, from, to, f, sr);
            if (e > bestE) { bestE = e; best = f; }
        }
        return best;
    }

    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    int presetNamed (HarmonizerProcessor& h, const char* name)
    {
        for (int i = 0; i < h.getNumPrograms(); ++i)
            if (h.getProgramName (i) == name)
                return i;
        return -1;
    }

    void runPhrase (double sr, int blockSize)
    {
        HarmonizerProcessor h;
        h.setRateAndBufferSizeDetails (sr, blockSize);
        h.prepareToPlay (sr, blockSize);

        const int preset = presetNamed (h, "Twin Lead: 3rd Above (A minor)");
        h.setCurrentProgram (preset);
        set (h, "mix", 100.0f);       // la voix seule, pour la mesurer
        set (h, "input", 0.0f);       // stéréo : la même note des deux côtés

        // En la mineur, tierce au-dessus : la→do, do→mi, ré→fa, mi→sol, sol→si
        const int played[]   = { 57, 60, 62, 64, 67 };
        const int expected[] = { 60, 64, 65, 67, 71 };
        const size_t perNote = (size_t) (sr * 0.6);

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        size_t n = 0;
        const size_t total = perNote * std::size (played);
        double seconds = 0.0;

        while (n < total)
        {
            const int count = (int) std::min ((size_t) blockSize, total - n);
            buffer.setSize (2, count, false, false, true);
            for (int i = 0; i < count; ++i)
            {
                const auto x = tone (midiToHz (played[(n + (size_t) i) / perNote]), double (n + (size_t) i) / sr);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            const auto t0 = std::chrono::steady_clock::now();
            h.processBlock (buffer, midi);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

            for (int i = 0; i < count; ++i)
                out.push_back (buffer.getSample (0, i));
            n += (size_t) count;
        }

        char text[200];
        for (size_t k = 0; k < std::size (played); ++k)
        {
            const double f = midiToHz (expected[k]);
            const size_t from = k * perNote + (size_t) (sr * 0.15), to = (k + 1) * perNote;
            const double got = measure (out, from, to, f, sr);
            const double err = 1200.0 * std::log2 (got / f);
            std::snprintf (text, sizeof text, "%.1f kHz, blocs de %d : %s%d → %s%d attendu, écart %+.1f cents",
                           sr / 1000.0, blockSize,
                           DiatonicScale::noteName (played[k]), played[k] / 12 - 1,
                           DiatonicScale::noteName (expected[k]), expected[k] / 12 - 1, err);
            check (std::abs (err) < 5.0, text);
        }

        std::snprintf (text, sizeof text, "%.1f kHz : charge processeur %.2f %% d'un cœur, note affichée %s%d",
                       sr / 1000.0, 100.0 * seconds / (double (total) / sr),
                       DiatonicScale::noteName (h.getDetectedNote()), h.getDetectedNote() / 12 - 1);
        check (seconds < 0.1 * double (total) / sr && h.getDetectedNote() == 67, text);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::printf ("== Paramètres\n");
    {
        HarmonizerProcessor h;
        auto* deg = h.parameters.getParameter ("adegrees");
        auto name = [deg] (int n) { return deg->getText (deg->convertTo0to1 ((float) n), 20); };

        check (name (2) == "+3rd" && name (-4) == "-5th" && name (7) == "+Oct" && name (0) == "Unison"
                 && name (14) == "+2 Oct" && name (1) == "+2nd",
               ("Noms des intervalles : " + name (2) + ", " + name (-4) + ", " + name (7) + ", "
                  + name (0) + ", " + name (14) + ", " + name (1)).toRawUTF8());

        check (juce::approximatelyEqual (deg->getValueForText ("+3rd"), deg->convertTo0to1 (2.0f))
                 && juce::approximatelyEqual (deg->getValueForText ("-Oct"), deg->convertTo0to1 (-7.0f)),
               "Saisie d'un intervalle au clavier (+3rd, -Oct)");

        check (! h.isDiatonic(), "Par défaut : mode Chromatic (anciennes sessions inchangées)");

        h.setCurrentProgram (presetNamed (h, "Diatonic: 3rds & 6ths (G major)"));
        juce::MemoryBlock state;
        h.getStateInformation (state);

        HarmonizerProcessor h2;
        h2.setStateInformation (state.getData(), (int) state.getSize());
        auto* key = h2.parameters.getParameter ("key");
        check (h2.isDiatonic() && key->getCurrentValueAsText() == "G"
                 && h2.parameters.getParameter ("bdegrees")->getCurrentValueAsText() == "+6th",
               "Preset G major enregistré puis relu : Diatonic, G, voix B en +6th");
    }

    std::printf ("== Phrase jouée dans le preset Twin Lead\n");
    runPhrase (48000.0, 256);
    runPhrase (44100.0, 512);
    runPhrase (48000.0, 32);

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
