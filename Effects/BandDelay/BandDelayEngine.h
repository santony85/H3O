#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Band Delay façon H3000 : jusqu'à 8 échos, chacun passé dans son propre filtre
// passe-bande résonant, avec son propre retour. Chaque écho a donc sa « couleur »
// (grave, médium, aigu…), et ses répétitions deviennent de plus en plus pures,
// comme une wah figée.
//
//  - Time      : la dernière bande sonne à Time ; les autres sont réparties avant.
//  - Low / High: fréquences des filtres, de la bande la plus grave à la plus aiguë.
//  - Pattern   : ordre d'arrivée des couleurs (montante, descendante, alternée).
//  - Resonance : largeur des filtres (Q de 0,7 à 12).
//  - Spread    : les bandes alternent gauche / droite.
//
// Les changements glissent en douceur. C++ pur, sans JUCE, pour pouvoir le tester seul.
class BandDelayEngine
{
public:
    static constexpr int maxBands = 8;

    enum class Pattern { rising, falling, alternating };

    struct Settings
    {
        int bands = 4;
        float timeMs = 800.0f;
        float lowHz = 300.0f;
        float highHz = 3000.0f;
        float resonance = 0.5f;   // 0 … 1
        Pattern pattern = Pattern::rising;
        float spread = 0.0f;      // 0 … 1
    };

    struct Band
    {
        float timeMs = 0.0f;
        float frequency = 1000.0f;
        float pan = 0.0f;   // -1 = gauche, 1 = droite
    };

    static float qFor (float resonance) noexcept
    {
        return 0.7f * std::pow (12.0f / 0.7f, std::clamp (resonance, 0.0f, 1.0f));
    }

    // Les bandes qu'un réglage produit (aussi utilisé par l'interface pour les dessiner)
    static std::array<Band, maxBands> layout (const Settings& s, int& count)
    {
        std::array<Band, maxBands> bands {};
        count = std::clamp (s.bands, 1, maxBands);

        const float lo = std::max (20.0f, std::min (s.lowHz, s.highHz));
        const float hi = std::max (s.lowHz, s.highHz);

        for (int k = 0; k < count; ++k)
        {
            // Rang de couleur : 0 = la plus grave, count-1 = la plus aiguë
            int rank = k;
            if (s.pattern == Pattern::falling)
                rank = count - 1 - k;
            else if (s.pattern == Pattern::alternating)
                rank = (k % 2 == 0) ? k / 2 : count - 1 - k / 2;   // grave, aigu, grave, aigu…

            const float u = count > 1 ? (float) rank / (float) (count - 1) : 0.5f;
            auto& b = bands[(size_t) k];
            b.timeMs    = s.timeMs * (float) (k + 1) / (float) count;
            b.frequency = lo * std::pow (hi / lo, u);   // réparties en échelle musicale
            b.pan       = s.spread * ((k % 2 == 0) ? -1.0f : 1.0f);
        }

        return bands;
    }

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        sr = (float) sampleRate;

        int size = 1;
        while (size < (int) (maxDelaySeconds * sampleRate) + 8)
            size <<= 1;

        for (auto& v : voices)
            v.line.assign ((size_t) size, 0.0f);
        mask = size - 1;

        timeCoeff  = 1.0f - std::exp (-1.0f / (0.08f * sr));   // positions : ~80 ms
        levelCoeff = 1.0f - std::exp (-1.0f / (0.02f * sr));   // niveaux : ~20 ms

        reset();
    }

    void reset()
    {
        for (auto& v : voices)
        {
            std::fill (v.line.begin(), v.line.end(), 0.0f);
            v.ic1 = v.ic2 = v.lastOut = 0.0f;
        }
        writePos = 0;
        primed = false;
    }

    // À appeler une fois par bloc
    void setSettings (const Settings& s) noexcept
    {
        int count = 0;
        const auto bands = layout (s, count);
        const float maxDelay = (float) mask - 4.0f;
        const float q = qFor (s.resonance);

        // Un passe-bande étroit laisse passer moins d'énergie : on compense en partie.
        // Et plus il y a de bandes, plus chacune est discrète.
        const float makeup = std::sqrt (q / 0.7f);
        const float gain = std::min (1.0f, 2.0f / std::sqrt ((float) count)) * std::min (makeup, 4.0f);

        for (int k = 0; k < maxBands; ++k)
        {
            auto& v = voices[(size_t) k];
            const bool active = k < count;

            if (active)
            {
                const auto& b = bands[(size_t) k];
                v.targetDelay = std::clamp (b.timeMs * 0.001f * sr, 1.0f, maxDelay);
                v.frequency   = std::clamp (b.frequency, 20.0f, 0.45f * sr);
            }

            const float pan = active ? bands[(size_t) k].pan : 0.0f;
            const float level = active ? gain : 0.0f;
            v.targetLeft  = level * std::min (1.0f, 1.0f - pan);
            v.targetRight = level * std::min (1.0f, 1.0f + pan);

            // Filtre passe-bande (variable d'état « TPT »), gain de 1 à la fréquence centrale
            const float g = std::tan (3.14159265f * v.frequency / sr);
            v.k  = 1.0f / q;
            v.a1 = 1.0f / (1.0f + g * (g + v.k));
            v.a2 = g * v.a1;
            v.a3 = g * v.a2;
        }

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

    // x : entrée mono ; feedback : chaque bande renvoie son écho filtré dans sa propre ligne (0 … <1)
    void process (float x, float feedback, float& outL, float& outR) noexcept
    {
        writePos = (writePos + 1) & mask;
        float l = 0.0f, r = 0.0f;

        for (auto& v : voices)
        {
            v.delay += timeCoeff  * (v.targetDelay - v.delay);
            v.left  += levelCoeff * (v.targetLeft  - v.left);
            v.right += levelCoeff * (v.targetRight - v.right);

            v.line[(size_t) writePos] = x + feedback * v.lastOut;

            if (v.left == 0.0f && v.right == 0.0f)
            {
                v.lastOut = 0.0f;
                continue;
            }

            const float y = bandpass (v, read (v, (float) writePos - v.delay));
            v.lastOut = y;
            l += v.left * y;
            r += v.right * y;
        }

        outL = l;
        outR = r;
    }

private:
    struct Voice
    {
        std::vector<float> line;
        float delay = 1.0f, targetDelay = 1.0f;
        float left = 0.0f, right = 0.0f, targetLeft = 0.0f, targetRight = 0.0f;
        float frequency = 1000.0f;
        float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
        float lastOut = 0.0f;
    };

    static float bandpass (Voice& v, float x) noexcept
    {
        const float v3 = x - v.ic2;
        const float v1 = v.a1 * v.ic1 + v.a2 * v3;
        const float v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
        v.ic1 = 2.0f * v1 - v.ic1;
        v.ic2 = 2.0f * v2 - v.ic2;
        return v.k * v1;   // passe-bande normalisé : gain 1 au centre
    }

    float read (const Voice& v, float pos) const noexcept
    {
        const float fl = std::floor (pos);
        const float t  = pos - fl;
        const int i    = (int) fl;

        const float y0 = v.line[(size_t) ((i - 1) & mask)];
        const float y1 = v.line[(size_t) (i & mask)];
        const float y2 = v.line[(size_t) ((i + 1) & mask)];
        const float y3 = v.line[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    std::array<Voice, maxBands> voices;
    int mask = 0, writePos = 0;
    float sr = 44100.0f, timeCoeff = 0.0f, levelCoeff = 0.0f;
    bool primed = false;
};
