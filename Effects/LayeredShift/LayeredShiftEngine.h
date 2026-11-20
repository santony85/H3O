#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "Harmonizer/PitchShifter.h"

// Layered Shift façon H3000 : deux couches transposées (A et B), chacune jouée
// deux fois — à gauche et à droite — légèrement désaccordées entre elles (Detune),
// avec un lent balayage de la hauteur (Sweep) décalé entre les quatre voix.
// Chaque couche a son propre délai ; le Feedback renvoie le tout dans l'entrée
// (cascades : chaque répétition est transposée une fois de plus).
//
// Repose sur le pitch shifter de l'Harmonizer (splice par corrélation, déjà testé).
// C++ pur, sans JUCE, pour pouvoir le tester seul.
class LayeredShiftEngine
{
public:
    struct Layer
    {
        float semitones = 0.0f;
        float cents = 0.0f;
        float delayMs = 0.0f;
        float level = 1.0f;
    };

    struct Settings
    {
        Layer a, b;
        float detuneCents = 0.0f;   // écart total gauche / droite
        float sweepRateHz = 0.2f;
        float sweepCents = 0.0f;    // profondeur du balayage (±)
    };

    static constexpr double maxDelaySeconds = 0.5;
    static constexpr double spliceSeconds   = 0.07;   // assez long pour des nappes sans grain

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        shifter.prepare (sampleRate, maxDelaySeconds + 0.01, spliceSeconds);
        smoothCoeff = 1.0f - std::exp (-1.0f / (0.05f * sr));   // délais, niveaux : ~50 ms
        reset();
    }

    void reset()
    {
        shifter.reset();
        for (auto& v : voices)
            PitchShifter::resetVoice (v.state);
        phase = 0.0f;
        lastWet = 0.0f;
        primed = false;
    }

    // À appeler une fois par bloc
    void setSettings (const Settings& s) noexcept
    {
        settings = s;

        for (int i = 0; i < 4; ++i)
        {
            auto& v = voices[(size_t) i];
            const auto& layer = i < 2 ? s.a : s.b;
            v.targetDelay = std::clamp (layer.delayMs, 0.0f, (float) maxDelaySeconds * 1000.0f) * 0.001f * sr;
            v.targetLevel = std::max (0.0f, layer.level);

            if (! primed)
            {
                v.delay = v.targetDelay;
                v.level = v.targetLevel;
            }
        }
        primed = true;
    }

    // x : entrée mono ; feedback 0 … <1 ; sorties gauche / droite (son traité seulement)
    void process (float x, float feedback, float& outL, float& outR) noexcept
    {
        // Retour écrêté en douceur : même poussé à fond, il ne peut pas s'emballer
        shifter.write (x + feedback * lastWet / (1.0f + std::abs (lastWet)));

        phase += settings.sweepRateHz / sr;
        if (phase >= 1.0f)
            phase -= 1.0f;

        const float window = (float) spliceSeconds * sr;
        float l = 0.0f, r = 0.0f;

        for (int i = 0; i < 4; ++i)
        {
            auto& v = voices[(size_t) i];
            v.delay += smoothCoeff * (v.targetDelay - v.delay);
            v.level += smoothCoeff * (v.targetLevel - v.level);

            const float y = shifter.process (v.state, ratioFor (i), v.delay, window) * v.level;
            (i % 2 == 0 ? l : r) += y;
        }

        // Le retour est ramené au niveau d'une seule voix : avec deux couches à 100 %,
        // la boucle garderait sinon un gain supérieur à 1
        const float total = voices[0].level + voices[2].level;
        lastWet = 0.5f * (l + r) / std::max (1.0f, total);
        outL = l;
        outR = r;
    }

    // Hauteur d'une voix en cents (0 = A gauche, 1 = A droite, 2 = B gauche, 3 = B droite),
    // pour une phase de balayage donnée (0 … 1)
    static float centsFor (const Settings& s, int voice, float sweepPhase) noexcept
    {
        const auto& layer = voice < 2 ? s.a : s.b;
        const float side = voice % 2 == 0 ? 0.5f : -0.5f;              // gauche plus haut, droite plus bas
        static constexpr float offsets[4] = { 0.0f, 0.5f, 0.25f, 0.75f };   // quatre balayages décalés
        const float sweep = s.sweepCents * std::sin (6.2831853f * (sweepPhase + offsets[voice]));
        return layer.semitones * 100.0f + layer.cents + side * s.detuneCents + sweep;
    }

private:
    float ratioFor (int voice) const noexcept
    {
        return std::exp2 (centsFor (settings, voice, phase) / 1200.0f);
    }

    struct VoiceState
    {
        PitchShifter::Voice state;
        float delay = 0.0f, targetDelay = 0.0f, level = 0.0f, targetLevel = 0.0f;
    };

    PitchShifter shifter;
    std::array<VoiceState, 4> voices;
    Settings settings;
    float sr = 44100.0f, smoothCoeff = 0.0f, phase = 0.0f, lastWet = 0.0f;
    bool primed = false;
};
