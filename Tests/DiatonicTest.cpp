// Tests du Diatonic Shift (sans JUCE) : gammes, détection de hauteur, et chaîne complète
// détection → intervalle dans la gamme → pitch shifter. Cible Xcode : DiatonicTest
#include "DiatonicScale.h"
#include "PitchDetector.h"
#include "PitchShifter.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* text)
    {
        std::printf ("%-70s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    const DiatonicScale::Scale& scaleNamed (const char* name)
    {
        for (const auto& s : DiatonicScale::scales())
            if (std::string (s.name) == name)
                return s;
        return DiatonicScale::scales()[0];
    }

    // Note « de guitare » : 6 harmoniques décroissantes
    float tone (double f, double t)
    {
        double y = 0.0;
        for (int h = 1; h <= 6; ++h)
            y += std::sin (2.0 * M_PI * f * h * t) / h;
        return (float) (0.3 * y);
    }

    double energyAt (const std::vector<float>& x, size_t from, size_t to, double freq, double sr)
    {
        const double w = 2.0 * M_PI * freq / sr, coeff = 2.0 * std::cos (w);
        const size_t len = to - from;
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * M_PI * double (i) / double (len - 1));
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
}

int main()
{
    using namespace DiatonicScale;
    char text[160];

    std::printf ("== Gammes\n");
    {
        const auto& major = scaleNamed ("Major");
        const auto& minor = scaleNamed ("Natural Minor");
        const auto& penta = scaleNamed ("Minor Pentatonic");

        struct Case { const char* what; int note, key; const Scale* s; int degrees, expected; };
        const Case cases[] = {
            { "Do majeur : mi + tierce = sol (tierce mineure)",       64, 0, &major, +2, +3 },
            { "Do majeur : do + tierce = mi (tierce majeure)",        60, 0, &major, +2, +4 },
            { "Do majeur : si + tierce = ré",                         59, 0, &major, +2, +3 },
            { "Do majeur : fa + quarte = si (triton)",                65, 0, &major, +3, +6 },
            { "Do majeur : la - tierce = fa",                         69, 0, &major, -2, -4 },
            { "Do majeur : ré + octave",                              62, 0, &major, +7, +12 },
            { "Mi mineur : mi + tierce = sol",                        64, 4, &minor, +2, +3 },
            { "Mi mineur : sol + tierce = si",                        67, 4, &minor, +2, +4 },
            { "Mi mineur : fa# + quinte = do (quinte diminuée)",      66, 4, &minor, +4, +6 },
            { "La penta mineure : la + 1 cran = do",                  57, 9, &penta, +1, +3 },
            { "Do majeur : do# (hors gamme) + tierce garde l'écart",  61, 0, &major, +2, +4 },
            { "Do majeur : unisson",                                  64, 0, &major,  0,  0 },
        };

        for (const auto& c : cases)
        {
            const int got = semitonesFor (c.note, c.key, *c.s, c.degrees);
            std::snprintf (text, sizeof text, "%s (%+d demi-tons, attendu %+d)", c.what, got, c.expected);
            check (got == c.expected, text);
        }
    }

    std::printf ("== Détection de hauteur\n");
    for (double sr : { 48000.0, 44100.0, 96000.0 })
    {
        for (double f : { 77.78, 82.41, 110.0, 196.0, 329.63, 659.25, 1046.5 })
        {
            PitchDetector d;
            d.prepare (sr);
            double worst = 0.0;
            int measures = 0, unvoiced = 0;

            for (int i = 0; i < (int) (sr * 0.5); ++i)
                if (d.push (tone (f, i / sr)) && i > sr * 0.05)
                {
                    ++measures;
                    if (! d.isVoiced()) { ++unvoiced; continue; }
                    worst = std::max (worst, std::abs (1200.0 * std::log2 (d.getFrequency() / f)));
                }

            std::snprintf (text, sizeof text, "%5.0f kHz, %7.2f Hz : écart max %.1f cents, %d/%d mesures sans note",
                           sr / 1000.0, f, worst, unvoiced, measures);
            check (worst < 10.0 && unvoiced == 0 && measures > 50, text);
        }
    }

    {
        PitchDetector d;
        d.prepare (48000.0);
        std::mt19937 rng (3);
        std::normal_distribution<float> noise (0.0f, 0.1f);
        int measures = 0, voiced = 0;
        for (int i = 0; i < 48000; ++i)
            if (d.push (noise (rng)))
            {
                ++measures;
                voiced += d.isVoiced() ? 1 : 0;
            }
        std::snprintf (text, sizeof text, "Bruit blanc : %d/%d mesures prises pour une note (doit rester rare)", voiced, measures);
        check (voiced * 10 < measures, text);

        d.reset();
        bool any = false;
        for (int i = 0; i < 48000; ++i)
            if (d.push (0.0f))
                any = any || d.isVoiced();
        check (! any, "Silence : aucune note détectée");
    }

    std::printf ("== Chaîne complète en mi mineur, voix « tierce au-dessus »\n");
    {
        const double sr = 48000.0;
        const auto& minor = scaleNamed ("Natural Minor");
        const int key = 4;   // mi

        // La phrase jouée : la3, sol3, si3, mi4 (0,8 s chacune)
        const int notes[] = { 57, 55, 59, 64 };
        const size_t perNote = (size_t) (sr * 0.8);

        PitchDetector detector;
        detector.prepare (sr);
        NoteTracker tracker;
        PitchShifter shifter;
        shifter.prepare (sr, 0.1, 0.1);
        PitchShifter::Voice voice;

        std::vector<float> out (perNote * 4);
        float ratio = 1.0f;

        for (size_t i = 0; i < out.size(); ++i)
        {
            const double f = midiToHz (notes[i / perNote]);
            const float x = tone (f, i / sr);

            if (detector.push (x))
            {
                tracker.update (detector.isVoiced() ? PitchDetector::toMidi (detector.getFrequency()) : -1.0f);
                if (tracker.getNote() >= 0)
                    ratio = std::pow (2.0f, semitonesFor (tracker.getNote(), key, minor, +2) / 12.0f);
            }

            shifter.write (x);
            out[i] = shifter.process (voice, ratio, (float) (0.010 * sr), (float) (0.050 * sr));
        }

        for (int n = 0; n < 4; ++n)
        {
            const int expectedNote = notes[n] + semitonesFor (notes[n], key, minor, +2);
            const double expected = midiToHz (expectedNote);
            // On mesure après 150 ms : le temps que la note soit reconnue
            const size_t from = (size_t) n * perNote + (size_t) (sr * 0.15), to = (size_t) (n + 1) * perNote;
            const double got = measure (out, from, to, expected, sr);
            const double err = 1200.0 * std::log2 (got / expected);

            std::snprintf (text, sizeof text, "%s%d joué → %s%d attendu (%.1f Hz) : mesuré %.1f Hz, écart %+.1f cents",
                           noteName (notes[n]), notes[n] / 12 - 1, noteName (expectedNote), expectedNote / 12 - 1,
                           expected, got, err);
            check (std::abs (err) < 5.0, text);
        }
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
