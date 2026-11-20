// Tests du moteur Ultra-Tap (sans JUCE). Cible Xcode : UltraTapTest
#include "UltraTapEngine.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* text)
    {
        std::printf ("%-74s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    struct Hit { size_t at; float left, right; };

    // Envoie une impulsion et relève les échos qui sortent (pics séparés d'au moins 2 ms)
    std::vector<Hit> impulseResponse (const UltraTapEngine::Settings& s, double sr, float feedback = 0.0f, double seconds = 3.0)
    {
        UltraTapEngine e;
        e.prepare (sr, 3.0);
        e.setSettings (s);

        std::vector<Hit> hits;
        const size_t n = (size_t) (sr * seconds);
        for (size_t i = 0; i < n; ++i)
        {
            float l, r;
            e.process (i == 0 ? 1.0f : 0.0f, feedback, l, r);
            if (std::max (std::abs (l), std::abs (r)) > 0.02f)
            {
                if (hits.empty() || i > hits.back().at + (size_t) (0.002 * sr))
                    hits.push_back ({ i, l, r });
                else if (std::abs (l) + std::abs (r) > std::abs (hits.back().left) + std::abs (hits.back().right))
                    hits.back() = { i, l, r };
            }
        }
        return hits;
    }
}

int main()
{
    char text[200];

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.1f kHz\n", sr / 1000.0);

        // Espacement régulier : 8 échos, un tous les 75 ms après 20 ms de pre-delay
        {
            UltraTapEngine::Settings s;
            s.preDelayMs = 20.0f; s.lengthMs = 600.0f; s.taps = 8;
            const auto hits = impulseResponse (s, sr);

            double worstMs = 0.0;
            for (size_t k = 0; k < hits.size() && k < 8; ++k)
            {
                const double expected = 20.0 + 75.0 * double (k + 1);
                worstMs = std::max (worstMs, std::abs (1000.0 * double (hits[k].at) / sr - expected));
            }
            std::snprintf (text, sizeof text, "Régulier : %zu échos (8 attendus), écart de position max %.2f ms",
                           hits.size(), worstMs);
            check (hits.size() == 8 && worstMs < 0.1, text);
        }

        // Slope positif : les écarts se resserrent ; négatif : ils s'allongent
        for (float slope : { 0.7f, -0.7f })
        {
            UltraTapEngine::Settings s;
            s.lengthMs = 1000.0f; s.taps = 10; s.slope = slope;
            const auto hits = impulseResponse (s, sr);

            bool monotonic = hits.size() == 10;
            for (size_t k = 2; monotonic && k < hits.size(); ++k)
            {
                const auto gapBefore = (double) (hits[k - 1].at - hits[k - 2].at);
                const auto gap       = (double) (hits[k].at - hits[k - 1].at);
                monotonic = slope > 0 ? gap < gapBefore : gap > gapBefore;
            }
            std::snprintf (text, sizeof text, "Slope %+.1f : écarts toujours %s, dernier écho à %.0f ms",
                           slope, slope > 0 ? "plus courts (rebond)" : "plus longs (ralenti)",
                           hits.empty() ? 0.0 : 1000.0 * double (hits.back().at) / sr);
            check (monotonic && std::abs (1000.0 * double (hits.back().at) / sr - 1000.0) < 0.5, text);
        }

        // Taper : volume qui baisse ou qui monte
        for (float taper : { -1.0f, 1.0f })
        {
            UltraTapEngine::Settings s;
            s.lengthMs = 800.0f; s.taps = 6; s.taper = taper;
            const auto hits = impulseResponse (s, sr);

            bool ok = hits.size() == 6;
            for (size_t k = 1; ok && k < hits.size(); ++k)
            {
                const float before = std::abs (hits[k - 1].left) + std::abs (hits[k - 1].right);
                const float now    = std::abs (hits[k].left) + std::abs (hits[k].right);
                ok = taper < 0 ? now < before : now > before;
            }
            std::snprintf (text, sizeof text, "Taper %+.0f : échos de plus en plus %s", taper, taper < 0 ? "faibles" : "forts");
            check (ok, text);
        }

        // Spread 100 % : les échos alternent gauche / droite
        {
            UltraTapEngine::Settings s;
            s.lengthMs = 400.0f; s.taps = 4; s.spread = 1.0f;
            const auto hits = impulseResponse (s, sr);
            bool ok = hits.size() == 4;
            for (size_t k = 0; ok && k < hits.size(); ++k)
                ok = (k % 2 == 0) ? (hits[k].right == 0.0f && hits[k].left > 0.1f)
                                  : (hits[k].left == 0.0f && hits[k].right > 0.1f);
            check (ok, "Spread 100 % : gauche, droite, gauche, droite");
        }

        // Feedback : la série se répète, de plus en plus faible, sans s'emballer
        {
            UltraTapEngine::Settings s;
            s.lengthMs = 300.0f; s.taps = 3;
            UltraTapEngine e;
            e.prepare (sr, 3.0);
            e.setSettings (s);
            float peakEarly = 0.0f, peakLate = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < (size_t) (sr * 20); ++i)
            {
                float l, r;
                e.process (i == 0 ? 1.0f : 0.0f, 0.9f, l, r);
                finite = finite && std::isfinite (l) && std::isfinite (r);
                (i < (size_t) (sr * 2) ? peakEarly : peakLate) = std::max (i < (size_t) (sr * 2) ? peakEarly : peakLate, std::abs (l));
            }
            std::snprintf (text, sizeof text, "Feedback 90 %% : crête %.2f au début, %.4f après 2 s, sans emballement",
                           peakEarly, peakLate);
            check (finite && peakLate < peakEarly && peakEarly < 2.0f, text);
        }
    }

    // 24 taps très serrés sur un son continu : pas de saturation
    {
        UltraTapEngine::Settings s;
        s.lengthMs = 20.0f; s.taps = 24;
        UltraTapEngine e;
        e.prepare (48000.0, 3.0);
        e.setSettings (s);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            float l, r;
            e.process ((float) std::sin (i * 0.01), 0.0f, l, r);   // ~76 Hz, quasi en phase sur 20 ms
            peak = std::max (peak, std::abs (l));
        }
        std::snprintf (text, sizeof text, "24 taps sur 20 ms : crête %.2f pour une entrée à 1.00", peak);
        check (peak < 2.5f, text);
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
