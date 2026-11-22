// Tests du moteur Octaver (sans JUCE). Cible Xcode : OctaverTest
#include "Octaver/OctaverEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979;
    int failures = 0;

    void check (bool ok, const char* name, const char* detail)
    {
        std::printf ("%-52s %s  %s\n", name, ok ? "OK    " : "ÉCHEC ", detail);
        if (! ok)
            ++failures;
    }

    // Une ou plusieurs notes (somme de sinus), 1 seconde
    std::vector<float> tones (std::initializer_list<float> freqs, int n = 48000, float amplitude = 0.4f)
    {
        std::vector<float> v ((size_t) n, 0.0f);
        for (float f : freqs)
            for (int i = 0; i < n; ++i)
                v[(size_t) i] += amplitude * (float) std::sin (2.0 * pi * f * i / sampleRate);
        return v;
    }

    // Énergie à une fréquence (Goertzel, fenêtre de Hann), sur la seconde moitié du signal
    double energy (const std::vector<float>& v, double freq)
    {
        const size_t start = v.size() / 2, n = v.size() - start;
        const double w = 2.0 * pi * freq / sampleRate, coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1));
            const double s = v[start + i] * hann + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    double db (double ratio) { return 10.0 * std::log10 (ratio + 1.0e-30); }

    std::vector<float> run (OctaverEngine::Settings s, std::vector<float> in)
    {
        OctaverEngine e;
        e.prepare (sampleRate);
        e.setSettings (s);
        auto r = in;
        e.process (in.data(), r.data(), (int) in.size());
        return in;
    }

    OctaverEngine::Settings only (int mode, float sub1, float sub2, float up)
    {
        OctaverEngine::Settings s;
        s.mode = mode;
        s.dry = 0.0f;
        s.sub1 = sub1;
        s.sub2 = sub2;
        s.up = up;
        s.toneHz = 4000.0f;
        return s;
    }
}

int main()
{
    char detail[160];
    using Engine = OctaverEngine;

    // 1. Octaves à zéro : le son direct passe intact
    {
        Engine::Settings s;
        s.sub1 = s.sub2 = s.up = 0.0f;
        const auto in = tones ({ 330.0f });
        const auto out = run (s, in);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - in[i]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-7f, "octaves à zéro : son direct intact", detail);
    }

    // 2. Analog, une octave en dessous : 220 Hz → 110 Hz domine
    {
        const auto out = run (only (Engine::analog, 1, 0, 0), tones ({ 220.0f }));
        const double sub = energy (out, 110.0), fundamental = energy (out, 220.0);
        std::snprintf (detail, sizeof detail, "110 Hz %+.1f dB par rapport à 220 Hz", db (sub / fundamental));
        check (sub > 10.0 * fundamental, "analog −1 : une octave en dessous", detail);
    }

    // 3. Analog, deux octaves en dessous : 220 Hz → 55 Hz domine
    {
        const auto out = run (only (Engine::analog, 0, 1, 0), tones ({ 220.0f }));
        const double sub = energy (out, 55.0), sub1 = energy (out, 110.0), fundamental = energy (out, 220.0);
        std::snprintf (detail, sizeof detail, "55 Hz %+.1f dB / 110 Hz, %+.1f dB / 220 Hz", db (sub / sub1), db (sub / fundamental));
        check (sub > 4.0 * sub1 && sub > 4.0 * fundamental, "analog −2 : deux octaves en dessous", detail);
    }

    // 4. Analog, une octave au-dessus : 220 Hz → 440 Hz domine
    {
        const auto out = run (only (Engine::analog, 0, 0, 1), tones ({ 220.0f }));
        const double up = energy (out, 440.0), fundamental = energy (out, 220.0);
        std::snprintf (detail, sizeof detail, "440 Hz %+.1f dB par rapport à 220 Hz", db (up / fundamental));
        check (up > 10.0 * fundamental, "analog +1 : une octave au-dessus", detail);
    }

    // 5. Poly : un accord (200 + 300 Hz) donne les deux octaves en dessous (100 et 150 Hz)
    {
        const auto out = run (only (Engine::poly, 1, 0, 0), tones ({ 200.0f, 300.0f }, 48000, 0.25f));
        const double a = energy (out, 100.0), b = energy (out, 150.0), between = energy (out, 125.0);
        std::snprintf (detail, sizeof detail, "100 Hz %+.1f dB, 150 Hz %+.1f dB (par rapport à 125 Hz)", db (a / between), db (b / between));
        check (a > 30.0 * between && b > 30.0 * between, "poly −1 : un accord reste juste", detail);
    }

    // 6. Poly, deux octaves en dessous et une au-dessus
    {
        const auto down = run (only (Engine::poly, 0, 1, 0), tones ({ 220.0f }));
        const auto up = run (only (Engine::poly, 0, 0, 1), tones ({ 220.0f }));
        const double d = energy (down, 55.0) / energy (down, 220.0), u = energy (up, 440.0) / energy (up, 220.0);
        std::snprintf (detail, sizeof detail, "55 Hz %+.1f dB, 440 Hz %+.1f dB (par rapport à 220 Hz)", db (d), db (u));
        check (d > 10.0 && u > 10.0, "poly −2 et +1", detail);
    }

    // 7. Silence (bruit très faible) : la bascule ne crépite pas
    {
        std::vector<float> noise (48000);
        unsigned seed = 1;
        for (auto& v : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            v = 1.0e-5f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
        const auto out = run (only (Engine::analog, 1, 1, 1), noise);
        float peak = 0.0f;
        for (auto v : out)
            peak = std::max (peak, std::abs (v));
        std::snprintf (detail, sizeof detail, "crête %.2e pour un bruit de 1e-5", peak);
        check (peak < 1.0e-4f, "silence : pas de crépitement", detail);
    }

    // 8. Tone : un réglage bas arrondit le grain (moins d'harmoniques au-dessus de l'octave)
    {
        auto bright = only (Engine::analog, 1, 0, 0), dark = bright;
        bright.toneHz = 8000.0f;
        dark.toneHz = 300.0f;
        const auto a = run (bright, tones ({ 220.0f })), b = run (dark, tones ({ 220.0f }));
        const double ra = energy (a, 990.0) / energy (a, 110.0), rb = energy (b, 990.0) / energy (b, 110.0);
        std::snprintf (detail, sizeof detail, "990 Hz / 110 Hz : %+.1f dB → %+.1f dB", db (ra), db (rb));
        check (rb < ra * 0.1, "tone : passe-bas sur les octaves", detail);
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
