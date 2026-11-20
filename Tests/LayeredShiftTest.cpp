// Tests du moteur Layered Shift (sans JUCE). Cible Xcode : LayeredShiftTest
#include "LayeredShift/LayeredShiftEngine.h"

#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;
    constexpr double pi = 3.14159265358979323846;

    void check (bool ok, const char* text)
    {
        std::printf ("%-78s %s\n", text, ok ? "OK" : "ÉCHEC");
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

    // Écart en cents de la fréquence dominante par rapport à « around » (balayage ±150 cents)
    double centsOff (const std::vector<float>& x, size_t from, size_t to, double around, double sr)
    {
        double best = 0.0, bestE = -1.0;
        for (double c = -150.0; c <= 150.0; c += 0.5)
        {
            const double e = energyAt (x, from, to, around * std::pow (2.0, c / 1200.0), sr);
            if (e > bestE) { bestE = e; best = c; }
        }
        return best;
    }

    struct Out { std::vector<float> l, r; };

    Out run (const LayeredShiftEngine::Settings& s, double sr, double seconds, float feedback = 0.0f, bool impulse = false)
    {
        LayeredShiftEngine e;
        e.prepare (sr);
        e.setSettings (s);
        Out o { std::vector<float> ((size_t) (sr * seconds)), std::vector<float> ((size_t) (sr * seconds)) };
        for (size_t i = 0; i < o.l.size(); ++i)
        {
            const float x = impulse ? (i < (size_t) (0.01 * sr) ? (float) std::sin (2.0 * pi * 330.0 * double (i) / sr) : 0.0f)
                                    : (float) (0.5 * std::sin (2.0 * pi * 330.0 * double (i) / sr));
            e.process (x, feedback, o.l[i], o.r[i]);
        }
        return o;
    }
}

int main()
{
    char text[200];

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.1f kHz\n", sr / 1000.0);

        // Chaque couche à sa hauteur, sans balayage ni désaccord
        for (auto [semis, fine] : { std::pair { 12.0f, 0.0f }, std::pair { 7.0f, 0.0f }, std::pair { -12.0f, 0.0f }, std::pair { 0.0f, 15.0f } })
        {
            LayeredShiftEngine::Settings s;
            s.a = { semis, fine, 10.0f, 1.0f };
            s.b.level = 0.0f;
            s.sweepCents = 0.0f;
            const auto o = run (s, sr, 1.5);
            const double target = 330.0 * std::pow (2.0, (semis * 100.0 + fine) / 1200.0);
            const double errL = centsOff (o.l, (size_t) (0.5 * sr), o.l.size(), target, sr);
            const double errR = centsOff (o.r, (size_t) (0.5 * sr), o.r.size(), target, sr);
            std::snprintf (text, sizeof text, "Couche A %+3.0f demi-tons %+3.0f cents : écart gauche %+.1f, droite %+.1f cents",
                           semis, fine, errL, errR);
            check (std::abs (errL) < 3.0 && std::abs (errR) < 3.0, text);
        }

        // Detune : la gauche monte de la moitié, la droite descend de la moitié
        {
            LayeredShiftEngine::Settings s;
            s.a = { 0.0f, 0.0f, 10.0f, 1.0f };
            s.b.level = 0.0f;
            s.detuneCents = 30.0f;
            s.sweepCents = 0.0f;
            const auto o = run (s, sr, 1.5);
            const double l = centsOff (o.l, (size_t) (0.5 * sr), o.l.size(), 330.0, sr);
            const double r = centsOff (o.r, (size_t) (0.5 * sr), o.r.size(), 330.0, sr);
            std::snprintf (text, sizeof text, "Detune 30 cents : gauche %+.1f, droite %+.1f cents (attendu +15 / -15)", l, r);
            check (std::abs (l - 15.0) < 3.0 && std::abs (r + 15.0) < 3.0, text);
        }

        // Sweep : la hauteur ondule de ± la profondeur, au bon rythme
        {
            LayeredShiftEngine::Settings s;
            s.a = { 0.0f, 0.0f, 10.0f, 1.0f };
            s.b.level = 0.0f;
            s.sweepRateHz = 1.0f;
            s.sweepCents = 30.0f;
            const auto o = run (s, sr, 3.0);
            double lo = 1e9, hi = -1e9;
            for (double t = 1.0; t < 2.0; t += 0.05)
            {
                const size_t a = (size_t) (t * sr), b = a + (size_t) (0.05 * sr);
                const double c = centsOff (o.l, a, b, 330.0, sr);
                lo = std::min (lo, c); hi = std::max (hi, c);
            }
            std::snprintf (text, sizeof text, "Sweep 1 Hz ± 30 cents : la hauteur va de %+.0f à %+.0f cents", lo, hi);
            check (lo < -20.0 && lo > -40.0 && hi > 20.0 && hi < 40.0, text);
        }

        // Délai par couche : la note courte ressort au bon moment
        {
            LayeredShiftEngine::Settings s;
            s.a = { 0.0f, 0.0f, 200.0f, 1.0f };
            s.b = { 0.0f, 0.0f, 400.0f, 1.0f };
            s.sweepCents = 0.0f;
            const auto o = run (s, sr, 0.8, 0.0f, true);
            auto energyAround = [&] (double t) {
                double e = 0.0;
                for (size_t i = (size_t) ((t - 0.005) * sr); i < (size_t) ((t + 0.08) * sr); ++i) e += (double) o.l[i] * o.l[i];
                return e;
            };
            const double atA = energyAround (0.2), atB = energyAround (0.4), between = energyAround (0.3);
            std::snprintf (text, sizeof text, "Délais 200 / 400 ms : les deux couches arrivent à leur place (%.0f fois le bruit entre)",
                           std::min (atA, atB) / std::max (between, 1e-12));
            check (atA > 100.0 * between && atB > 100.0 * between, text);
        }

        // Feedback : une cascade d'octaves qui s'éteint sans s'emballer
        {
            LayeredShiftEngine::Settings s;
            s.a = { 12.0f, 0.0f, 150.0f, 0.8f };
            s.b = { 7.0f, 0.0f, 300.0f, 0.6f };
            s.detuneCents = 10.0f;
            s.sweepCents = 5.0f;
            const auto o = run (s, sr, 12.0, 0.9f, true);
            float early = 0.0f, late = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < o.l.size(); ++i)
            {
                finite = finite && std::isfinite (o.l[i]) && std::isfinite (o.r[i]);
                const float a = std::max (std::abs (o.l[i]), std::abs (o.r[i]));
                (i < (size_t) (2 * sr) ? early : late) = std::max (i < (size_t) (2 * sr) ? early : late, i < (size_t) (2 * sr) ? a : (i > (size_t) (10 * sr) ? a : 0.0f));
            }
            std::snprintf (text, sizeof text, "Feedback 90 %% : crête %.2f au début, %.4f après 10 s, stable", early, late);
            check (finite && late < 0.5f * early && early < 3.0f, text);
        }
    }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
