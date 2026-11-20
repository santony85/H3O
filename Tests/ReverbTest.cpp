// Mesures du moteur de réverbération (sans JUCE). Cible Xcode : ReverbTest
#include "ReverbEngine.h"

#include <cstdio>
#include <random>

namespace
{
    int failures = 0;

    void check (bool ok, const char* text)
    {
        std::printf ("%-80s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    const char* typeName (ReverbEngine::Type t)
    {
        return t == ReverbEngine::Type::room ? "Room" : t == ReverbEngine::Type::hall ? "Hall" : "Plate";
    }

    struct Response { std::vector<float> l, r; };

    Response impulse (const ReverbEngine::Settings& s, double sr, double seconds)
    {
        ReverbEngine e;
        e.prepare (sr);
        e.setSettings (s);
        Response out { std::vector<float> ((size_t) (sr * seconds)), std::vector<float> ((size_t) (sr * seconds)) };
        for (size_t i = 0; i < out.l.size(); ++i)
            e.process (i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, out.l[i], out.r[i]);
        return out;
    }

    // Temps de réverbération mesuré : pente de la décroissance d'énergie (Schroeder) entre -5 et -35 dB, × 2
    double measureRT60 (const Response& x, double sr)
    {
        std::vector<double> energy (x.l.size());
        double sum = 0.0;
        for (size_t i = x.l.size(); i-- > 0;)
        {
            sum += (double) x.l[i] * x.l[i] + (double) x.r[i] * x.r[i];
            energy[i] = sum;
        }
        const double total = energy[0];
        size_t t5 = 0, t35 = 0;
        for (size_t i = 0; i < energy.size(); ++i)
        {
            const double db = 10.0 * std::log10 (energy[i] / total);
            if (t5 == 0 && db <= -5.0) t5 = i;
            if (db <= -35.0) { t35 = i; break; }
        }
        return t35 > t5 ? 2.0 * double (t35 - t5) / sr : 0.0;
    }
}

int main()
{
    char text[200];
    using T = ReverbEngine::Type;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("== %.1f kHz\n", sr / 1000.0);

        // La durée mesurée suit le réglage Decay (aigus non amortis pour une mesure nette)
        for (auto type : { T::room, T::hall, T::plate })
            for (float decay : { 0.8f, 2.5f, 6.0f })
            {
                ReverbEngine::Settings s;
                s.type = type; s.decaySeconds = decay; s.dampHz = 20000.0f; s.preDelayMs = 0.0f;
                const auto x = impulse (s, sr, decay * 1.6 + 0.5);
                const double rt = measureRT60 (x, sr);
                std::snprintf (text, sizeof text, "%-5s Decay %.1f s : mesuré %.2f s (%+.0f %%)",
                               typeName (type), decay, rt, 100.0 * (rt / decay - 1.0));
                check (std::abs (rt / decay - 1.0) < 0.2, text);
            }
    }

    std::printf ("== Caractère (48 kHz)\n");
    const double sr = 48000.0;

    // Pre-delay : rien avant, puis le son arrive
    {
        ReverbEngine::Settings s;
        s.preDelayMs = 80.0f;
        const auto x = impulse (s, sr, 0.3);
        float before = 0.0f, after = 0.0f;
        for (size_t i = 0; i < x.l.size(); ++i)
        {
            const float a = std::max (std::abs (x.l[i]), std::abs (x.r[i]));
            (i < (size_t) (0.079 * sr) ? before : after) = std::max (i < (size_t) (0.079 * sr) ? before : after, a);
        }
        std::snprintf (text, sizeof text, "Pre-delay 80 ms : silence avant (%.1e), réverbe après (crête %.2f)", before, after);
        check (before == 0.0f && after > 0.05f, text);
    }

    for (auto type : { T::room, T::hall, T::plate })
    {
        ReverbEngine::Settings s;
        s.type = type; s.decaySeconds = 2.0f; s.preDelayMs = 0.0f;
        const auto x = impulse (s, sr, 2.0);

        // Densité : à 150-250 ms la queue doit ressembler à un souffle (kurtosis ~3), pas à des échos isolés
        const size_t a = (size_t) (0.15 * sr), b = (size_t) (0.25 * sr);
        double m2 = 0.0, m4 = 0.0;
        for (size_t i = a; i < b; ++i) { const double v = x.l[i]; m2 += v * v; m4 += v * v * v * v; }
        m2 /= double (b - a); m4 /= double (b - a);
        const double kurtosis = m4 / (m2 * m2);

        // Gauche et droite différentes (largeur stéréo) pour une entrée mono
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = (size_t) (0.05 * sr); i < (size_t) (1.0 * sr); ++i)
        {
            lr += (double) x.l[i] * x.r[i]; ll += (double) x.l[i] * x.l[i]; rr += (double) x.r[i] * x.r[i];
        }
        const double corr = lr / std::sqrt (ll * rr);

        std::snprintf (text, sizeof text, "%-5s : densité de la queue (kurtosis %.1f, souffle = 3), corrélation G/D %+.2f",
                       typeName (type), kurtosis, corr);
        check (kurtosis < 6.0 && std::abs (corr) < 0.4, text);
    }

    // Niveau sur un son continu, et stabilité avec la Decay maximale
    for (auto type : { T::room, T::hall, T::plate })
    {
        ReverbEngine::Settings s;
        s.type = type; s.decaySeconds = 20.0f; s.size = 1.5f; s.modulation = 1.0f; s.diffusion = 1.0f;
        ReverbEngine e;
        e.prepare (sr);
        e.setSettings (s);
        std::mt19937 rng (5);
        std::normal_distribution<float> noise (0.0f, 0.1f);
        double inSum = 0.0, outSum = 0.0;
        float peak = 0.0f;
        bool finite = true;
        const int n = (int) (sr * 30);
        for (int i = 0; i < n; ++i)
        {
            const float x = i < (int) (sr * 10) ? noise (rng) : 0.0f;
            float l, r;
            e.process (x, x, l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            if (i > (int) (sr * 5) && i < (int) (sr * 10)) { inSum += (double) x * x; outSum += 0.5 * ((double) l * l + (double) r * r); }
        }
        std::snprintf (text, sizeof text, "%-5s Decay 20 s, taille max : niveau %+.1f dB sur du bruit, crête %.2f, stable",
                       typeName (type), 10.0 * std::log10 (outSum / inSum), peak);
        check (finite && peak < 4.0f, text);
    }

    // Silence → silence
    {
        ReverbEngine e;
        e.prepare (sr);
        e.setSettings ({});
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i) { float l, r; e.process (0.0f, 0.0f, l, r); peak = std::max (peak, std::abs (l) + std::abs (r)); }
        check (peak == 0.0f, "Silence en entrée : silence en sortie");
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
