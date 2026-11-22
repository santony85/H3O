#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Flanger stéréo, en C++ pur (testable sans JUCE).
//
// Le son passe dans une ligne à retard très courte (quelques millisecondes) dont la durée balaie
// au rythme d'une onde ; mélangé au son direct, il crée un peigne de creux qui glissent : le « jet ».
//
//   Classic      : le retard balaie autour de Manual (jusqu'à ±3 octaves de durée)
//   Through-Zero : le son direct est lui-même retardé de Manual ; le retard de l'effet passe
//                  de 0 à 2 × Manual et croise le son direct : l'annulation complète des bandes magnétiques
//   Feedback     : renvoie l'effet à l'entrée ; positif = métallique, négatif = creux, plus doux
//   Stereo Phase : décale l'onde de droite (180° = les creux montent d'un côté quand ils descendent de l'autre)
class FlangerEngine
{
public:
    enum Mode { classic = 0, throughZero, numModes };
    enum Shape { sine = 0, triangle, numShapes };

    static constexpr float maxDelayMs = 30.0f;

    struct Settings
    {
        int mode = classic;
        int shape = triangle;
        float depth = 0.7f;          // 0 à 1
        float manualMs = 2.0f;       // centre du balayage (Classic), retard du son direct (Through-Zero)
        float feedback = 0.5f;       // −0,95 à 0,95
        float stereoPhase = 0.0f;    // degrés, 0 à 180
        float mix = 0.5f;            // 0,5 = creux les plus profonds
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        int size = 1;
        while (size < (int) (fs * maxDelayMs * 0.001) + 8)
            size <<= 1;
        for (auto* set : { &lines, &dryLines })
            for (auto& line : *set)
                line.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        write = 0;
        for (auto* set : { &lines, &dryLines })
            for (auto& line : *set)
                std::fill (line.begin(), line.end(), 0.0f);
        last[0] = last[1] = 0.0f;
        smoothedManual = -1.0f;
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.mode = std::clamp (s.mode, 0, numModes - 1);
        settings.depth = std::clamp (s.depth, 0.0f, 1.0f);
        settings.manualMs = std::clamp (s.manualMs, 0.1f, 10.0f);
        settings.feedback = std::clamp (s.feedback, -0.95f, 0.95f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
    }

    void setRate (float hz) { increment = std::max (0.0, (double) hz) / fs; }

    // Calé sur le morceau : phase de l'onde (0 à 1) donnée par la position de lecture
    void setPhase (double newPhase) { phase = newPhase - std::floor (newPhase); }
    double getPhase() const { return phase; }

    // Onde unipolaire (0 à 1) : 0 = retard le plus court, 1 = le plus long
    static float wave (int shape, double p)
    {
        p -= std::floor (p);
        if (shape == triangle)
            return (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
        return (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * p));
    }

    // Retard de l'effet (ms) pour une position de l'onde et un Manual donné
    float delayMs (float w, float manual) const
    {
        if (settings.mode == throughZero)
            return std::max (0.0f, manual * (1.0f + settings.depth * (2.0f * w - 1.0f)));
        return std::clamp (manual * std::exp2 (3.0f * settings.depth * (2.0f * w - 1.0f)), 0.05f, maxDelayMs - 2.0f);
    }
    float delayMs (float w) const { return delayMs (w, settings.manualMs); }

    // Traite un bloc en place ; right peut être nul (mono)
    void process (float* left, float* right, int numSamples)
    {
        if (lines[0].empty())
            return;

        const double offset = settings.stereoPhase / 360.0;
        const float dryGain = 1.0f - settings.mix, wetGain = settings.mix;
        const float msToSamples = (float) fs * 0.001f;
        const float maxSamples = (float) (mask - 4);
        const int channels = right != nullptr ? 2 : 1;
        if (smoothedManual < 0.0f)
            smoothedManual = settings.manualMs;

        for (int i = 0; i < numSamples; ++i)
        {
            // Manual lissé : tourner le bouton glisse au lieu de cliquer
            smoothedManual += 0.002f * (settings.manualMs - smoothedManual);

            for (int ch = 0; ch < channels; ++ch)
            {
                float* data = ch == 0 ? left : right;
                auto& line = lines[(size_t) ch];
                const float x = data[i];

                line[(size_t) write] = x + settings.feedback * last[ch];
                dryLines[(size_t) ch][(size_t) write] = x;

                const float w = wave (settings.shape, phase + (ch == 0 ? 0.0 : offset));
                const float wetDelay = std::clamp (delayMs (w, smoothedManual) * msToSamples, 1.0f, maxSamples);
                const float wet = read (line, wetDelay);
                last[ch] = wet;

                // Through-Zero : le son direct passe par la même ligne, retardé de Manual
                const float dry = settings.mode == throughZero
                                    ? read (dryLines[(size_t) ch], std::clamp (smoothedManual * msToSamples, 1.0f, maxSamples))
                                    : x;
                data[i] = dryGain * dry + wetGain * wet;
            }

            if (channels == 1)
                last[1] = last[0];
            write = (write + 1) & mask;
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        for (auto& v : last)
            if (std::abs (v) < 1.0e-15f)
                v = 0.0f;
    }

private:
    // Lecture entre deux échantillons (Hermite à 4 points) : un balayage sans grésillement
    float read (const std::vector<float>& line, float delaySamples) const
    {
        const float position = (float) write - delaySamples;
        const float floorPos = std::floor (position);
        const float t = position - floorPos;
        const int i = (int) floorPos;
        const float xm1 = line[(size_t) ((i - 1) & mask)], x0 = line[(size_t) (i & mask)];
        const float x1 = line[(size_t) ((i + 1) & mask)], x2 = line[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    Settings settings;
    double fs = 44100.0;
    double phase = 0.0, increment = 0.0;
    std::vector<float> lines[2], dryLines[2];   // entrée + feedback ; entrée seule (son direct du Through-Zero)
    int mask = 0, write = 0;
    float last[2] {};
    float smoothedManual = -1.0f;
};
