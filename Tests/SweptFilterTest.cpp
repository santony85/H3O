// Tests du moteur Swept Filter (sans JUCE). Cible Xcode : SweptFilterTest
#include "SweptFilterEngine.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
    int failures = 0;
    constexpr double pi = 3.14159265358979323846;
    using E = SweptFilterEngine;

    void check (bool ok, const char* text)
    {
        std::printf ("%-80s %s\n", text, ok ? "OK" : "ÉCHEC");
        failures += ok ? 0 : 1;
    }

    // Gain (dB) du filtre immobile pour une sinusoïde
    double gainDb (E::Settings s, double freq, double sr)
    {
        s.source = E::Source::lfo; s.rateHz = 0.0f;
        E e;
        e.prepare (sr);
        e.setSettings (s);
        double in = 0.0, out = 0.0;
        for (int i = 0; i < (int) sr; ++i)
        {
            float l = (float) std::sin (2.0 * pi * freq * i / sr), r = l;
            const float x = l;
            e.process (l, r);
            if (i > (int) (sr / 2)) { in += (double) x * x; out += (double) l * l; }
        }
        return 10.0 * std::log10 (out / in);
    }
}

int main()
{
    char text[200];

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.1f kHz : filtres immobiles à 1 kHz (Q 0,7, sans compensation)\n", sr / 1000.0);

        E::Settings s;
        s.lowHz = s.highHz = 1000.0f;
        s.resonance = 0.0f;

        s.type = E::Type::lowPass;
        const double lpAt = gainDb (s, 1000.0, sr), lpLow = gainDb (s, 100.0, sr), lpHigh = gainDb (s, 4000.0, sr);
        std::snprintf (text, sizeof text, "Passe-bas  : %+.1f dB à 100 Hz, %+.1f dB à 1 kHz, %+.1f dB à 4 kHz", lpLow, lpAt, lpHigh);
        check (std::abs (lpLow) < 0.5 && std::abs (lpAt + 3.0) < 0.5 && lpHigh < -22.0, text);

        s.type = E::Type::highPass;
        const double hpAt = gainDb (s, 1000.0, sr), hpLow = gainDb (s, 250.0, sr), hpHigh = gainDb (s, 10000.0, sr);
        std::snprintf (text, sizeof text, "Passe-haut : %+.1f dB à 250 Hz, %+.1f dB à 1 kHz, %+.1f dB à 10 kHz", hpLow, hpAt, hpHigh);
        check (hpLow < -22.0 && std::abs (hpAt + 3.0) < 0.5 && std::abs (hpHigh) < 0.5, text);

        s.type = E::Type::bandPass;
        s.resonance = 0.6f;
        const float q = E::qFor (s.resonance);
        const double comp = 20.0 * std::log10 (E::makeupFor (s.type, q));
        const double bpAt = gainDb (s, 1000.0, sr) - comp, bpOct = gainDb (s, 2000.0, sr) - comp;
        std::snprintf (text, sizeof text, "Passe-bande Q %.1f : %+.1f dB au centre, %+.1f dB une octave au-dessus", q, bpAt, bpOct);
        check (std::abs (bpAt) < 0.5 && bpOct < -10.0, text);
    }

    const double sr = 48000.0;
    std::printf ("== Balayage par LFO (48 kHz)\n");

    // Rampe montante à 1 Hz de 200 à 3200 Hz, passe-bande étroit, note à 800 Hz (milieu de la plage) :
    // la note doit ressortir une fois par seconde, quand le filtre passe dessus (t = 0,5 s, 1,5 s…)
    {
        E::Settings s;
        s.type = E::Type::bandPass; s.shape = E::Shape::rampUp;
        s.lowHz = 200.0f; s.highHz = 3200.0f; s.resonance = 0.8f; s.rateHz = 1.0f;
        E e;
        e.prepare (sr);
        e.setSettings (s);
        std::vector<float> out ((size_t) (sr * 4));
        for (size_t i = 0; i < out.size(); ++i)
        {
            float l = (float) std::sin (2.0 * pi * 800.0 * double (i) / sr), r = l;
            e.process (l, r);
            out[i] = l;
        }
        double worst = 0.0;
        for (int k = 1; k < 4; ++k)
        {
            const size_t from = (size_t) (k * sr), to = (size_t) ((k + 1) * sr);
            size_t at = from;
            float peak = 0.0f;
            for (size_t i = from; i < to; ++i)
                if (std::abs (out[i]) > peak) { peak = std::abs (out[i]); at = i; }
            worst = std::max (worst, std::abs (double (at - from) / sr - 0.5));
        }
        std::snprintf (text, sizeof text, "Rampe 1 Hz : la note de 800 Hz ressort à mi-cycle (écart max %.0f ms)", worst * 1000.0);
        check (worst < 0.03, text);
    }

    // Formes d'onde et phase stéréo
    {
        E::Settings s;
        s.rateHz = 2.0f;
        for (auto shape : { E::Shape::sine, E::Shape::triangle, E::Shape::rampUp, E::Shape::rampDown, E::Shape::square })
        {
            s.shape = shape;
            s.stereoPhase = 1.0f;
            E e;
            e.prepare (sr);
            e.setSettings (s);
            float lo = 1.0f, hi = 0.0f, maxSum = 0.0f, minSum = 2.0f, maxJump = 0.0f, last = 0.0f;
            for (int i = 0; i < (int) sr * 2; ++i)
            {
                float l = 0.0f, r = 0.0f;
                e.process (l, r);
                const float p = e.getPosition (0), pr = e.getPosition (1);
                if (i > 1000)
                {
                    lo = std::min (lo, p); hi = std::max (hi, p);
                    maxSum = std::max (maxSum, p + pr); minSum = std::min (minSum, p + pr);
                    if (shape == E::Shape::square || shape == E::Shape::sine || shape == E::Shape::triangle)
                        maxJump = std::max (maxJump, std::abs (p - last));
                }
                last = p;
            }
            const char* names[] = { "Sinus", "Triangle", "Rampe montante", "Rampe descendante", "Carré" };
            const bool symmetric = shape == E::Shape::sine || shape == E::Shape::triangle;
            std::snprintf (text, sizeof text, "%-17s : position de %.2f à %.2f%s%s", names[(int) shape], lo, hi,
                           symmetric ? (std::abs (maxSum - 1.0f) < 0.02f && std::abs (minSum - 1.0f) < 0.02f ? ", droite en opposition" : ", PAS en opposition") : "",
                           shape == E::Shape::square ? (maxJump < 0.01f ? ", fronts adoucis" : ", fronts BRUTAUX") : "");
            check (lo < 0.02f && hi > 0.98f
                     && (! symmetric || (std::abs (maxSum - 1.0f) < 0.02f && std::abs (minSum - 1.0f) < 0.02f))
                     && (shape != E::Shape::square || maxJump < 0.01f), text);
        }
    }

    std::printf ("== Enveloppe (auto-wah)\n");
    for (bool down : { false, true })
    {
        E::Settings s;
        s.source = E::Source::envelope;
        s.attackMs = 5.0f; s.releaseMs = 150.0f; s.downward = down;
        E e;
        e.prepare (sr);
        e.setSettings (s);
        float atAttack = 0.0f, after = 0.0f, before = 0.0f;
        for (int i = 0; i < (int) (sr * 2); ++i)
        {
            const double t = double (i) / sr;
            // Silence, puis une note pincée à t = 0,5 s qui s'éteint en ~0,15 s
            const float x = t < 0.5 ? 0.0f : (float) (0.4 * std::exp (-(t - 0.5) / 0.15) * std::sin (2.0 * pi * 196.0 * t));
            float l = x, r = x;
            e.process (l, r);
            if (std::abs (t - 0.45) < 1e-4) before = e.getPosition (0);
            if (std::abs (t - 0.53) < 1e-4) atAttack = e.getPosition (0);
            if (std::abs (t - 1.9) < 1e-4) after = e.getPosition (0);
        }
        std::snprintf (text, sizeof text, "%s : position %.2f avant la note, %.2f à l'attaque, %.2f 1,4 s après",
                       down ? "Vers le bas " : "Vers le haut", before, atAttack, after);
        check (down ? (before > 0.99f && atAttack < 0.3f && after > 0.9f)
                    : (before < 0.01f && atAttack > 0.7f && after < 0.1f), text);
    }

    std::printf ("== Niveau et stabilité\n");
    for (auto type : { E::Type::lowPass, E::Type::bandPass, E::Type::highPass })
        for (float res : { 0.0f, 1.0f })
        {
            E::Settings s;
            s.type = type; s.resonance = res; s.rateHz = 8.0f; s.lowHz = 80.0f; s.highHz = 12000.0f;
            E e;
            e.prepare (sr);
            e.setSettings (s);
            std::mt19937 rng (2);
            std::normal_distribution<float> noise (0.0f, 0.1f);
            double in = 0.0, out = 0.0;
            float peak = 0.0f;
            bool finite = true;
            for (int i = 0; i < (int) sr * 4; ++i)
            {
                float l = noise (rng), r = l;
                in += (double) l * l;
                e.process (l, r);
                out += (double) l * l;
                peak = std::max (peak, std::abs (l));
                finite = finite && std::isfinite (l) && std::isfinite (r);
            }
            const char* names[] = { "Passe-bas", "Passe-bande", "Passe-haut" };
            const double db = 10.0 * std::log10 (out / in);
            std::snprintf (text, sizeof text, "%-11s Q %4.1f balayé à 8 Hz sur du bruit : niveau %+.1f dB, crête %.2f",
                           names[(int) type], E::qFor (res), db, peak);
            check (finite && db > -16.0 && db < 3.0 && peak < 1.5f, text);
        }

    std::printf (failures == 0 ? "TOUS LES TESTS OK\n" : "%d TEST(S) EN ÉCHEC\n", failures);
    return failures == 0 ? 0 : 1;
}
