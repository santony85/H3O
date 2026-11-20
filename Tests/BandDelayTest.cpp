// Tests du moteur Band Delay (sans JUCE). Cible Xcode : BandDelayTest
#include "BandDelayEngine.h"

#include <cstdio>
#include <random>

namespace
{
    int failures = 0;
    constexpr double pi = 3.14159265358979323846;

    void check (bool ok, const char* text)
    {
        std::printf ("%-76s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    double energyAt (const std::vector<float>& x, size_t from, size_t to, double freq, double sr)
    {
        const double w = 2.0 * pi * freq / sr, coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            const double s0 = x[i] + coeff * s1 - s2;
            s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    // Fréquence dominante d'un passage (balayage en 1/24 d'octave de 50 Hz à 12 kHz)
    double dominant (const std::vector<float>& x, size_t from, size_t to, double sr)
    {
        double best = 0.0, bestE = -1.0;
        for (double f = 50.0; f < 12000.0; f *= std::pow (2.0, 1.0 / 48.0))
        {
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

    // Une impulsion en entrée ; renvoie la sortie gauche + droite
    std::vector<float> impulse (const BandDelayEngine::Settings& s, double sr, double seconds, float feedback,
                                std::vector<float>* left = nullptr, std::vector<float>* right = nullptr)
    {
        BandDelayEngine e;
        e.prepare (sr, 2.5);
        e.setSettings (s);
        std::vector<float> out ((size_t) (sr * seconds));
        if (left)  left->assign (out.size(), 0.0f);
        if (right) right->assign (out.size(), 0.0f);
        for (size_t i = 0; i < out.size(); ++i)
        {
            float l, r;
            e.process (i == 0 ? 1.0f : 0.0f, feedback, l, r);
            out[i] = l + r;
            if (left)  (*left)[i] = l;
            if (right) (*right)[i] = r;
        }
        return out;
    }
}

int main()
{
    char text[200];

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.1f kHz\n", sr / 1000.0);

        for (auto pattern : { BandDelayEngine::Pattern::rising, BandDelayEngine::Pattern::falling,
                              BandDelayEngine::Pattern::alternating })
        {
            BandDelayEngine::Settings s;
            s.bands = 4; s.timeMs = 800.0f; s.lowHz = 300.0f; s.highHz = 3000.0f;
            s.resonance = 0.8f; s.pattern = pattern;

            int count = 0;
            const auto bands = BandDelayEngine::layout (s, count);
            const auto out = impulse (s, sr, 1.0, 0.0f);

            // Chaque écho : il arrive à sa place et il a la couleur de sa bande
            double worstCents = 0.0, worstLevelDb = 0.0;
            bool onTime = true;
            for (int k = 0; k < count; ++k)
            {
                const size_t at = (size_t) (bands[(size_t) k].timeMs * 0.001 * sr);
                const size_t len = (size_t) (0.08 * sr);
                const double before = rms (out, at - (size_t) (0.02 * sr), at - (size_t) (0.001 * sr));
                const double during = rms (out, at, at + len);
                onTime = onTime && during > 20.0 * std::max (before, 1e-9);

                const double f = dominant (out, at, at + len, sr);
                worstCents = std::max (worstCents, std::abs (1200.0 * std::log2 (f / bands[(size_t) k].frequency)));
            }

            const char* name = pattern == BandDelayEngine::Pattern::rising ? "montante"
                             : pattern == BandDelayEngine::Pattern::falling ? "descendante" : "alternée";
            std::snprintf (text, sizeof text, "Couleurs %s : 4 échos à leur place, fréquence à %.0f cents près max",
                           name, worstCents);
            check (onTime && worstCents < 60.0, text);
            (void) worstLevelDb;
        }

        // L'ordre des couleurs
        {
            BandDelayEngine::Settings s;
            s.bands = 4; s.lowHz = 200.0f; s.highHz = 3200.0f;
            int n = 0;
            s.pattern = BandDelayEngine::Pattern::rising;
            const auto up = BandDelayEngine::layout (s, n);
            s.pattern = BandDelayEngine::Pattern::falling;
            const auto down = BandDelayEngine::layout (s, n);
            s.pattern = BandDelayEngine::Pattern::alternating;
            const auto alt = BandDelayEngine::layout (s, n);

            std::snprintf (text, sizeof text, "Ordre : montant %.0f→%.0f Hz, descendant %.0f→%.0f, alterné %.0f/%.0f/%.0f/%.0f",
                           up[0].frequency, up[3].frequency, down[0].frequency, down[3].frequency,
                           alt[0].frequency, alt[1].frequency, alt[2].frequency, alt[3].frequency);
            check (up[0].frequency < up[1].frequency && up[2].frequency < up[3].frequency
                     && down[0].frequency > down[3].frequency
                     && alt[0].frequency < alt[1].frequency && alt[1].frequency > alt[2].frequency
                     && alt[2].frequency < alt[3].frequency, text);
        }

        // Spread 100 % : la 1re bande à gauche seulement, la 2e à droite seulement
        {
            BandDelayEngine::Settings s;
            s.bands = 2; s.timeMs = 400.0f; s.spread = 1.0f;
            std::vector<float> l, r;
            impulse (s, sr, 0.6, 0.0f, &l, &r);
            const size_t a = (size_t) (0.2 * sr), b = (size_t) (0.4 * sr), len = (size_t) (0.05 * sr);
            const bool ok = rms (l, a, a + len) > 100.0 * rms (r, a, a + len) + 1e-12
                         && rms (r, b, b + len) > 100.0 * rms (l, b, b + len) + 1e-12;
            check (ok, "Spread 100 % : 1re bande à gauche, 2e à droite");
        }

        // Feedback : chaque bande se répète à intervalle régulier, de plus en plus pure, sans s'emballer
        {
            BandDelayEngine::Settings s;
            s.bands = 1; s.timeMs = 250.0f; s.lowHz = s.highHz = 800.0f; s.resonance = 1.0f;
            const auto out = impulse (s, sr, 20.0, 0.9f);
            const size_t len = (size_t) (0.1 * sr);
            const double first  = rms (out, (size_t) (0.25 * sr), (size_t) (0.25 * sr) + len);
            const double second = rms (out, (size_t) (0.50 * sr), (size_t) (0.50 * sr) + len);
            const double late   = rms (out, out.size() - len, out.size());
            bool finite = true;
            for (float y : out)
                finite = finite && std::isfinite (y);

            std::snprintf (text, sizeof text, "Feedback 90 %%, Q 12 : 2e répétition %+.1f dB, après 20 s %.0f dB, stable",
                           20.0 * std::log10 (second / first), 20.0 * std::log10 (std::max (late, 1e-12) / first));
            check (finite && second < first && second > 0.2 * first && late < 0.01 * first, text);
        }
    }

    // Niveau sur un vrai signal (bruit) : ni trop faible ni saturé, du filtre large au plus résonant
    for (float res : { 0.0f, 0.5f, 1.0f })
    {
        BandDelayEngine::Settings s;
        s.bands = 8; s.timeMs = 600.0f; s.lowHz = 200.0f; s.highHz = 5000.0f; s.resonance = res; s.spread = 1.0f;
        BandDelayEngine e;
        e.prepare (48000.0, 2.5);
        e.setSettings (s);
        std::mt19937 rng (1);
        std::normal_distribution<float> noise (0.0f, 0.2f);
        double sumIn = 0.0, sumOut = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < 48000 * 3; ++i)
        {
            const float x = noise (rng);
            float l, r;
            e.process (x, 0.0f, l, r);
            if (i > 48000)
            {
                sumIn += (double) x * x;
                sumOut += 0.5 * ((double) l * l + (double) r * r);
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            }
        }
        const double db = 10.0 * std::log10 (sumOut / sumIn);
        std::snprintf (text, sizeof text, "8 bandes, résonance %3.0f %% (Q %.1f) : niveau %+.1f dB, crête %.2f",
                       res * 100.0f, BandDelayEngine::qFor (res), db, peak);
        check (db > -14.0 && db < 3.0 && peak < 2.5f, text);
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
