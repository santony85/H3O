// Tests du moteur de flanger (sans JUCE). Cible Xcode : FlangerTest
#include "FlangerEngine.h"

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

    FlangerEngine make (FlangerEngine::Settings s, float rate)
    {
        FlangerEngine e;
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

    float peakFrom (const std::vector<float>& v, size_t start)
    {
        float p = 0.0f;
        for (size_t i = start; i < v.size(); ++i)
            p = std::max (p, std::abs (v[i]));
        return p;
    }
}

int main()
{
    char detail[160];
    using Engine = FlangerEngine;

    // 1. Mix 0 : le son est intact
    {
        Engine::Settings s;
        s.mix = 0.0f;
        auto e = make (s, 0.5f);
        auto in = sine (440.0f, 48000), l = in, r = in;
        e.process (l.data(), r.data(), (int) l.size());
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max ({ worst, std::abs (l[i] - in[i]), std::abs (r[i] - in[i]) });
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-6f, "mix 0 : son intact", detail);
    }

    // 2. Retard fixe de 1 ms, Mix 50 % : un creux à 500 Hz, un sommet à 1 kHz (le peigne)
    {
        auto run = [] (float freq)
        {
            Engine::Settings s;
            s.depth = 0.0f;
            s.manualMs = 1.0f;
            s.feedback = 0.0f;
            s.mix = 0.5f;
            auto e = make (s, 0.0f);
            auto l = sine (freq, 48000), r = l;
            e.process (l.data(), r.data(), (int) l.size());
            return peakFrom (l, 24000);
        };
        const float notch = run (500.0f), peak = run (1000.0f);
        std::snprintf (detail, sizeof detail, "500 Hz : %.3f, 1 kHz : %.3f (entrée 0,5)", notch, peak);
        check (notch < 0.02f && std::abs (peak - 0.5f) < 0.02f, "peigne : creux et sommets aux bonnes fréquences", detail);
    }

    // 3. Feedback 95 % avec balayage : le son reste borné
    {
        Engine::Settings s;
        s.feedback = 0.95f;
        s.depth = 1.0f;
        auto e = make (s, 0.3f);
        auto l = sine (220.0f, 96000), r = l;
        e.process (l.data(), r.data(), (int) l.size());
        bool finite = true;
        for (auto v : l)
            finite = finite && std::isfinite (v);
        const float p = peakFrom (l, 0);
        std::snprintf (detail, sizeof detail, "crête %.2f", p);
        check (finite && p < 6.0f, "feedback 95 % : borné", detail);
    }

    // 4. Through-Zero, profondeur 0 : l'effet et le son direct sont alignés, la sortie = l'entrée retardée de Manual
    {
        Engine::Settings s;
        s.mode = Engine::throughZero;
        s.depth = 0.0f;
        s.manualMs = 2.0f;
        s.feedback = 0.0f;
        auto e = make (s, 0.0f);
        auto in = sine (1000.0f, 48000), l = in, r = in;
        e.process (l.data(), r.data(), (int) l.size());
        const size_t delay = 96;   // 2 ms
        float worst = 0.0f;
        for (size_t i = 1000; i < in.size(); ++i)
            worst = std::max (worst, std::abs (l[i] - in[i - delay]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-3f, "through-zero : son direct et effet alignés", detail);
    }

    // 5. Through-Zero : le retard de l'effet traverse celui du son direct (de 0 à 2 × Manual)
    {
        Engine::Settings s;
        s.mode = Engine::throughZero;
        s.depth = 1.0f;
        s.manualMs = 3.0f;
        Engine e;
        e.prepare (sampleRate);
        e.setSettings (s);
        const bool ok = std::abs (e.delayMs (0.0f)) < 1.0e-6f && std::abs (e.delayMs (0.5f) - 3.0f) < 1.0e-6f
                     && std::abs (e.delayMs (1.0f) - 6.0f) < 1.0e-6f;
        std::snprintf (detail, sizeof detail, "%.2f / %.2f / %.2f ms", e.delayMs (0.0f), e.delayMs (0.5f), e.delayMs (1.0f));
        check (ok, "through-zero : balayage de 0 à 2 × Manual", detail);
    }

    // 6. Stereo Phase 180° : à gauche le retard monte quand il descend à droite
    {
        const float a = Engine::wave (Engine::triangle, 0.1), b = Engine::wave (Engine::triangle, 0.1 + 0.5);
        std::snprintf (detail, sizeof detail, "gauche %.2f, droite %.2f", a, b);
        check (std::abs (a + b - 1.0f) < 1.0e-6f, "stereo phase 180° : ondes opposées", detail);
    }

    // 7. Calé sur le morceau : la phase donnée est celle de l'onde
    {
        Engine e;
        e.prepare (sampleRate);
        e.setPhase (5.25);
        check (std::abs (e.getPhase() - 0.25) < 1.0e-9 && std::abs (Engine::wave (Engine::sine, 0.5) - 1.0f) < 1.0e-6f,
               "formes d'onde et phase calée", "");
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
