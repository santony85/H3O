#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Moteur de chorus multi-voix, en C++ pur (sans JUCE) pour pouvoir le tester seul.
//
// Chaque voix relit l'entrée avec un retard qui oscille (LFO sinusoïdal) :
// le retard qui varie fait légèrement monter et descendre la hauteur, et le
// mélange avec le son direct donne l'épaississement du chorus.
//
// Pour un son riche et large :
//  - jusqu'à 4 voix par côté, chacune avec son propre LFO légèrement plus rapide
//    que la précédente : leurs phases glissent sans cesse, le son ne « tourne » jamais en boucle ;
//  - le côté droit lit ses LFO en quadrature (décalés d'un quart de cycle) :
//    quand la gauche monte, la droite est au sommet, d'où l'image stéréo très large ;
//  - interpolation d'Hermite à 4 points, comme dans le harmonizer.
class ChorusEngine
{
public:
    static constexpr int maxVoices = 4;

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        sr = (float) sampleRate;

        int size = 1;
        while (size < (int) (maxDelaySeconds * sampleRate) + 64)
            size <<= 1;

        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);

        mask = size - 1;
        maxDelay = (float) (size - 8);

        // Petit décalage fixe entre les voix pour qu'elles ne se superposent jamais exactement
        for (int v = 0; v < maxVoices; ++v)
            voiceOffset[v] = (float) v * 0.0017f * sr;

        reset();
    }

    void reset()
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);

        writePos = 0;

        for (int v = 0; v < maxVoices; ++v)
        {
            phase[v] = (float) v / (float) maxVoices;
            gain[v]  = v < voiceCount ? 1.0f : 0.0f;
        }

        norm = 1.0f / std::sqrt ((float) voiceCount);
        feedbackL = feedbackR = 0.0f;
    }

    // À appeler une fois par bloc
    void setRate (float hz)
    {
        // Chaque voix tourne un peu plus vite que la précédente (+11 %)
        for (int v = 0; v < maxVoices; ++v)
            phaseInc[v] = hz * (1.0f + 0.11f * (float) v) / sr;
    }

    void setVoices (int n) { voiceCount = std::clamp (n, 1, maxVoices); }

    // baseDelay et depth en échantillons ; feedback entre -1 et 1.
    // Renvoie le son traité seul (wet) des deux côtés.
    void process (float inL, float inR, float baseDelay, float depth, float feedback,
                  float& wetL, float& wetR) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[0][(size_t) writePos] = inL + feedback * feedbackL;
        buffer[1][(size_t) writePos] = inR + feedback * feedbackR;

        // Les voix ajoutées ou retirées arrivent en fondu (~20 ms) : aucun clic
        const float targetNorm = 1.0f / std::sqrt ((float) voiceCount);
        norm += (targetNorm - norm) * gainCoeff;

        float sumL = 0.0f, sumR = 0.0f;

        for (int v = 0; v < maxVoices; ++v)
        {
            const float target = v < voiceCount ? 1.0f : 0.0f;
            gain[v] += (target - gain[v]) * gainCoeff;

            phase[v] += phaseInc[v];
            if (phase[v] >= 1.0f)
                phase[v] -= 1.0f;

            if (gain[v] < 1.0e-4f)
                continue;

            const float angle = twoPi * phase[v];
            const float modL  = 0.5f * (1.0f + std::sin (angle));   // de 0 à 1
            const float modR  = 0.5f * (1.0f + std::cos (angle));   // quadrature

            const float base = baseDelay + voiceOffset[v];
            const float outL = read (0, clampDelay (base + depth * modL));
            const float outR = read (1, clampDelay (base + depth * modR));

            // Le feedback ne repart que de la première voix : stable même avec 4 voix
            if (v == 0)
            {
                feedbackL = outL;
                feedbackR = outR;
            }

            sumL += gain[v] * outL;
            sumR += gain[v] * outR;
        }

        wetL = sumL * norm;
        wetR = sumR * norm;
    }

private:
    static constexpr float twoPi = 6.28318530717958647692f;
    static constexpr float gainCoeff = 0.0012f;

    float clampDelay (float d) const noexcept { return std::clamp (d, 2.0f, maxDelay); }

    float read (int ch, float delay) const noexcept
    {
        const auto& b   = buffer[ch];
        const float pos = (float) writePos - delay;
        const float fl  = std::floor (pos);
        const float x   = pos - fl;
        const int i     = (int) fl;

        const float y0 = b[(size_t) ((i - 1) & mask)];
        const float y1 = b[(size_t) ( i      & mask)];
        const float y2 = b[(size_t) ((i + 1) & mask)];
        const float y3 = b[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * x + c2) * x + c1) * x + y1;
    }

    std::vector<float> buffer[2];
    int writePos = 0, mask = 0;
    float sr = 44100.0f, maxDelay = 0.0f;

    int voiceCount = 2;
    float phase[maxVoices] {}, phaseInc[maxVoices] {}, gain[maxVoices] {}, voiceOffset[maxVoices] {};
    float norm = 1.0f;
    float feedbackL = 0.0f, feedbackR = 0.0f;
};
