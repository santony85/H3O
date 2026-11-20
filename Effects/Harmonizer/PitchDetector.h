#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Détecteur de hauteur pour une note jouée seule (guitare, voix), algorithme YIN.
//
// Pour rester léger, l'analyse se fait sur le signal sous-échantillonné vers ~24 kHz :
// largement assez pour des fondamentales de 70 Hz (mi grave accordé bas) à 1500 Hz.
// Une nouvelle mesure toutes les ~5 ms, sur une fenêtre de ~36 ms.
//
// Tout est alloué dans prepare() : utilisable dans le fil audio. C++ pur, sans JUCE.
class PitchDetector
{
public:
    static constexpr double minFrequency = 70.0;
    static constexpr double maxFrequency = 1500.0;

    void prepare (double sampleRate)
    {
        decimation = std::max (1, (int) std::lround (sampleRate / 24000.0));
        rate = sampleRate / decimation;

        maxLag = (int) std::ceil (rate / minFrequency);
        minLag = std::max (2, (int) std::floor (rate / maxFrequency));
        window = maxLag + 32;
        hop    = std::max (32, (int) (rate * 0.005));

        history.assign ((size_t) (window + maxLag + 1), 0.0f);
        diff.assign ((size_t) (maxLag + 2), 0.0f);

        // Passe-bas avant sous-échantillonnage (évite le repliement des harmoniques aiguës)
        const double fc = 0.45 * rate / 2.0;
        lpCoeff = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * fc / sampleRate));

        reset();
    }

    void reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        lp1 = lp2 = 0.0f;
        phase = 0;
        sinceAnalysis = 0;
        written = 0;
        frequency = 0.0f;
        voiced = false;
    }

    // À appeler pour chaque échantillon d'entrée. Renvoie true quand une nouvelle mesure est disponible.
    bool push (float x) noexcept
    {
        lp1 += lpCoeff * (x - lp1);
        lp2 += lpCoeff * (lp1 - lp2);

        if (++phase < decimation)
            return false;
        phase = 0;

        // Fenêtre glissante : le plus récent à la fin
        std::copy (history.begin() + 1, history.end(), history.begin());
        history.back() = lp2;
        written = std::min (written + 1, (int) history.size());

        if (++sinceAnalysis < hop || written < (int) history.size())
            return false;

        sinceAnalysis = 0;
        analyse();
        return true;
    }

    bool isVoiced() const noexcept        { return voiced; }
    float getFrequency() const noexcept   { return frequency; }   // en Hz, valable si isVoiced()
    float getConfidence() const noexcept  { return confidence; }  // 0 (bruit) à 1 (note pure)

    // Note MIDI (avec décimales) d'une fréquence : 440 Hz → 69
    static float toMidi (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

private:
    void analyse() noexcept
    {
        const float* x = history.data();

        // Niveau trop faible : rien à détecter (évite de suivre le souffle de l'ampli)
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += (double) x[j] * x[j];

        if (energy / window < 1.0e-6)   // environ -60 dBFS
        {
            voiced = false;
            confidence = 0.0f;
            return;
        }

        // Fonction de différence de YIN, normalisée par sa moyenne cumulée
        diff[0] = 1.0f;
        double running = 0.0;

        for (int lag = 1; lag <= maxLag; ++lag)
        {
            double d = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double delta = (double) x[j] - x[j + lag];
                d += delta * delta;
            }

            running += d;
            diff[(size_t) lag] = running > 0.0 ? (float) (d * lag / running) : 1.0f;
        }

        // Premier creux sous le seuil : la période (les creux suivants sont ses multiples)
        int best = -1;
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            if (diff[(size_t) lag] < threshold)
            {
                while (lag + 1 < maxLag && diff[(size_t) lag + 1] < diff[(size_t) lag])
                    ++lag;
                best = lag;
                break;
            }
        }

        if (best < 0)
        {
            voiced = false;
            confidence = 0.0f;
            return;
        }

        // Interpolation parabolique : période à une fraction d'échantillon près
        const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
        const double period = (double) best + (double) std::clamp (shift, -1.0f, 1.0f);

        frequency  = (float) (rate / period);
        confidence = std::clamp (1.0f - b, 0.0f, 1.0f);
        voiced     = frequency >= minFrequency && frequency <= maxFrequency;
    }

    static constexpr float threshold = 0.15f;

    std::vector<float> history, diff;
    int decimation = 2, phase = 0, window = 0, hop = 0, minLag = 2, maxLag = 0;
    int sinceAnalysis = 0, written = 0;
    double rate = 24000.0;
    float lpCoeff = 1.0f, lp1 = 0.0f, lp2 = 0.0f;

    float frequency = 0.0f, confidence = 0.0f;
    bool voiced = false;
};
