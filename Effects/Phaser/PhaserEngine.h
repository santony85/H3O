#pragma once

#include <algorithm>
#include <cmath>

// Phaser stéréo, en C++ pur (testable sans JUCE).
//
// Le son passe dans une série de filtres passe-tout (2 à 12 étages) dont la fréquence balaie
// le spectre au rythme d'une onde ; mélangé au son direct, il crée des creux (« notches »)
// qui montent et descendent : le « whoosh » du phaser. 4 étages = 2 creux, 12 étages = 6 creux.
//
//   Feedback : renvoie la sortie à l'entrée ; creuse et colore les creux (négatif = son plus nasal)
//   Stereo Phase : décale l'onde de droite (180° = les creux montent à gauche quand ils descendent à droite)
class PhaserEngine
{
public:
    static constexpr int maxStages = 12;
    enum Shape { sine = 0, triangle, numShapes };

    struct Settings
    {
        int stages = 4;              // 2 à 12, nombre pair
        int shape = sine;
        float depth = 0.7f;          // 0 à 1 : largeur du balayage (jusqu'à ±3 octaves)
        float centreHz = 800.0f;     // centre du balayage
        float feedback = 0.0f;       // −0,9 à 0,9
        float stereoPhase = 0.0f;    // degrés, 0 à 180
        float mix = 0.5f;            // 0,5 = phaser classique (creux les plus profonds)
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        for (auto& channel : state)
            std::fill (std::begin (channel), std::end (channel), 0.0f);
        last[0] = last[1] = 0.0f;
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.stages = std::clamp (s.stages - s.stages % 2, 2, maxStages);
        settings.depth = std::clamp (s.depth, 0.0f, 1.0f);
        settings.feedback = std::clamp (s.feedback, -0.9f, 0.9f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
        settings.centreHz = std::clamp (s.centreHz, 50.0f, 8000.0f);
    }

    void setRate (float hz) { increment = std::max (0.0, (double) hz) / fs; }

    // Calé sur le morceau : phase de l'onde (0 à 1) donnée par la position de lecture
    void setPhase (double newPhase) { phase = newPhase - std::floor (newPhase); }
    double getPhase() const { return phase; }

    // Onde unipolaire (0 à 1) : 0 = creux en bas du balayage, 1 = en haut
    static float wave (int shape, double p)
    {
        p -= std::floor (p);
        if (shape == triangle)
            return (float) (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
        return (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * p));
    }

    // Fréquence des passe-tout pour une position de l'onde (Hz)
    float sweepFrequency (float w) const
    {
        const float octaves = 3.0f * settings.depth * (2.0f * w - 1.0f);
        return std::clamp (settings.centreHz * std::exp2 (octaves), 20.0f, (float) fs * 0.45f);
    }

    // Traite un bloc en place ; right peut être nul (mono)
    void process (float* left, float* right, int numSamples)
    {
        const double offset = settings.stereoPhase / 360.0;
        const float dryGain = 1.0f - settings.mix, wetGain = settings.mix;
        const float k = 3.14159265f / (float) fs;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < (right != nullptr ? 2 : 1); ++ch)
            {
                float* data = ch == 0 ? left : right;
                const float w = wave (settings.shape, phase + (ch == 0 ? 0.0 : offset));
                const float t = std::tan (k * sweepFrequency (w));
                const float a = (t - 1.0f) / (t + 1.0f);

                const float dry = data[i];
                float x = dry + settings.feedback * last[ch];
                auto* s = state[ch];
                for (int n = 0; n < settings.stages; ++n)
                {
                    // Passe-tout du premier ordre : y = a·x + s ; s = x − a·y
                    const float y = a * x + s[n];
                    s[n] = x - a * y;
                    x = y;
                }
                last[ch] = x;
                data[i] = dryGain * dry + wetGain * x;
            }

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        // Pas de bruit de fond infini (dénormaux) quand le son s'arrête
        for (auto& channel : state)
            for (auto& v : channel)
                if (std::abs (v) < 1.0e-15f)
                    v = 0.0f;
    }

private:
    Settings settings;
    double fs = 44100.0;
    double phase = 0.0, increment = 0.0;
    float state[2][maxStages] {};
    float last[2] {};
};
