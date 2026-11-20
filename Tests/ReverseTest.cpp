// Tests du moteur Reverse Shift (sans JUCE). Cible Xcode : ReverseTest
#include "ReverseEngine.h"

#include <cstdio>

namespace
{
    int failures = 0;
    constexpr double pi = 3.14159265358979323846;

    void check (bool ok, const char* text)
    {
        std::printf ("%-72s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    double energyAt (const std::vector<float>& x, size_t from, size_t to, double freq, double sr)
    {
        const double w = 2.0 * pi * freq / sr, coeff = 2.0 * std::cos (w);
        const size_t len = to - from;
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * pi * double (i) / double (len - 1));
            const double s0 = x[from + i] * hann + coeff * s1 - s2;
            s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    double measure (const std::vector<float>& x, size_t from, size_t to, double around, double sr)
    {
        double best = 0.0, bestE = -1.0;
        for (double c = -300.0; c <= 300.0; c += 0.5)
        {
            const double f = around * std::pow (2.0, c / 1200.0);
            const double e = energyAt (x, from, to, f, sr);
            if (e > bestE) { bestE = e; best = f; }
        }
        return best;
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i)
            s += (double) x[i] * x[i];
        return std::sqrt (s / double (to - from));
    }
}

int main()
{
    char text[160];

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.1f kHz\n", sr / 1000.0);

        // Hauteur : une note tenue, rejouée à l'envers avec différentes transpositions
        for (int semis : { 0, 7, 12, -12, -5 })
        {
            ReverseEngine e;
            e.prepare (sr, 2.0);
            const float ratio = std::pow (2.0f, semis / 12.0f);
            const float length = (float) (0.4 * sr);

            std::vector<float> out ((size_t) (sr * 3));
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = e.process ((float) (0.5 * std::sin (2.0 * pi * 330.0 * double (i) / sr)), length, ratio);

            // Mesure au milieu de chaque tranche de la 1re lecture : l'autre lecture y est muette
            // (sur une longue fenêtre, les raccords entre tranches brouilleraient la mesure)
            const double expected = 330.0 * ratio;
            double worst = 0.0;
            for (int k = 2; k < 7; ++k)
            {
                const size_t centre = (size_t) ((k + 0.5) * length), half = (size_t) (0.03 * sr);
                const double got = measure (out, centre - half, centre + half, expected, sr);
                const double e = 1200.0 * std::log2 (got / expected);
                if (std::abs (e) > std::abs (worst))
                    worst = e;
            }
            const double err = worst;
            const double levelDb = 20.0 * std::log10 (rms (out, (size_t) sr, out.size()) / (0.5 / std::sqrt (2.0)));

            std::snprintf (text, sizeof text, "%+3d demi-tons : %.1f Hz attendu, écart %+.1f cents, niveau %+.1f dB",
                           semis, expected, err, levelDb);
            check (std::abs (err) < 5.0 && std::abs (levelDb) < 4.0, text);
        }

        // À l'envers : une note pincée (attaque nette, extinction lente) doit revenir
        // « aspirée » : le son monte puis s'arrête net, au lieu d'attaquer puis de s'éteindre
        {
            ReverseEngine e;
            e.prepare (sr, 2.0);
            const float length = (float) (0.5 * sr);
            const size_t pluckAt = (size_t) (0.1 * sr);

            std::vector<float> in ((size_t) (sr * 1.5), 0.0f), out (in.size());
            for (size_t i = pluckAt; i < in.size(); ++i)
            {
                const double t = double (i - pluckAt) / sr;
                in[i] = (float) (0.8 * std::exp (-t / 0.08) * std::sin (2.0 * pi * 440.0 * t));
            }
            for (size_t i = 0; i < in.size(); ++i)
                out[i] = e.process (in[i], length, 1.0f);

            // Le moment le plus fort de la sortie, après le pincement
            size_t peak = pluckAt;
            const size_t win = (size_t) (0.005 * sr);
            double best = 0.0;
            for (size_t i = pluckAt + win; i + win < out.size(); i += win)
            {
                const double r = rms (out, i, i + win);
                if (r > best) { best = r; peak = i; }
            }

            const size_t span = (size_t) (0.04 * sr);
            const double before = rms (out, peak - span, peak);
            const double after  = rms (out, peak + 2 * win, peak + 2 * win + span);

            std::snprintf (text, sizeof text, "Note pincée rejouée à l'envers : %.0f fois plus de son avant le pic qu'après",
                           before / std::max (after, 1e-9));
            check (before > 4.0 * after, text);
        }
    }

    // Silence et stabilité
    {
        ReverseEngine e;
        e.prepare (48000.0, 2.0);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
            peak = std::max (peak, std::abs (e.process (0.0f, 20000.0f, 1.5f)));
        check (peak == 0.0f, "Silence en entrée : silence en sortie");

        // Changement de longueur en cours de route : pas de valeur aberrante
        ReverseEngine f;
        f.prepare (48000.0, 2.0);
        bool finite = true;
        float maxOut = 0.0f;
        for (int i = 0; i < 48000 * 4; ++i)
        {
            const float len = 2400.0f + 40000.0f * (float) (0.5 + 0.5 * std::sin (i * 0.0001));
            const float y = f.process ((float) std::sin (i * 0.05), len, 1.3f);
            finite = finite && std::isfinite (y);
            maxOut = std::max (maxOut, std::abs (y));
        }
        std::snprintf (text, sizeof text, "Longueur qui varie sans cesse : sortie finie, crête %.2f (entrée 1.00)", maxOut);
        check (finite && maxOut < 2.0f, text);
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
