#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Harmonizer/PitchDetector.h"

// Les sources de la matrice de modulation de H3O Chain :
//   - LFO 1 et LFO 2 : oscillateurs lents (sinus, triangle, carré, dents de scie)       −1 … +1
//   - Envelope       : le volume de ton jeu (−48 dB → 0, 0 dB → 1)                     0 … 1
//   - Pitch          : la note jouée, de mi grave (E2) à mi aigu (E6)                   0 … 1
//   - Random         : une valeur au hasard, renouvelée au rythme choisi, en glissant   −1 … +1
//
// Les sources avancent par petits paquets d'échantillons (process) ; on lit leur valeur à la fin de chaque paquet.
// Tout est alloué dans prepare() : utilisable dans le fil audio. C++ pur, sans JUCE.
class ModulationSources
{
public:
    enum Source { off = 0, lfo1, lfo2, envelope, pitch, random, numSources };
    enum Shape  { sine = 0, triangle, square, sawUp, sawDown, numShapes };

    static const char* sourceName (int s)
    {
        static const char* names[] = { "Off", "LFO 1", "LFO 2", "Envelope", "Pitch", "Random" };
        return names[std::clamp (s, 0, (int) numSources - 1)];
    }

    static const char* shapeName (int s)
    {
        static const char* names[] = { "Sine", "Triangle", "Square", "Saw Up", "Saw Down" };
        return names[std::clamp (s, 0, (int) numShapes - 1)];
    }

    static bool isBipolar (int s) { return s == lfo1 || s == lfo2 || s == random; }

    // Divisions de tempo pour les LFO synchronisés. "." = pointée, "T" = triolet.
    static constexpr int numDivisions = 15;

    static const char* divisionName (int d)
    {
        static const char* names[] = { "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2 .", "1/2", "1/2 T",
                                       "1/4 .", "1/4", "1/4 T", "1/8 .", "1/8", "1/8 T", "1/16", "1/16 T" };
        return names[std::clamp (d, 0, numDivisions - 1)];
    }

    // Durée d'un cycle, en noires. Les mesures suivent la signature rythmique (4 noires en 4/4, 3 en 3/4…)
    static double divisionQuarters (int d, double quartersPerBar = 4.0)
    {
        static const double beats[] = { -8.0, -4.0, -2.0, -1.0, 3.0, 2.0, 4.0 / 3.0,
                                        1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
        const double b = beats[std::clamp (d, 0, numDivisions - 1)];
        return b < 0.0 ? -b * quartersPerBar : b;   // négatif = en mesures
    }

    static constexpr int defaultDivision = 3;   // 1 Bar

    struct Settings
    {
        float lfoRate[2]  { 0.5f, 3.0f };      // Hz
        int   lfoShape[2] { sine, triangle };
        float randomRate  = 2.0f;              // nouvelles valeurs par seconde
        float attackMs    = 10.0f;             // enveloppe
        float releaseMs   = 250.0f;
    };

    static constexpr float lowestNote  = 40.0f;   // E2
    static constexpr float highestNote = 88.0f;   // E6

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        detector.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        phase[0] = phase[1] = 0.0;
        level = 0.0f;
        pitchTarget = pitchValue = 0.0f;
        randomPhase = 1.0;   // tire une première valeur tout de suite
        randomTarget = randomValue = 0.0f;
        detector.reset();
        for (auto& v : values)
            v = 0.0f;
    }

    void setSettings (const Settings& s) { settings = s; }

    // LFO synchronisé et morceau en lecture : place le LFO à la phase voulue (0 … 1)
    // au début du prochain paquet, pour qu'il reste calé sur les mesures
    void setLfoPhase (int l, double p) noexcept { phase[l & 1] = p - std::floor (p); }

    // Avance de n échantillons. mono : le signal joué (avant les effets).
    // trackPitch : la détection de note coûte du calcul, seulement si une ligne utilise Pitch
    void process (const float* mono, int n, bool trackPitch = true) noexcept
    {
        // Enveloppe : suit le niveau crête, montée et descente réglables.
        // Les temps sont ceux du trajet complet (≈ 40 dB) : 5 constantes de temps
        const float attack  = coefficient (settings.attackMs / 5.0f);
        const float release = coefficient (settings.releaseMs / 5.0f);
        const float pitchGlide = coefficient (20.0f);
        const float randomGlide = coefficient (250.0f / std::max (0.05f, settings.randomRate));

        for (int i = 0; i < n; ++i)
        {
            const float x = std::abs (mono[i]);
            level += (x > level ? attack : release) * (x - level);

            if (trackPitch && detector.push (mono[i]) && detector.isVoiced())
                pitchTarget = std::clamp ((PitchDetector::toMidi (detector.getFrequency()) - lowestNote) / (highestNote - lowestNote), 0.0f, 1.0f);
            pitchValue += pitchGlide * (pitchTarget - pitchValue);   // sans note, la dernière est gardée

            randomPhase += settings.randomRate / rate;
            if (randomPhase >= 1.0)
            {
                randomPhase -= std::floor (randomPhase);
                randomTarget = nextRandom();
            }
            randomValue += randomGlide * (randomTarget - randomValue);
        }

        for (int l = 0; l < 2; ++l)
        {
            phase[l] += (double) settings.lfoRate[l] * (double) n / rate;
            phase[l] -= std::floor (phase[l]);
            values[lfo1 + l] = shapeAt (settings.lfoShape[l], phase[l]);
        }

        const float db = 20.0f * std::log10 (std::max (level, 1.0e-6f));
        values[envelope] = std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        values[pitch]    = pitchValue;
        values[random]   = randomValue;
    }

    float get (int source) const noexcept { return source > off && source < numSources ? values[source] : 0.0f; }

    static float shapeAt (int shape, double p) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        switch (shape)
        {
            case triangle: return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case square:   return p < 0.5 ? 1.0f : -1.0f;
            case sawUp:    return (float) (2.0 * p - 1.0);
            case sawDown:  return (float) (1.0 - 2.0 * p);
            default:       return (float) std::sin (twoPi * p);
        }
    }

private:
    float coefficient (float ms) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (std::max (0.1f, ms) * 0.001f * (float) rate));
    }

    float nextRandom() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / 8388608.0f - 1.0f;   // −1 … +1
    }

    Settings settings;
    double rate = 48000.0;
    double phase[2] { 0.0, 0.0 };
    float level = 0.0f, pitchTarget = 0.0f, pitchValue = 0.0f;
    double randomPhase = 1.0;
    float randomTarget = 0.0f, randomValue = 0.0f;
    std::uint32_t seed = 0x12345678u;
    PitchDetector detector;
    float values[numSources] {};
};
