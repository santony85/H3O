#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Ultra-Tap façon H3000 : une seule ligne à retard lue en jusqu'à 24 points (« taps »).
//
//  - Pre-Delay : attente avant la première répétition.
//  - Length    : durée entre le départ et la dernière répétition.
//  - Taps      : nombre de répétitions, réparties sur Length.
//  - Slope     : espacement. 0 = régulier ; positif = les répétitions se resserrent
//                (balle qui rebondit) ; négatif = elles s'espacent (écho qui ralentit).
//  - Taper     : volume des répétitions. Négatif = elles s'éteignent ; positif = elles montent
//                (effet « à l'envers ») ; 0 = toutes au même niveau.
//  - Spread    : les répétitions alternent gauche / droite.
//
// Les positions, niveaux et placements changent en douceur : pas de clic quand on tourne un bouton.
// C++ pur, sans JUCE, pour pouvoir le tester seul.
class UltraTapEngine
{
public:
    static constexpr int maxTaps = 24;

    struct Settings
    {
        float preDelayMs = 0.0f;
        float lengthMs = 600.0f;
        int taps = 8;
        float slope = 0.0f;    // -1 … 1
        float taper = 0.0f;    // -1 … 1
        float spread = 0.0f;   //  0 … 1
    };

    struct Tap
    {
        float timeMs = 0.0f;
        float level = 0.0f;
        float pan = 0.0f;   // -1 = gauche, 1 = droite
    };

    // Les répétitions qu'un réglage produit (aussi utilisé par l'interface pour les dessiner)
    static std::array<Tap, maxTaps> layout (const Settings& s, int& count)
    {
        std::array<Tap, maxTaps> taps {};
        count = std::clamp (s.taps, 1, maxTaps);

        // Slope → exposant de la courbe d'espacement : 1 = régulier
        const float exponent = std::pow (2.0f, -2.0f * std::clamp (s.slope, -1.0f, 1.0f));
        const float taper = std::clamp (s.taper, -1.0f, 1.0f);

        for (int k = 0; k < count; ++k)
        {
            const float u = (float) (k + 1) / (float) count;   // la dernière répétition tombe à Length
            auto& t = taps[(size_t) k];

            t.timeMs = s.preDelayMs + s.lengthMs * std::pow (u, exponent);

            // Position dans la série, de 0 (première) à 1 (dernière)
            const float pos = count > 1 ? (float) k / (float) (count - 1) : 0.0f;
            t.level = taper < 0.0f ? 1.0f + taper * pos * 0.95f            // s'éteignent
                                   : 1.0f - taper * (1.0f - pos) * 0.95f;   // montent

            t.pan = s.spread * ((k % 2 == 0) ? -1.0f : 1.0f);
        }

        return taps;
    }

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        sr = (float) sampleRate;

        int size = 1;
        while (size < (int) (maxDelaySeconds * sampleRate) + 8)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;

        // Lissage : ~80 ms pour les positions, ~20 ms pour les niveaux
        timeCoeff  = 1.0f - std::exp (-1.0f / (0.08f * sr));
        levelCoeff = 1.0f - std::exp (-1.0f / (0.02f * sr));

        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        lastTap = 0.0f;
        primed = false;
    }

    void setSettings (const Settings& s) noexcept
    {
        int count = 0;
        const auto taps = layout (s, count);
        const float maxDelay = (float) mask - 4.0f;

        // Plus il y a de répétitions, plus chacune est discrète (pas de saturation à 24 taps)
        float energy = 0.0f;
        for (int k = 0; k < count; ++k)
            energy += taps[(size_t) k].level * taps[(size_t) k].level;
        const float gain = std::min (1.0f, 2.0f / std::sqrt (std::max (energy, 1.0e-6f)));

        for (int k = 0; k < maxTaps; ++k)
        {
            auto& v = voices[(size_t) k];
            const bool active = k < count;

            // Une répétition qu'on retire garde sa position et s'éteint en fondu
            if (active)
                v.targetDelay = std::clamp (taps[(size_t) k].timeMs * 0.001f * sr, 1.0f, maxDelay);

            const float level = active ? taps[(size_t) k].level * gain : 0.0f;
            const float pan   = active ? taps[(size_t) k].pan : 0.0f;
            v.targetLeft  = level * std::min (1.0f, 1.0f - pan);
            v.targetRight = level * std::min (1.0f, 1.0f + pan);
        }

        lastActive = count - 1;

        if (! primed)
        {
            for (auto& v : voices)
            {
                v.delay = v.targetDelay;
                v.left  = v.targetLeft;
                v.right = v.targetRight;
            }
            primed = true;
        }
    }

    // x : entrée mono ; feedback : renvoie la dernière répétition dans la ligne (0 … <1)
    void process (float x, float feedback, float& outL, float& outR) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x + feedback * lastTap;

        float l = 0.0f, r = 0.0f;

        for (int k = 0; k < maxTaps; ++k)
        {
            auto& v = voices[(size_t) k];
            v.delay += timeCoeff  * (v.targetDelay - v.delay);
            v.left  += levelCoeff * (v.targetLeft  - v.left);
            v.right += levelCoeff * (v.targetRight - v.right);

            if (v.left == 0.0f && v.right == 0.0f)
                continue;

            const float y = read ((float) writePos - v.delay);
            l += v.left * y;
            r += v.right * y;

            if (k == lastActive)
                lastTap = y;
        }

        outL = l;
        outR = r;
    }

private:
    struct Voice
    {
        float delay = 1.0f, targetDelay = 1.0f;
        float left = 0.0f, right = 0.0f, targetLeft = 0.0f, targetRight = 0.0f;
    };

    float read (float pos) const noexcept
    {
        const float fl = std::floor (pos);
        const float t  = pos - fl;
        const int i    = (int) fl;

        const float y0 = buffer[(size_t) ((i - 1) & mask)];
        const float y1 = buffer[(size_t) (i & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)];
        const float y3 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    std::vector<float> buffer;
    std::array<Voice, maxTaps> voices {};
    int mask = 0, writePos = 0, lastActive = 0;
    float sr = 44100.0f, timeCoeff = 0.0f, levelCoeff = 0.0f, lastTap = 0.0f;
    bool primed = false;
};
