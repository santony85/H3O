// Test hors plugin : vérifie que le pitch shifter transpose à la bonne hauteur.
// Lancement : cmake --build build --config Release --target PitchTest -- -quiet && ./build/Release/PitchTest

#include "PitchShifter.h"
#include <cstdio>
#include <cmath>
#include <vector>

// Énergie du signal à une fréquence donnée (algorithme de Goertzel, fenêtre de Hann)
static double energyAt (const std::vector<float>& x, size_t skip, double freq, double sampleRate)
{
    const double w = 2.0 * M_PI * freq / sampleRate;
    const double coeff = 2.0 * std::cos (w);
    const size_t len = x.size() - skip;
    double s1 = 0.0, s2 = 0.0;

    for (size_t i = 0; i < len; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos (2.0 * M_PI * double (i) / double (len - 1));
        const double s0 = x[skip + i] * hann + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Fréquence dominante : balayage de ±150 cents autour de la valeur attendue, par pas de 0,25 cent.
// (Compter les passages à zéro ne marche pas : chaque splice fait sauter la phase.)
static double measureFrequency (const std::vector<float>& x, double sampleRate, size_t skip, double around)
{
    double bestFreq = 0.0, bestEnergy = -1.0;
    for (double cents = -150.0; cents <= 150.0; cents += 0.25)
    {
        const double f = around * std::pow (2.0, cents / 1200.0);
        const double e = energyAt (x, skip, f, sampleRate);
        if (e > bestEnergy) { bestEnergy = e; bestFreq = f; }
    }
    return bestFreq;
}

int main()
{
    const double sr = 48000.0;
    const size_t n = (size_t) (sr * 10.0);   // 10 s, pour pouvoir mesurer quelques cents
    int failures = 0;

    struct Case { const char* name; double cents; };
    const Case cases[] = {
        { "+9 cents (Micro-Pitch)", 9.0 },
        { "-9 cents (Micro-Pitch)", -9.0 },
        { "+7 demi-tons (quinte)", 700.0 },
        { "+12 demi-tons (octave)", 1200.0 },
        { "-12 demi-tons (octave basse)", -1200.0 },
    };

    // Plusieurs notes d'entrée : un pitch shifter naïf ne marche que pour certaines
    for (double inFreq : { 200.0, 220.0, 230.0, 261.63, 329.63 })
    {
    std::printf ("== entrée %.2f Hz\n", inFreq);

    for (const auto& c : cases)
    {
        PitchShifter shifter;
        shifter.prepare (sr, 0.1, 0.1);
        PitchShifter::Voice voice;

        const float ratio = (float) std::pow (2.0, c.cents / 1200.0);
        std::vector<float> out (n);

        for (size_t i = 0; i < n; ++i)
        {
            shifter.write ((float) std::sin (2.0 * M_PI * inFreq * double (i) / sr));
            out[i] = shifter.process (voice, ratio, (float) (0.010 * sr), (float) (0.050 * sr));
        }

        const double expected = inFreq * ratio;
        const double measured = measureFrequency (out, sr, (size_t) (sr * 0.2), expected);
        const double errorCents = 1200.0 * std::log2 (measured / expected);
        const bool ok = std::fabs (errorCents) < 1.0;
        failures += ok ? 0 : 1;

        std::printf ("%-30s attendu %8.2f Hz  mesuré %8.2f Hz  écart %+6.2f cents  %s\n",
                     c.name, expected, measured, errorCents, ok ? "OK" : "ÉCHEC");
    }
    }

    std::printf ("%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures;
}
