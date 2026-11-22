#pragma once

#include <algorithm>
#include <cmath>

// Trémolo, trémolo harmonique et auto-pan, en C++ pur (testable sans JUCE).
//
//   Tremolo  : le volume ondule (les deux côtés ensemble, ou décalés avec Stereo Phase : 180° = ping-pong)
//   Harmonic : les graves et les aigus ondulent en opposition (le trémolo des vieux amplis « brownface ») :
//              le volume reste presque constant, c'est le timbre qui bouge
//   Auto-Pan : le son se promène de gauche à droite, à puissance constante
//
// La forme de l'onde est lissée (Smooth) : même la forme carrée ne claque pas.
class TremoloEngine
{
public:
    enum Mode { tremolo = 0, harmonic, autoPan, numModes };
    enum Shape { sine = 0, triangle, square, sawUp, sawDown, numShapes };

    struct Settings
    {
        int mode = tremolo;
        int shape = sine;
        float depth = 0.6f;          // 0 à 1
        float smooth = 0.3f;         // 0 à 1 : arrondit les angles (carré, dents de scie)
        float stereoPhase = 0.0f;    // degrés, 0 à 180 (Tremolo et Harmonic)
        float crossoverHz = 650.0f;  // Harmonic : séparation graves / aigus
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        lowL = lowR = 0.0f;
        gainSmoothL = gainSmoothR = -1.0f;
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.depth = std::clamp (s.depth, 0.0f, 1.0f);
        settings.smooth = std::clamp (s.smooth, 0.0f, 1.0f);
        crossoverCoef = 1.0f - std::exp (-2.0f * 3.14159265f * std::clamp (s.crossoverHz, 50.0f, 5000.0f) / (float) fs);
        // Lissage : de 0,5 ms (net, sans clic) à ~20 % de la période
        smoothMs = 0.5f + settings.smooth * 40.0f;
    }

    void setRate (float hz) { increment = std::max (0.0, (double) hz) / fs; }

    // Calé sur le morceau : phase de l'onde (0 à 1) donnée par la position de lecture
    void setPhase (double newPhase) { phase = newPhase - std::floor (newPhase); }
    double getPhase() const { return phase; }

    // Forme d'onde unipolaire : 1 = plein volume / position de départ, 0 = creux
    static float wave (int shape, double p)
    {
        p -= std::floor (p);
        switch (shape)
        {
            case triangle: return (float) (p < 0.5 ? 1.0 - 2.0 * p : 2.0 * p - 1.0);
            case square:   return p < 0.5 ? 1.0f : 0.0f;
            case sawUp:    return (float) p;
            case sawDown:  return (float) (1.0 - p);
            default:       return (float) (0.5 + 0.5 * std::cos (2.0 * 3.14159265358979 * p));
        }
    }

    // Traite un bloc en place ; right peut être nul (mono : l'auto-pan agit alors comme un trémolo)
    void process (float* left, float* right, int numSamples)
    {
        const float smoothCoef = 1.0f - std::exp (-1.0f / (0.001f * smoothMs * (float) fs));
        const double offset = settings.stereoPhase / 360.0;
        const float depth = settings.depth;

        for (int i = 0; i < numSamples; ++i)
        {
            float waveL = wave (settings.shape, phase);
            float waveR = wave (settings.shape, phase + offset);
            if (settings.mode == autoPan)
                waveR = 1.0f - waveL;   // à l'opposé : quand l'un monte, l'autre descend

            // Lissage des angles (et départ sans saut)
            if (gainSmoothL < 0.0f)
            {
                gainSmoothL = waveL;
                gainSmoothR = waveR;
            }
            gainSmoothL += smoothCoef * (waveL - gainSmoothL);
            gainSmoothR += smoothCoef * (waveR - gainSmoothR);

            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;

            if (settings.mode == harmonic)
            {
                // Graves et aigus en opposition ; séparation complémentaire : sans profondeur, le son est intact
                lowL += crossoverCoef * (inL - lowL);
                lowR += crossoverCoef * (inR - lowR);
                const float highL = inL - lowL, highR = inR - lowR;

                const float gainLowL = 1.0f - depth * (1.0f - gainSmoothL), gainHighL = 1.0f - depth * gainSmoothL;
                const float gainLowR = 1.0f - depth * (1.0f - gainSmoothR), gainHighR = 1.0f - depth * gainSmoothR;
                left[i] = lowL * gainLowL + highL * gainHighL;
                if (right != nullptr)
                    right[i] = lowR * gainLowR + highR * gainHighR;
            }
            else if (settings.mode == autoPan && right != nullptr)
            {
                // Position de −1 (gauche) à +1 (droite), à puissance constante ; au centre, rien ne change
                const float position = depth * (gainSmoothR - gainSmoothL);
                const float angle = (position + 1.0f) * 0.25f * 3.14159265f;
                left[i]  = inL * std::cos (angle) * 1.41421356f;
                right[i] = inR * std::sin (angle) * 1.41421356f;
            }
            else
            {
                left[i] = inL * (1.0f - depth * (1.0f - gainSmoothL));
                if (right != nullptr)
                    right[i] = inR * (1.0f - depth * (1.0f - gainSmoothR));
            }

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    Settings settings;
    double fs = 44100.0;
    double phase = 0.0, increment = 0.0;
    float crossoverCoef = 0.1f, smoothMs = 10.0f;
    float lowL = 0.0f, lowR = 0.0f;
    float gainSmoothL = -1.0f, gainSmoothR = -1.0f;
};
