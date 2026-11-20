// Tests du moteur de chorus (sans JUCE). Cible Xcode : ChorusTest
#include "ChorusEngine.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    constexpr double sampleRate = 48000.0;
    int failures = 0;

    void check (bool ok, const char* name, const char* detail)
    {
        std::printf ("%-48s %s  %s\n", name, ok ? "OK    " : "ÉCHEC ", detail);
        if (! ok)
            ++failures;
    }

    ChorusEngine makeEngine (int voices, float rateHz)
    {
        ChorusEngine e;
        e.setVoices (voices);
        e.prepare (sampleRate, 0.06);
        e.setRate (rateHz);
        return e;
    }
}

int main()
{
    char detail[128];

    // 1. Sans modulation, une voix = un simple retard de "Delay" échantillons
    {
        auto e = makeEngine (1, 1.0f);
        const float base = 480.0f;   // 10 ms
        int peakAt = -1;
        float peak = 0.0f;

        for (int n = 0; n < 2000; ++n)
        {
            float l, r;
            e.process (n == 0 ? 1.0f : 0.0f, 0.0f, base, 0.0f, 0.0f, l, r);
            if (std::abs (l) > peak) { peak = std::abs (l); peakAt = n; }
        }

        std::snprintf (detail, sizeof detail, "pic à %d éch. (attendu 480), amplitude %.3f", peakAt, peak);
        check (peakAt == 480 && std::abs (peak - 1.0f) < 1e-4f, "Retard fixe (Depth 0)", detail);
    }

    // 2. Le retard suit bien le LFO : entre Delay et Delay + Depth
    {
        auto e = makeEngine (1, 2.0f);
        const float base = 480.0f, depth = 144.0f;   // 10 ms + 3 ms
        int minAt = 1 << 30, maxAt = 0;

        // Une impulsion toutes les 50 ms : on regarde où elle ressort
        for (int pulse = 0; pulse < 40; ++pulse)
        {
            int at = -1;
            float best = 0.0f;
            for (int n = 0; n < 2400; ++n)
            {
                float l, r;
                e.process (n == 0 ? 1.0f : 0.0f, 0.0f, base, depth, 0.0f, l, r);
                if (std::abs (l) > best) { best = std::abs (l); at = n; }
            }
            minAt = std::min (minAt, at);
            maxAt = std::max (maxAt, at);
        }

        std::snprintf (detail, sizeof detail, "retard de %d à %d éch. (attendu ~480 à ~624)", minAt, maxAt);
        check (minAt >= 478 && minAt <= 495 && maxAt >= 610 && maxAt <= 626, "Balayage du retard", detail);
    }

    // 3. Gauche et droite sont décorrélées (LFO en quadrature) : vraie largeur stéréo
    {
        auto e = makeEngine (2, 0.6f);
        std::mt19937 rng (1);
        std::normal_distribution<float> noise (0.0f, 0.3f);
        double lr = 0, ll = 0, rr = 0;

        for (int n = 0; n < (int) sampleRate * 5; ++n)
        {
            const float x = noise (rng);
            float l, r;
            e.process (x, x, 576.0f, 192.0f, 0.0f, l, r);
            lr += (double) l * r; ll += (double) l * l; rr += (double) r * r;
        }

        const double corr = lr / std::sqrt (ll * rr);
        std::snprintf (detail, sizeof detail, "corrélation G/D %.2f (doit être < 0.9)", corr);
        check (corr < 0.9, "Largeur stéréo", detail);
    }

    // 4. Stable avec le feedback au maximum, 4 voix, positif et négatif
    for (float fb : { 0.8f, -0.8f })
    {
        auto e = makeEngine (4, 0.3f);
        std::mt19937 rng (2);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        float maxOut = 0.0f;
        bool finite = true;

        for (int n = 0; n < (int) sampleRate * 20; ++n)
        {
            float l, r;
            e.process (noise (rng), noise (rng), 96.0f, 240.0f, fb, l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            maxOut = std::max ({ maxOut, std::abs (l), std::abs (r) });
        }

        std::snprintf (detail, sizeof detail, "max %.2f sur 20 s de bruit", maxOut);
        check (finite && maxOut < 12.0f, fb > 0 ? "Stabilité feedback +80 %" : "Stabilité feedback -80 %", detail);
    }

    // 5. Changer le nombre de voix en cours de route ne fait pas de clic
    {
        auto e = makeEngine (1, 0.5f);
        float prev = 0.0f, maxJump = 0.0f;

        for (int n = 0; n < (int) sampleRate * 2; ++n)
        {
            if (n == (int) sampleRate / 2) e.setVoices (4);
            if (n == (int) sampleRate)     e.setVoices (2);

            float l, r;
            e.process (1.0f, 1.0f, 480.0f, 0.0f, 0.0f, l, r);   // signal continu
            if (n > 2000)
                maxJump = std::max (maxJump, std::abs (l - prev));
            prev = l;
        }

        std::snprintf (detail, sizeof detail, "plus grand saut %.4f (doit être < 0.005)", maxJump);
        check (maxJump < 0.005f, "Changement de voix sans clic", detail);
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
