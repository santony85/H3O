// Tests du moteur Whammy (sans JUCE). Cible Xcode : WhammyTest
#include "Whammy/WhammyEngine.h"

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

    std::vector<float> sine (float freq, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = 0.5f * std::sin (2.0f * pi * freq * (float) i / (float) sampleRate);
        return v;
    }

    // Fréquence par comptage des passages à zéro (montants), sur la fin du signal
    float frequency (const std::vector<float>& v, size_t start)
    {
        int crossings = 0;
        size_t first = 0, last = 0;
        for (size_t i = start + 1; i < v.size(); ++i)
            if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            {
                if (crossings == 0)
                    first = i;
                last = i;
                ++crossings;
            }
        return crossings > 1 ? (float) ((crossings - 1) * sampleRate / (double) (last - first)) : 0.0f;
    }

    std::vector<float> run (WhammyEngine::Settings s, float freq, std::vector<float>* right = nullptr)
    {
        WhammyEngine e;
        e.prepare (sampleRate);
        e.setSettings (s);
        auto l = sine (freq, 48000), r = l;
        e.process (l.data(), r.data(), (int) l.size());
        if (right != nullptr)
            *right = r;
        return l;
    }
}

int main()
{
    char detail[160];
    using Engine = WhammyEngine;

    // 1. Pédale au talon sur l'unisson : le son passe intact (pas de retard, pas de peigne)
    {
        Engine::Settings s;
        s.heel = 0.0f;
        s.pedal = 0.0f;
        auto in = sine (330.0f, 48000);
        auto out = run (s, 330.0f);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - in[i]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-6f, "talon à l'unisson : son intact", detail);
    }

    // 2. Pointe à +12 : une octave au-dessus
    {
        Engine::Settings s;
        s.pedal = 1.0f;
        const float f = frequency (run (s, 220.0f), 24000);
        std::snprintf (detail, sizeof detail, "220 Hz → %.1f Hz", f);
        check (std::abs (f - 440.0f) < 3.0f, "pointe +12 : octave au-dessus", detail);
    }

    // 3. Pointe à −12 : une octave en dessous
    {
        Engine::Settings s;
        s.toe = -12.0f;
        s.pedal = 1.0f;
        const float f = frequency (run (s, 440.0f), 24000);
        std::snprintf (detail, sizeof detail, "440 Hz → %.1f Hz", f);
        check (std::abs (f - 220.0f) < 2.0f, "pointe −12 : octave en dessous", detail);
    }

    // 4. Pédale à mi-course entre 0 et +12 : un triton (rapport √2)
    {
        Engine::Settings s;
        s.pedal = 0.5f;
        const float f = frequency (run (s, 200.0f), 24000);
        std::snprintf (detail, sizeof detail, "200 Hz → %.1f Hz (attendu 282,8)", f);
        check (std::abs (f - 282.84f) < 3.0f, "mi-course : intervalle intermédiaire", detail);
    }

    // 5. Glide : la note met le temps réglé pour suivre la pédale
    {
        Engine e;
        e.prepare (sampleRate);
        Engine::Settings s;
        s.glideMs = 100.0f;
        e.setSettings (s);
        std::vector<float> l (2400, 0.0f), r (l);   // 50 ms
        s.pedal = 1.0f;
        e.setSettings (s);
        e.process (l.data(), r.data(), (int) l.size());
        const float halfway = e.currentSemitones();
        std::vector<float> more (48000, 0.0f), moreR (more);
        e.process (more.data(), moreR.data(), (int) more.size());
        std::snprintf (detail, sizeof detail, "après 50 ms : %.1f, après 1 s : %.2f demi-tons", halfway, e.currentSemitones());
        check (halfway > 3.0f && halfway < 8.0f && std::abs (e.currentSemitones() - 12.0f) < 0.01f, "glide : glissé progressif", detail);
    }

    // 6. Detune : la gauche un peu plus haut, la droite un peu plus bas
    {
        Engine::Settings s;
        s.mode = Engine::detune;
        s.detuneCents = 20.0f;
        std::vector<float> r;
        const auto l = run (s, 440.0f, &r);
        const float fl = frequency (l, 24000), fr = frequency (r, 24000);
        const float expectedUp = 440.0f * std::exp2 (20.0f / 1200.0f), expectedDown = 440.0f * std::exp2 (-20.0f / 1200.0f);
        std::snprintf (detail, sizeof detail, "gauche %.1f Hz, droite %.1f Hz", fl, fr);
        check (std::abs (fl - expectedUp) < 1.5f && std::abs (fr - expectedDown) < 1.5f, "detune : deux voix désaccordées", detail);
    }

    // 7. Mix 0 : le son direct seul, même pédale enfoncée
    {
        Engine::Settings s;
        s.pedal = 1.0f;
        s.mix = 0.0f;
        auto in = sine (330.0f, 48000);
        auto out = run (s, 330.0f);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - in[i]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", worst);
        check (worst < 1.0e-6f, "mix 0 : son direct seul", detail);
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
