#pragma once

#include <algorithm>
#include <cmath>

// Compresseur de studio, en C++ pur (testable sans JUCE).
//
//   entrée ─┬───────────────────────────── × gain ── mix avec le son sec ── sortie
//           └─ filtre coupe-bas ─ détecteur ─ courbe (seuil, ratio, genou) ─ lissage (attaque, relâche)
//
// Tout le calcul du gain se fait en décibels (courbe et lissage), ce qui donne une compression
// régulière quel que soit le niveau. Les deux canaux sont liés : l'image stéréo ne bouge pas.
//
// Trois caractères :
//   Clean : feed-forward, détection crête, temps exacts : transparent, précis.
//   Opto  : détection RMS, genou large, relâche en deux temps qui dépend du jeu (comme une cellule optique) : doux, musical.
//   Punch : feedback (le détecteur écoute la sortie, comme les compresseurs à transistors) : les attaques passent, ça claque.
class CompressorEngine
{
public:
    enum Style { clean = 0, opto, punch, numStyles };

    struct Settings
    {
        int style = clean;
        float thresholdDb = -20.0f;
        float ratio = 4.0f;
        float kneeDb = 6.0f;
        float attackMs = 10.0f;
        float releaseMs = 150.0f;
        bool autoRelease = false;    // relâche qui dépend du jeu (toujours en Opto)
        float sidechainHz = 20.0f;   // coupe-bas du détecteur : les basses ne font plus « pomper »
        float makeupDb = 0.0f;
        bool autoMakeup = false;     // rattrape le volume perdu par la compression
        float mix = 1.0f;            // 0 = son sec, 1 = compressé (compression parallèle entre les deux)
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        smoothCoef = coefficient (20.0f);
        reset();
        setSettings (settings);
    }

    void reset()
    {
        for (auto& f : sidechain)
            f = {};
        meanSquare = 0.0f;
        envelope = slowEnvelope = 0.0f;
        currentMakeup = targetMakeup;
        currentMix = settings.mix;
        lastGainReduction = 0.0f;
        lastLevel = -120.0f;
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.ratio = std::max (1.0f, s.ratio);
        settings.kneeDb = std::max (0.0f, s.kneeDb);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);

        const bool opto = settings.style == CompressorEngine::opto;
        knee = settings.kneeDb + (opto ? 6.0f : 0.0f);   // l'opto a toujours un genou très doux

        attackCoef = coefficient (std::max (0.05f, settings.attackMs * (opto ? 1.5f : 1.0f)));
        releaseCoef = coefficient (std::max (5.0f, settings.releaseMs));
        programDependent = opto || settings.autoRelease;

        // Relâche en deux temps : rapide après une attaque isolée, lente après une compression qui dure
        fastReleaseCoef = coefficient (std::clamp (settings.releaseMs * 0.25f, 20.0f, 120.0f));
        // (la partie lente se charge doucement : une note courte n'y laisse presque rien)
        slowAttackCoef  = coefficient (opto ? 1500.0f : 1000.0f);
        slowReleaseCoef = coefficient (std::max (settings.releaseMs * 2.5f, 500.0f));

        rmsCoef = coefficient (opto ? 15.0f : 5.0f);

        // Le feedback reste stable : la boucle (gain ratio) ne doit pas réagir plus vite que 1,5 / ratio par échantillon
        if (settings.style == punch)
            attackCoef = std::max (attackCoef, 1.0f - std::min (1.0f, 1.5f / settings.ratio));

        // Coupe-bas du détecteur : filtre d'état (Butterworth 12 dB/octave), stable même quand on tourne le bouton
        const float g = std::tan (3.14159265f * std::clamp (settings.sidechainHz, 10.0f, 0.45f * (float) fs) / (float) fs);
        svfK = 1.41421356f;
        svfA1 = 1.0f / (1.0f + g * (g + svfK));
        svfA2 = g * svfA1;
        svfA3 = g * svfA2;

        targetMakeup = settings.makeupDb + (settings.autoMakeup ? autoMakeupDb (settings) : 0.0f);
    }

    const Settings& getSettings() const { return settings; }

    // Courbe statique : niveau de sortie (dB) pour un niveau d'entrée (dB), genou doux centré sur le seuil
    static float curve (float inputDb, float thresholdDb, float ratio, float kneeDb)
    {
        const float over = inputDb - thresholdDb;
        if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
        {
            const float x = over + 0.5f * kneeDb;
            return inputDb + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
        }
        return over <= 0.0f ? inputDb : thresholdDb + over / ratio;
    }

    // Réduction de gain (dB, positive) voulue pour ce niveau
    float targetReduction (float levelDb) const
    {
        return levelDb - curve (levelDb, settings.thresholdDb, settings.ratio, knee);
    }

    // Gain automatique : rattrape une bonne partie de la réduction subie par un jeu fort (crêtes vers −12 dBFS)
    static float autoMakeupDb (const Settings& s)
    {
        const float knee = s.kneeDb + (s.style == opto ? 6.0f : 0.0f);
        const float reference = -12.0f;
        return 0.7f * (reference - curve (reference, s.thresholdDb, std::max (1.0f, s.ratio), knee));
    }

    // Traite un bloc en place ; right peut être nul (mono)
    void process (float* left, float* right, int numSamples)
    {
        float maxReduction = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = right != nullptr ? right[i] : dryL;

            // Détecteur sur le signal filtré (coupe-bas)
            const float scL = highPass (0, dryL);
            const float scR = right != nullptr ? highPass (1, dryR) : scL;

            float levelDb;
            if (settings.style == opto)
            {
                meanSquare = rmsCoef * meanSquare + (1.0f - rmsCoef) * 0.5f * (scL * scL + scR * scR);
                levelDb = 10.0f * std::log10 (meanSquare + 1.0e-12f) + 3.0f;   // +3 dB : une sinusoïde lit son niveau crête
            }
            else
            {
                levelDb = 20.0f * std::log10 (std::max (std::abs (scL), std::abs (scR)) + 1.0e-6f);
            }

            // Punch : le détecteur écoute la sortie (niveau d'entrée moins la réduction déjà appliquée)
            const float reduction = settings.style == punch ? feedbackReduction (levelDb) : targetReduction (levelDb);

            // Lissage : attaque quand la réduction augmente, relâche quand elle diminue
            float gainReduction;
            if (programDependent)
            {
                envelope = reduction > envelope ? attackCoef * envelope + (1.0f - attackCoef) * reduction
                                                : fastReleaseCoef * envelope + (1.0f - fastReleaseCoef) * reduction;
                slowEnvelope = envelope > slowEnvelope ? slowAttackCoef * slowEnvelope + (1.0f - slowAttackCoef) * envelope
                                                       : slowReleaseCoef * slowEnvelope + (1.0f - slowReleaseCoef) * envelope;
                gainReduction = std::max (envelope, slowEnvelope);
            }
            else
            {
                envelope = reduction > envelope ? attackCoef * envelope + (1.0f - attackCoef) * reduction
                                                : releaseCoef * envelope + (1.0f - releaseCoef) * reduction;
                gainReduction = envelope;
            }

            currentMakeup = smoothCoef * currentMakeup + (1.0f - smoothCoef) * targetMakeup;
            currentMix    = smoothCoef * currentMix + (1.0f - smoothCoef) * settings.mix;

            const float gain = std::pow (10.0f, (currentMakeup - gainReduction) * 0.05f);
            const float dryGain = 1.0f - currentMix, wetGain = currentMix * gain;

            left[i] = dryL * (dryGain + wetGain);
            if (right != nullptr)
                right[i] = dryR * (dryGain + wetGain);

            maxReduction = std::max (maxReduction, gainReduction);
            lastLevel = levelDb;
        }

        lastGainReduction = maxReduction;
    }

    float getGainReduction() const { return lastGainReduction; }   // dB, la plus forte du dernier bloc
    float getInputLevel() const    { return lastLevel; }           // dB, niveau vu par le détecteur
    float getMakeup() const        { return currentMakeup; }

private:
    float coefficient (float ms) const
    {
        return std::exp (-1.0f / (0.001f * ms * (float) fs));
    }

    // Feedback : la réduction se calcule sur le niveau de sortie, avec une pente (ratio − 1)
    // pour que la courbe statique reste celle du seuil et du ratio affichés
    float feedbackReduction (float inputDb) const
    {
        const float outputDb = inputDb - (programDependent ? std::max (envelope, slowEnvelope) : envelope);
        const float over = outputDb - settings.thresholdDb;
        const float slope = settings.ratio - 1.0f;
        if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
        {
            const float x = over + 0.5f * knee;
            return slope * x * x / (2.0f * knee);
        }
        return over <= 0.0f ? 0.0f : slope * over;
    }

    float highPass (int channel, float x)
    {
        auto& s = sidechain[channel];
        const float v3 = x - s.ic2;
        const float v1 = svfA1 * s.ic1 + svfA2 * v3;
        const float v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return x - svfK * v1 - v2;
    }

    struct FilterState { float ic1 = 0.0f, ic2 = 0.0f; };

    Settings settings;
    double fs = 44100.0;

    float knee = 6.0f;
    float attackCoef = 0.0f, releaseCoef = 0.0f;
    bool programDependent = false;
    float fastReleaseCoef = 0.0f, slowAttackCoef = 0.0f, slowReleaseCoef = 0.0f;
    float rmsCoef = 0.0f, smoothCoef = 0.0f;
    float svfK = 1.41421356f, svfA1 = 1.0f, svfA2 = 0.0f, svfA3 = 0.0f;

    FilterState sidechain[2];
    float meanSquare = 0.0f;
    float envelope = 0.0f, slowEnvelope = 0.0f;
    float targetMakeup = 0.0f, currentMakeup = 0.0f, currentMix = 1.0f;
    float lastGainReduction = 0.0f, lastLevel = -120.0f;
};
