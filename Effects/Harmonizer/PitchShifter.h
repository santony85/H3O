#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// Pitch shifter à ligne à retard, le principe des harmoniseurs Eventide :
//
// Le son est écrit en continu dans un tampon circulaire. Une "tête de lecture"
// le relit à une vitesse différente : plus vite → plus aigu, plus lentement → plus grave.
// Comme elle rattrape l'écriture (ou se laisse distancer), elle doit régulièrement
// sauter dans le tampon : c'est le "splice".
//
// Un saut à distance fixe tombe souvent au mauvais moment : l'ancienne et la nouvelle
// position sont en opposition de phase, la note s'annule et on entend des battements.
// Ici, avant chaque saut, on cherche par corrélation la distance de saut où la forme
// d'onde ressemble le plus à l'actuelle, puis on fait un court fondu enchaîné entre
// l'ancienne et la nouvelle tête. Les deux sont alors en phase : le saut devient inaudible.
//
// Aucune dépendance à JUCE : cette classe peut être testée seule (voir Tests/).
class PitchShifter
{
public:
    // État d'une voix (une transposition)
    struct Voice
    {
        float delay = 0.0f;       // délai de la tête active, en échantillons
        float oldDelay = 0.0f;    // délai de l'ancienne tête pendant un fondu
        float lastLo = 0.0f;      // pour suivre les changements du délai de base
        int fadeRemaining = 0;    // échantillons restants du fondu en cours
        bool initialised = false;
    };

    void prepare (double sampleRate, double maxBaseDelaySeconds, double maxWindowSeconds)
    {
        corrLength = std::max (16, (int) std::lround (0.005 * sampleRate));   // 5 ms comparés
        fadeLength = std::max (16, (int) std::lround (0.010 * sampleRate));   // 10 ms de fondu
        minLo = (float) (corrLength + 4);

        // Pire cas : délai de base + fenêtre + course de la tête pendant 4 fondus (ratio 4)
        const int needed = (int) std::ceil ((maxBaseDelaySeconds + maxWindowSeconds) * sampleRate)
                         + corrLength + 4 * fadeLength + 64;

        // Taille en puissance de 2 : le modulo devient un simple masque binaire
        int size = 1;
        while (size < needed)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        maxDelay = (float) (size - 8);
        writePos = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    static void resetVoice (Voice& v) { v = Voice(); }

    // Écrit un échantillon d'entrée (une fois par échantillon, avant les appels à process)
    void write (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;
    }

    // Lit une voix transposée.
    //   ratio      : rapport de fréquence (2.0 = une octave au-dessus, 0.5 = en dessous)
    //   baseDelay  : délai minimal en échantillons
    //   window     : plage de travail de la tête (splice) en échantillons
    float process (Voice& v, float ratio, float baseDelay, float window) noexcept
    {
        const float lo = std::max (baseDelay, minLo);
        const float hi = lo + window;
        const float step = 1.0f - ratio;   // variation du délai par échantillon

        if (! v.initialised)
        {
            v.delay = ratio > 1.0f ? hi : lo;
            v.lastLo = lo;
            v.initialised = true;
        }

        // Si le délai de base change, on décale les têtes d'autant (sans saut)
        const float shift = lo - v.lastLo;
        v.delay += shift;
        v.oldDelay += shift;
        v.lastLo = lo;

        // Faut-il sauter ? On anticipe : l'ancienne tête doit rester valide pendant tout le fondu.
        if (v.fadeRemaining == 0)
        {
            const float delayAfterFade = v.delay + step * (float) fadeLength;
            const bool tooShort = ratio > 1.0f && delayAfterFade < lo;
            const bool tooLong  = ratio < 1.0f && delayAfterFade > hi;

            if (tooShort || tooLong)
            {
                v.oldDelay = v.delay;
                v.delay = findSplice (v.delay, window, tooShort, lo);
                v.fadeRemaining = fadeLength;
            }
        }

        float out;
        if (v.fadeRemaining > 0)
        {
            // Fondu en cosinus : les deux têtes étant en phase, les amplitudes s'additionnent à 1
            const float t = 1.0f - (float) v.fadeRemaining / (float) fadeLength;
            const float gNew = 0.5f - 0.5f * std::cos (pi * t);
            out = gNew * read (v.delay) + (1.0f - gNew) * read (v.oldDelay);
            v.oldDelay = std::clamp (v.oldDelay + step, 3.0f, maxDelay);
            --v.fadeRemaining;
        }
        else
        {
            out = read (v.delay);
        }

        v.delay = std::clamp (v.delay + step, 3.0f, maxDelay);
        return out;
    }

private:
    // Cherche le nouveau délai : un saut d'une demi-fenêtre à une fenêtre entière,
    // à l'endroit où le signal ressemble le plus à ce que la tête actuelle s'apprête à lire.
    float findSplice (float currentDelay, float window, bool jumpBack, float lo) const noexcept
    {
        const int minJump = std::max (1, (int) (window * 0.5f));
        const int maxJump = std::max (minJump, (int) window);

        // Segment de référence : ce que la tête actuelle va lire juste après
        const int refStart = writePos - (int) std::lround (currentDelay);

        float bestScore = -std::numeric_limits<float>::infinity();
        int bestJump = jumpBack ? maxJump : -maxJump;

        for (int j = minJump; j <= maxJump; ++j)
        {
            const int jump = jumpBack ? j : -j;   // jumpBack : délai plus long (plus loin dans le passé)
            const float candidate = currentDelay + (float) jump;

            if (candidate < lo - 0.5f * window || candidate > maxDelay - (float) corrLength)
                continue;

            const int candStart = refStart - jump;
            float xy = 0.0f, yy = 1.0e-9f;

            // Corrélation normalisée, un échantillon sur deux (deux fois moins de calcul)
            for (int k = 0; k < corrLength; k += 2)
            {
                const float x = buffer[(size_t) ((refStart + k) & mask)];
                const float y = buffer[(size_t) ((candStart + k) & mask)];
                xy += x * y;
                yy += y * y;
            }

            const float score = xy / std::sqrt (yy);
            if (score > bestScore)
            {
                bestScore = score;
                bestJump = jump;
            }
        }

        return std::clamp (currentDelay + (float) bestJump, 3.0f, maxDelay);
    }

    // Lecture avec interpolation d'Hermite à 4 points (bien plus propre que linéaire
    // pour la transposition, qui lit en permanence entre deux échantillons)
    float read (float delay) const noexcept
    {
        const float pos = (float) writePos - delay;
        const float fl  = std::floor (pos);
        const float x   = pos - fl;
        const int i     = (int) fl;

        const float y0 = buffer[(size_t) ((i - 1) & mask)];
        const float y1 = buffer[(size_t) ( i      & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)];
        const float y3 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * x + c2) * x + c1) * x + y1;
    }

    static constexpr float pi = 3.14159265358979f;

    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
    int corrLength = 240;
    int fadeLength = 480;
    float minLo = 244.0f;
    float maxDelay = 0.0f;
};
