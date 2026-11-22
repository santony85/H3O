// Tests du moteur de phaser (sans JUCE). Cible Xcode : PhaserTest
#include "PhaserEngine.h"

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
        std::printf ("%-52s %s  %s\n", name, ok ? "OK    " : "ÉCHEC ", detail);
        if (! ok)
            ++failures;
    }

    std::vector<float> sine (float freq, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = 0.5f * std::sin (2.0f * pi * freq * (float) i / (float) sampleRate);
        return v;
    }

    float rms (const std::vector<float>& v, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double) v[i] * v[i];
        return (float) std::sqrt (sum / (double) (to - from));
    }

    float db (float ratio) { return 20.0f * std::log10 (std::max (ratio, 1.0e-9f)); }

    // Gain (dB) d'une sinusoïde à travers un phaser figé (profondeur 0) centré sur centreHz
    float gainAt (float freq, int stages, float centreHz, float feedback = 0.0f)
    {
        PhaserEngine e;
        e.prepare (sampleRate);
        PhaserEngine::Settings s;
        s.stages = stages;
        s.depth = 0.0f;
        s.centreHz = centreHz;
        s.feedback = feedback;
        e.setSettings (s);
        auto in = sine (freq, 48000), l = in, r = in;
        e.process (l.data(), r.data(), (int) l.size());
        return db (rms (l, 24000, 48000) / rms (in, 24000, 48000));
    }
}

int main()
{
    char detail[160];
    using Engine = PhaserEngine;

    // 1. Mix 0 : le son est intact
    {
        Engine e;
        e.prepare (sampleRate);
        Engine::Settings s;
        s.mix = 0.0f;
        e.setSettings (s);
        e.setRate (1.0f);
        auto in = sine (440.0f, 48000), l = in, r = in;
        e.process (l.data(), r.data(), (int) l.size());
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max ({ worst, std::abs (l[i] - in[i]), std::abs (r[i] - in[i]) });
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-6f, "mix 0 : son intact", detail);
    }

    // 2. 4 étages figés à 1 kHz : creux à 1 000 × tan 22,5° ≈ 414 Hz et × tan 67,5° ≈ 2 414 Hz, rien à 1 kHz
    {
        const float notchLow = gainAt (414.2f, 4, 1000.0f), notchHigh = gainAt (2414.2f, 4, 1000.0f);
        const float centre = gainAt (1000.0f, 4, 1000.0f);
        std::snprintf (detail, sizeof detail, "414 Hz %.1f dB, 2414 Hz %.1f dB, 1 kHz %.1f dB", notchLow, notchHigh, centre);
        check (notchLow < -30.0f && notchHigh < -30.0f && centre > -0.5f, "4 étages : deux creux aux bonnes fréquences", detail);
    }

    // 3. 2 étages : un seul creux, au centre
    {
        const float notch = gainAt (1000.0f, 2, 1000.0f), away = gainAt (200.0f, 2, 1000.0f);
        std::snprintf (detail, sizeof detail, "1 kHz %.1f dB, 200 Hz %.1f dB", notch, away);
        check (notch < -30.0f && away > -3.0f, "2 étages : un creux au centre", detail);
    }

    // 4. Le creux se déplace : à 414 Hz, le niveau monte et descend au rythme de l'onde
    {
        Engine e;
        e.prepare (sampleRate);
        Engine::Settings s;
        s.depth = 0.8f;
        s.centreHz = 1000.0f;
        e.setSettings (s);
        e.setRate (1.0f);
        auto l = sine (414.2f, 96000), r = l;
        e.process (l.data(), r.data(), (int) l.size());
        float lo = 1.0f, hi = 0.0f;
        for (size_t start = 0; start + 960 <= l.size(); start += 960)
        {
            const float v = rms (l, start, start + 960);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        std::snprintf (detail, sizeof detail, "niveau de %.1f à %.1f dB", db (lo / 0.3536f), db (hi / 0.3536f));
        check (db (hi / lo) > 20.0f, "balayage : le creux passe et repasse", detail);
    }

    // 5. Feedback : creux plus marqués, pics bornés, jamais d'emballement
    {
        const float peak = gainAt (1000.0f, 4, 1000.0f, 0.9f), peakNeg = gainAt (1000.0f, 4, 1000.0f, -0.9f);
        Engine e;
        e.prepare (sampleRate);
        Engine::Settings s;
        s.stages = 12;
        s.depth = 1.0f;
        s.feedback = 0.9f;
        e.setSettings (s);
        e.setRate (3.0f);
        std::vector<float> l (96000), r (96000);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (i % 4800 == 0) ? 1.0f : 0.0f;   // clics : toutes les fréquences
        e.process (l.data(), r.data(), (int) l.size());
        bool finite = true;
        float maxOut = 0.0f;
        for (auto v : l)
        {
            finite = finite && std::isfinite (v);
            maxOut = std::max (maxOut, std::abs (v));
        }
        std::snprintf (detail, sizeof detail, "+90 %% : %+.1f dB, −90 %% : %+.1f dB au centre, crête %.2f", peak, peakNeg, maxOut);
        check (finite && maxOut < 8.0f && peak > 3.0f, "feedback : résonance stable", detail);
    }

    // 6. Stereo Phase 180° : gauche et droite différentes ; 0° : identiques
    {
        auto run = [] (float stereoPhase)
        {
            Engine e;
            e.prepare (sampleRate);
            Engine::Settings s;
            s.stereoPhase = stereoPhase;
            s.depth = 0.8f;
            e.setSettings (s);
            e.setRate (0.5f);
            auto l = sine (500.0f, 48000), r = l;
            e.process (l.data(), r.data(), (int) l.size());
            float d = 0.0f;
            for (size_t i = 0; i < l.size(); ++i)
                d = std::max (d, std::abs (l[i] - r[i]));
            return d;
        };
        const float same = run (0.0f), wide = run (180.0f);
        std::snprintf (detail, sizeof detail, "écart G/D : 0° %.2e, 180° %.2f", same, wide);
        check (same < 1.0e-6f && wide > 0.1f, "stereo phase : G et D décalés", detail);
    }

    // 7. Calé sur le morceau
    {
        Engine e;
        e.prepare (sampleRate);
        e.setPhase (2.25);
        const bool ok = std::abs (e.getPhase() - 0.25) < 1.0e-9
                     && std::abs (Engine::wave (Engine::sine, 0.0)) < 1.0e-6f
                     && std::abs (Engine::wave (Engine::triangle, 0.5) - 1.0f) < 1.0e-6f;
        check (ok, "formes d'onde et phase calée", "");
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
