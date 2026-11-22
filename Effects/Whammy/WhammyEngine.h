#pragma once

#include <algorithm>
#include <cmath>
#include "Harmonizer/PitchShifter.h"

// Pitch shifter à pédale (« whammy »), en C++ pur (testable sans JUCE).
//
// Une pédale d'expression (0 = talon, 1 = pointe) fait glisser la transposition d'un intervalle à l'autre :
// de 0 à +12 demi-tons, la note monte d'une octave quand on appuie ; de 0 à −24, un « dive bomb ».
//
//   Whammy : la note transposée suit la pédale ; Mix 100 % = seulement la note transposée,
//            Mix 50 % = le son direct plus l'harmonie (les intervalles d'harmonie des pédales whammy)
//   Detune : deux voix très légèrement désaccordées (gauche plus haut, droite plus bas) : un doublage épais
//   Glide  : le temps que met la note pour suivre la pédale (0 = instantané, plus = un glissé de corde)
//
// Le décalage de la tête de lecture se fait par le PitchShifter de l'harmoniseur (épissures en phase).
// À l'unisson (transposition nulle), le son direct remplace la voix transposée : pas d'effet de peigne.
class WhammyEngine
{
public:
    enum Mode { whammy = 0, detune, numModes };

    struct Settings
    {
        int mode = whammy;
        float heel = 0.0f;          // demi-tons, pédale levée
        float toe = 12.0f;          // demi-tons, pédale enfoncée
        float pedal = 0.0f;         // 0 à 1
        float glideMs = 60.0f;      // 0 à 1000
        float detuneCents = 10.0f;  // mode Detune : écart de chaque voix
        float mix = 1.0f;           // 0 à 1
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& s : shifters)
            s.prepare (fs, 0.01, windowSeconds);
        reset();
    }

    void reset()
    {
        for (auto& s : shifters)
            s.reset();
        for (auto& v : voices)
            PitchShifter::resetVoice (v);
        smoothedSemitones = targetSemitones();
        shiftAmount = 0.0f;
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.mode = std::clamp (s.mode, 0, numModes - 1);
        settings.heel = std::clamp (s.heel, -24.0f, 24.0f);
        settings.toe = std::clamp (s.toe, -24.0f, 24.0f);
        settings.pedal = std::clamp (s.pedal, 0.0f, 1.0f);
        settings.glideMs = std::clamp (s.glideMs, 0.0f, 1000.0f);
        settings.detuneCents = std::clamp (s.detuneCents, 0.0f, 50.0f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
    }

    // La transposition visée par la pédale, et celle entendue en ce moment (après le glissé)
    float targetSemitones() const { return settings.heel + (settings.toe - settings.heel) * settings.pedal; }
    float currentSemitones() const { return smoothedSemitones; }

    // Retard de la voix transposée (ms) : ce qu'il faut à la tête de lecture pour travailler
    static constexpr double windowSeconds = 0.030;

    // Traite un bloc en place ; right peut être nul (mono)
    void process (float* left, float* right, int numSamples)
    {
        const int channels = right != nullptr ? 2 : 1;
        const float window = (float) (windowSeconds * fs);
        const float glide = settings.glideMs <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (0.001f * settings.glideMs * (float) fs));
        const float target = targetSemitones();
        const float dryGain = 1.0f - settings.mix, wetGain = settings.mix;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedSemitones += glide * (target - smoothedSemitones);

            // Près de l'unisson, la voix transposée cède la place au son direct (fondu sur un quart de ton)
            float semis[2] { smoothedSemitones, smoothedSemitones };
            if (settings.mode == detune)
            {
                semis[0] = settings.detuneCents * 0.01f;
                semis[1] = -settings.detuneCents * 0.01f;
            }
            const float amount = std::min (1.0f, std::max (std::abs (semis[0]), std::abs (semis[1])) * 4.0f);
            shiftAmount += 0.002f * (amount - shiftAmount);

            for (int ch = 0; ch < channels; ++ch)
            {
                float* data = ch == 0 ? left : right;
                const float x = data[i];
                auto& shifter = shifters[(size_t) ch];
                shifter.write (x);
                const float ratio = std::exp2 (semis[ch] / 12.0f);
                const float shifted = shifter.process (voices[(size_t) ch], ratio, 0.0f, window);
                const float wet = shiftAmount * shifted + (1.0f - shiftAmount) * x;
                data[i] = dryGain * x + wetGain * wet;
            }
        }
    }

private:
    Settings settings;
    double fs = 44100.0;
    PitchShifter shifters[2];
    PitchShifter::Voice voices[2];
    float smoothedSemitones = 0.0f;
    float shiftAmount = 0.0f;
};
