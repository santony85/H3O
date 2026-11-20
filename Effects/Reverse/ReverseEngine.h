#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Reverse Shift façon H3000 : le son est découpé en tranches de « Length » millisecondes,
// et chaque tranche est rejouée à l'envers, éventuellement transposée.
//
// Deux lectures se chevauchent de moitié, chacune avec une fenêtre de Hann
// (la somme des deux fenêtres vaut toujours 1 : pas de trou ni de clic aux raccords).
// Au début d'une tranche, la lecture part de l'échantillon le plus récent et remonte
// dans le passé à la vitesse « ratio » : ratio 2 = une octave au-dessus, 0,5 = une octave en dessous.
//
// Un moteur par canal. C++ pur, sans JUCE, pour pouvoir le tester seul.
class ReverseEngine
{
public:
    static constexpr float maxRatio = 2.0f;   // +12 demi-tons

    void prepare (double sampleRate, double maxLengthSeconds)
    {
        // La lecture remonte au plus de ratio × longueur dans le passé
        const int needed = (int) std::ceil (maxLengthSeconds * sampleRate * maxRatio) + 16;
        int size = 1;
        while (size < needed)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    // phase : décalage des tranches, de 0 à 1 (utilisé pour que la droite ne coupe pas au même endroit que la gauche)
    void reset (float phase = 0.0f)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        startPhase = phase;
        started = false;
    }

    // length : longueur des tranches en échantillons ; prise en compte au début de chaque tranche
    float process (float x, float length, float ratio) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;

        length = std::max (length, 32.0f);
        ratio  = std::clamp (ratio, 0.25f, maxRatio);

        if (! started)
        {
            // Les deux lectures sont décalées d'une demi-tranche
            for (int g = 0; g < 2; ++g)
            {
                grains[g].length = length;
                grains[g].age    = std::fmod ((startPhase + 0.5f * (float) g) * length, length);
                grains[g].start  = (float) writePos - grains[g].age * ratio;
            }
            started = true;
        }

        float out = 0.0f;

        for (auto& gr : grains)
        {
            if (gr.age >= gr.length)
            {
                gr.age -= gr.length;
                gr.length = length;
                gr.start  = (float) writePos;
            }

            // Fenêtre de Hann : sin² (π t / L)
            const float s = std::sin (3.14159265f * gr.age / gr.length);
            out += s * s * read (gr.start - ratio * gr.age);

            gr.age += 1.0f;
        }

        return out;
    }

private:
    struct Grain
    {
        float start = 0.0f;    // position d'écriture au début de la tranche
        float age = 0.0f;      // échantillons écoulés depuis le début de la tranche
        float length = 1.0f;   // longueur de cette tranche
    };

    // Lecture entre deux échantillons, interpolation d'Hermite à 4 points
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
    int mask = 0, writePos = 0;
    Grain grains[2];
    float startPhase = 0.0f;
    bool started = false;
};
