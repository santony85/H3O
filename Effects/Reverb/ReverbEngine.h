#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Réverbération stéréo façon H3000 (Room, Hall, Plate), construite comme les
// réverbes numériques modernes :
//
//   entrée → pre-delay → premières réflexions (Room / Hall)
//          → diffusion (4 passe-tout par côté : le son s'étale en quelques ms)
//          → réseau de 8 lignes à retard bouclées (« FDN »), mélangées à chaque tour
//            par une matrice de Hadamard (aucune perte, aucun gain : seule la Decay
//            fixe la durée), avec un filtre d'amortissement des aigus dans la boucle
//            et une légère modulation des longueurs (enlève le côté métallique).
//
// C++ pur, sans JUCE, pour pouvoir le tester seul.
class ReverbEngine
{
public:
    enum class Type { room, hall, plate };

    struct Settings
    {
        Type type = Type::hall;
        float preDelayMs = 20.0f;
        float size = 1.0f;          // 0,5 … 1,5 : multiplie la taille de la pièce
        float decaySeconds = 2.5f;  // RT60 : temps pour perdre 60 dB
        float dampHz = 6000.0f;     // les aigus s'éteignent plus vite au-dessus
        float diffusion = 0.8f;     // 0 … 1 : échos distincts → nappe lisse
        float modulation = 0.3f;    // 0 … 1
    };

    static constexpr int numLines = 8;
    static constexpr float maxPreDelayMs = 250.0f;
    static constexpr float maxSize = 1.5f;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;

        preDelay.prepare ((int) (maxPreDelayMs * 0.001f * sr) + 64 + (int) (0.08f * sr));
        for (auto& line : lines)
            line.prepare ((int) (0.080f * maxSize * sr * 1.6f) + 64);
        for (auto& side : diffusers)
            for (auto& ap : side)
                ap.prepare ((int) (0.020f * sr) + 4);

        smoothCoeff = 1.0f - std::exp (-1.0f / (0.1f * sr));   // tailles : glissement ~100 ms
        reset();
    }

    void reset()
    {
        preDelay.clear();
        for (auto& line : lines) { line.clear(); line.lp = 0.0f; }
        for (auto& side : diffusers)
            for (auto& ap : side)
                ap.clear();
        for (int i = 0; i < numLines; ++i)
            lfoPhase[(size_t) i] = 0.37f * (float) i;
        primed = false;
    }

    // À appeler une fois par bloc
    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        const float size = std::clamp (s.size, 0.5f, maxSize);

        // Longueurs des 8 lignes (ms) : nombres premiers entre eux, pour éviter les résonances
        static constexpr float hall[numLines]  = { 31.7f, 37.1f, 41.3f, 43.7f, 53.9f, 59.3f, 67.1f, 73.9f };
        static constexpr float room[numLines]  = { 11.3f, 13.7f, 16.1f, 17.9f, 20.3f, 23.9f, 26.3f, 29.9f };
        static constexpr float plate[numLines] = { 19.1f, 21.7f, 24.7f, 27.1f, 30.7f, 33.1f, 36.7f, 39.7f };
        const float* base = s.type == Type::room ? room : s.type == Type::plate ? plate : hall;

        const float decay = std::max (0.1f, s.decaySeconds);
        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];
            line.target = base[i] * size * 0.001f * sr;
            if (! primed)
                line.length = line.target;
            // Gain par tour pour perdre 60 dB en « decay » secondes
            line.gain = std::pow (10.0f, -3.0f * line.target / (decay * sr));
        }

        // Amortissement des aigus dans la boucle
        // (au maximum, plus aucun amortissement : la durée est la même dans tout le spectre)
        dampCoeff = s.dampHz >= 19500.0f ? 1.0f
                  : 1.0f - std::exp (-2.0f * 3.14159265f * std::clamp (s.dampHz, 500.0f, 0.45f * sr) / sr);

        // Diffusion : la plaque est très dense dès le départ
        const float diffusion = std::clamp (s.diffusion, 0.0f, 1.0f) * (s.type == Type::plate ? 0.78f : 0.7f);
        static constexpr float apMs[2][4] = { { 4.71f, 3.59f, 12.73f, 9.31f }, { 4.97f, 3.31f, 13.39f, 8.83f } };
        const float apScale = s.type == Type::room ? 0.6f : s.type == Type::plate ? 0.8f : 1.0f;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
            {
                auto& ap = diffusers[(size_t) c][(size_t) k];
                ap.length = std::max (1, (int) (apMs[c][k] * apScale * 0.001f * sr));
                ap.gain = diffusion;
            }

        // Premières réflexions : marquées dans une pièce, plus lointaines dans un hall, absentes d'une plaque
        earlyLevel = s.type == Type::room ? 0.5f : s.type == Type::hall ? 0.3f : 0.0f;
        earlyScale = (s.type == Type::hall ? 1.6f : 1.0f) * size;

        modDepth = std::clamp (s.modulation, 0.0f, 1.0f) * 0.0008f * sr * (s.type == Type::plate ? 0.5f : 1.0f);
        preDelaySamples = std::clamp (s.preDelayMs, 0.0f, maxPreDelayMs) * 0.001f * sr;

        primed = true;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        // Pre-delay (deux canaux entrelacés dans la même ligne : simple et suffisant)
        preDelay.push (inL, inR);
        float l = preDelay.readL (preDelaySamples), r = preDelay.readR (preDelaySamples);

        // Premières réflexions
        float earlyL = 0.0f, earlyR = 0.0f;
        if (earlyLevel > 0.0f)
        {
            static constexpr float tapMs[6]   = { 7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 41.9f };
            static constexpr float tapGain[6] = { 0.6f, 0.5f, 0.42f, 0.35f, 0.28f, 0.22f };
            for (int k = 0; k < 6; ++k)
            {
                const float d = preDelaySamples + tapMs[k] * earlyScale * 0.001f * sr;
                if (k % 2 == 0) earlyL += tapGain[k] * preDelay.readL (d) + 0.3f * tapGain[k] * preDelay.readR (d);
                else            earlyR += tapGain[k] * preDelay.readR (d) + 0.3f * tapGain[k] * preDelay.readL (d);
            }
            earlyL *= earlyLevel;
            earlyR *= earlyLevel;
        }

        // Diffusion
        for (auto& ap : diffusers[0]) l = ap.process (l);
        for (auto& ap : diffusers[1]) r = ap.process (r);

        // Lecture des 8 lignes (longueur qui glisse + modulation lente)
        std::array<float, numLines> s {};
        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];
            line.length += smoothCoeff * (line.target - line.length);

            float& ph = lfoPhase[(size_t) i];
            ph += lfoRate[i] / sr;
            if (ph >= 1.0f) ph -= 1.0f;
            const float mod = modDepth * std::sin (6.2831853f * ph);

            const float y = line.read (line.length + mod);
            line.lp += dampCoeff * (y - line.lp);   // les aigus s'éteignent plus vite
            s[(size_t) i] = y;
        }

        // Sorties : deux combinaisons orthogonales des lignes (gauche / droite décorrélées)
        constexpr float norm = 0.35355339f;   // 1 / √8
        const float tailL = (s[0] + s[1] - s[2] - s[3] + s[4] + s[5] - s[6] - s[7]) * norm;
        const float tailR = (s[0] - s[1] + s[2] - s[3] + s[4] - s[5] + s[6] - s[7]) * norm;

        // Retour : amortissement, gain de decay, puis mélange de Hadamard (sans perte)
        std::array<float, numLines> m {};
        for (int i = 0; i < numLines; ++i)
            m[(size_t) i] = lines[(size_t) i].lp * lines[(size_t) i].gain;
        hadamard (m);

        for (int i = 0; i < numLines; ++i)
        {
            // Gauche dans les lignes paires, droite dans les impaires, signes alternés
            const float in = (i % 2 == 0 ? l : r) * ((i / 2) % 2 == 0 ? 1.0f : -1.0f);
            lines[(size_t) i].write (m[(size_t) i] + in * 0.5f);
        }

        outL = earlyL + tailL;
        outR = earlyR + tailR;
    }

private:
    // Hadamard 8 × 8 normalisée (transformée rapide) : orthogonale, conserve l'énergie
    static void hadamard (std::array<float, numLines>& x) noexcept
    {
        for (int h = 1; h < numLines; h <<= 1)
            for (int i = 0; i < numLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b;
                    x[(size_t) (j + h)] = a - b;
                }
        for (auto& v : x)
            v *= 0.35355339f;
    }

    struct Line
    {
        std::vector<float> buf;
        int mask = 0, pos = 0;
        float length = 100.0f, target = 100.0f, gain = 0.0f, lp = 0.0f;

        void prepare (int maxLength)
        {
            int size = 1;
            while (size < maxLength + 8) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        void write (float x) noexcept { pos = (pos + 1) & mask; buf[(size_t) pos] = x; }

        // Lecture « delay » échantillons en arrière (avant l'écriture de ce tour), interpolation d'Hermite
        float read (float delay) const noexcept
        {
            const float p = (float) pos - std::clamp (delay, 2.0f, (float) mask - 4.0f) + 1.0f;
            const float fl = std::floor (p);
            const float t = p - fl;
            const int i = (int) fl;
            const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)];
            const float y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }
    };

    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0, length = 1;
        float gain = 0.5f;

        void prepare (int maxLength) { buf.assign ((size_t) maxLength + 1, 0.0f); }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

        float process (float x) noexcept
        {
            int readPos = pos - length;
            if (readPos < 0) readPos += (int) buf.size();
            const float delayed = buf[(size_t) readPos];
            const float w = x + gain * delayed;
            buf[(size_t) pos] = w;
            if (++pos >= (int) buf.size()) pos = 0;
            return delayed - gain * w;
        }
    };

    struct StereoDelay
    {
        std::vector<float> l, r;
        int mask = 0, pos = 0;

        void prepare (int maxLength)
        {
            int size = 1;
            while (size < maxLength + 8) size <<= 1;
            l.assign ((size_t) size, 0.0f);
            r.assign ((size_t) size, 0.0f);
            mask = size - 1;
        }
        void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
        void push (float a, float b) noexcept { pos = (pos + 1) & mask; l[(size_t) pos] = a; r[(size_t) pos] = b; }

        float readL (float d) const noexcept { return read (l, d); }
        float readR (float d) const noexcept { return read (r, d); }

        float read (const std::vector<float>& b, float d) const noexcept
        {
            const float p = (float) pos - d;
            const float fl = std::floor (p);
            const float t = p - fl;
            const int i = (int) fl;
            return b[(size_t) (i & mask)] + t * (b[(size_t) ((i + 1) & mask)] - b[(size_t) (i & mask)]);
        }
    };

    static constexpr float lfoRate[numLines] = { 0.31f, 0.47f, 0.53f, 0.67f, 0.79f, 0.89f, 0.97f, 1.13f };

    Settings settings;
    StereoDelay preDelay;
    std::array<Line, numLines> lines;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<float, numLines> lfoPhase {};

    float sr = 44100.0f, smoothCoeff = 0.0f, dampCoeff = 1.0f, modDepth = 0.0f;
    float preDelaySamples = 0.0f, earlyLevel = 0.0f, earlyScale = 1.0f;
    bool primed = false;
};
