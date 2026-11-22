// Tests du moteur de trémolo (sans JUCE). Cible Xcode : TremoloTest
#include "TremoloEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr float pi = 3.14159265f;
    int failures = 0;

    void check (bool ok, const char* name, const char* detail)
    {
        std::printf ("%-50s %s  %s\n", name, ok ? "OK    " : "ÉCHEC ", detail);
        if (! ok)
            ++failures;
    }

    TremoloEngine make (TremoloEngine::Settings s, float rate)
    {
        TremoloEngine e;
        e.prepare (sampleRate);
        e.setSettings (s);
        e.setRate (rate);
        return e;
    }

    std::vector<float> sine (float freq, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = 0.5f * std::sin (2.0f * pi * freq * (float) i / (float) sampleRate);
        return v;
    }

    // Enveloppe : crête sur des fenêtres de 5 ms
    std::vector<float> envelope (const std::vector<float>& v)
    {
        std::vector<float> env;
        for (size_t start = 0; start + 240 <= v.size(); start += 240)
        {
            float p = 0.0f;
            for (size_t i = start; i < start + 240; ++i)
                p = std::max (p, std::abs (v[i]));
            env.push_back (p);
        }
        return env;
    }
}

int main()
{
    char detail[160];
    using Engine = TremoloEngine;

    // 1. Profondeur 0 : le son est intact, dans les trois modes
    {
        float worst = 0.0f;
        for (int mode = 0; mode < Engine::numModes; ++mode)
        {
            Engine::Settings s;
            s.mode = mode;
            s.depth = 0.0f;
            auto e = make (s, 5.0f);
            auto in = sine (440.0f, 48000), l = in, r = in;
            e.process (l.data(), r.data(), (int) l.size());
            for (size_t i = 0; i < in.size(); ++i)
                worst = std::max ({ worst, std::abs (l[i] - in[i]), std::abs (r[i] - in[i]) });
        }
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-5f, "profondeur 0 : son intact (3 modes)", detail);
    }

    // 2. Trémolo sinus à 4 Hz, profondeur 100 % : le volume va de plein à silence, 4 fois par seconde
    {
        Engine::Settings s;
        s.depth = 1.0f;
        s.smooth = 0.0f;
        auto e = make (s, 4.0f);
        std::vector<float> l (96000, 0.5f), r (l);
        e.process (l.data(), r.data(), (int) l.size());
        float lo = 1.0f, hi = 0.0f;
        int peaks = 0;
        for (size_t i = 1; i + 1 < l.size(); ++i)
        {
            lo = std::min (lo, l[i]);
            hi = std::max (hi, l[i]);
            if (l[i] > l[i - 1] && l[i] >= l[i + 1] && l[i] > 0.49f)
                ++peaks;
        }
        std::snprintf (detail, sizeof detail, "de %.3f à %.3f, %d sommets en 2 s", lo, hi, peaks);
        check (lo < 0.01f && hi > 0.49f && peaks >= 7 && peaks <= 9, "trémolo : profondeur et vitesse exactes", detail);
    }

    // 3. Carré sans lissage : pas de clic (le gain ne saute jamais d'un coup)
    {
        Engine::Settings s;
        s.shape = Engine::square;
        s.depth = 1.0f;
        s.smooth = 0.0f;
        auto e = make (s, 8.0f);
        std::vector<float> l (48000, 0.5f), r (l);
        e.process (l.data(), r.data(), (int) l.size());
        float jump = 0.0f;
        for (size_t i = 1; i < l.size(); ++i)
            jump = std::max (jump, std::abs (l[i] - l[i - 1]));
        std::snprintf (detail, sizeof detail, "plus grand saut %.3f (sur 0,5)", jump);
        check (jump < 0.05f, "forme carrée : sans clic", detail);
    }

    // 4. Stereo Phase 180° : quand la gauche est pleine, la droite est au creux (ping-pong)
    {
        Engine::Settings s;
        s.depth = 1.0f;
        s.stereoPhase = 180.0f;
        auto e = make (s, 2.0f);
        std::vector<float> l (48000, 0.5f), r (l);
        e.process (l.data(), r.data(), (int) l.size());
        float worst = 0.0f;
        for (size_t i = 4800; i < l.size(); ++i)
            worst = std::max (worst, std::abs ((l[i] + r[i]) - 0.5f));
        std::snprintf (detail, sizeof detail, "G + D reste à 0,5 à %.3f près", worst);
        check (worst < 0.02f, "stereo phase 180° : ping-pong", detail);
    }

    // 5. Auto-pan : puissance constante, et le son va bien d'un côté à l'autre
    {
        Engine::Settings s;
        s.mode = Engine::autoPan;
        s.depth = 1.0f;
        auto e = make (s, 1.0f);
        std::vector<float> l (96000, 0.5f), r (l);
        e.process (l.data(), r.data(), (int) l.size());
        float powerError = 0.0f, minL = 1.0f, minR = 1.0f;
        for (size_t i = 4800; i < l.size(); ++i)
        {
            powerError = std::max (powerError, std::abs (l[i] * l[i] + r[i] * r[i] - 0.5f));
            minL = std::min (minL, l[i]);
            minR = std::min (minR, r[i]);
        }
        std::snprintf (detail, sizeof detail, "écart de puissance %.3f, minimum G %.3f, D %.3f", powerError, minL, minR);
        check (powerError < 0.01f && minL < 0.02f && minR < 0.02f, "auto-pan : de gauche à droite, volume constant", detail);
    }

    // 6. Harmonique : les graves et les aigus ondulent en opposition
    {
        Engine::Settings s;
        s.mode = Engine::harmonic;
        s.depth = 1.0f;
        auto run = [&] (float freq)
        {
            auto e = make (s, 2.0f);
            auto l = sine (freq, 48000), r = l;
            e.process (l.data(), r.data(), (int) l.size());
            return envelope (l);
        };
        const auto bass = run (80.0f), treble = run (6000.0f);
        double correlation = 0.0, meanB = 0.0, meanT = 0.0;
        for (size_t i = 0; i < bass.size(); ++i)
        {
            meanB += bass[i];
            meanT += treble[i];
        }
        meanB /= (double) bass.size();
        meanT /= (double) treble.size();
        double varB = 0.0, varT = 0.0;
        for (size_t i = 0; i < bass.size(); ++i)
        {
            correlation += (bass[i] - meanB) * (treble[i] - meanT);
            varB += (bass[i] - meanB) * (bass[i] - meanB);
            varT += (treble[i] - meanT) * (treble[i] - meanT);
        }
        correlation /= std::sqrt (varB * varT);
        std::snprintf (detail, sizeof detail, "corrélation graves / aigus %.2f", correlation);
        check (correlation < -0.8, "harmonique : graves et aigus en opposition", detail);
    }

    // 7. Calé sur le morceau : la phase donnée est celle de l'onde
    {
        const bool ok = std::abs (Engine::wave (Engine::sine, 0.0) - 1.0f) < 1.0e-6f
                     && std::abs (Engine::wave (Engine::sine, 0.5)) < 1.0e-6f
                     && std::abs (Engine::wave (Engine::triangle, 1.25) - 0.5f) < 1.0e-6f;
        Engine e;
        e.prepare (sampleRate);
        e.setPhase (3.75);
        check (ok && std::abs (e.getPhase() - 0.75) < 1.0e-9, "formes d'onde et phase calée", "");
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
