#pragma once

#include <algorithm>
#include <cmath>

// Swept Filter façon H3000 : un filtre résonant dont la fréquence se déplace
// entre Low et High (en échelle musicale), poussée soit par un LFO, soit par
// l'enveloppe du jeu (auto-wah : plus tu attaques fort, plus le filtre s'ouvre).
//
// Filtre à variable d'état « TPT » (stable même balayé très vite), recalculé à chaque échantillon.
// C++ pur, sans JUCE, pour pouvoir le tester seul.
class SweptFilterEngine
{
public:
    enum class Source { lfo, envelope };
    enum class Type { lowPass, bandPass, highPass };
    enum class Shape { sine, triangle, rampUp, rampDown, square };

    struct Settings
    {
        Source source = Source::lfo;
        Type type = Type::lowPass;
        Shape shape = Shape::sine;
        float lowHz = 300.0f;
        float highHz = 3000.0f;
        float resonance = 0.5f;      // 0 … 1 → Q de 0,7 à 15
        float rateHz = 0.5f;
        float stereoPhase = 0.0f;    // 0 … 1 (1 = la droite en opposition)
        float sensitivityDb = 0.0f;  // enveloppe : -20 … +20 dB
        float attackMs = 10.0f;
        float releaseMs = 200.0f;
        bool downward = false;       // enveloppe : le filtre se ferme quand on attaque
    };

    static float qFor (float resonance) noexcept
    {
        return 0.707f * std::pow (15.0f / 0.707f, std::clamp (resonance, 0.0f, 1.0f));
    }

    // Gain de sortie : un passe-bas ou passe-haut très résonant gonfle d'environ Q
    // à la fréquence de coupure ; on en reprend une partie pour garder un niveau régulier
    static float makeupFor (Type type, float q) noexcept
    {
        return type == Type::bandPass ? std::sqrt (std::max (1.0f, q / 0.707f)) * 0.8f
                                      : 1.0f / std::sqrt (std::max (1.0f, q / 0.707f));
    }

    // Fréquence pour une position 0 … 1 dans la plage Low … High
    static float frequencyFor (const Settings& s, float position) noexcept
    {
        const float lo = std::max (20.0f, std::min (s.lowHz, s.highHz));
        const float hi = std::max (s.lowHz, s.highHz);
        return lo * std::pow (hi / lo, std::clamp (position, 0.0f, 1.0f));
    }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        squareSmooth = 1.0f - std::exp (-1.0f / (0.004f * sr));   // carré adouci : pas de clic
        reset();
    }

    void reset()
    {
        for (auto& c : channels)
            c = Channel();
        phase = 0.0f;
        envelope = 0.0f;
        position[0] = position[1] = 0.0f;
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        q = qFor (s.resonance);
        makeup = makeupFor (s.type, q);
        attackCoeff  = 1.0f - std::exp (-1.0f / (std::max (0.1f, s.attackMs) * 0.001f * sr));
        releaseCoeff = 1.0f - std::exp (-1.0f / (std::max (1.0f, s.releaseMs) * 0.001f * sr));
        envGain = 4.0f * std::pow (10.0f, s.sensitivityDb / 20.0f);
    }

    void process (float& left, float& right) noexcept
    {
        float target[2];

        if (settings.source == Source::lfo)
        {
            phase += settings.rateHz / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            target[0] = lfo (phase);
            float pr = phase + 0.5f * settings.stereoPhase;
            if (pr >= 1.0f) pr -= 1.0f;
            target[1] = lfo (pr);
        }
        else
        {
            // Suiveur d'enveloppe (montée Attack, descente Release), sur le niveau des deux côtés
            const float level = 0.5f * (std::abs (left) + std::abs (right));
            envelope += (level > envelope ? attackCoeff : releaseCoeff) * (level - envelope);
            // Racine : la réponse suit mieux l'oreille (les notes douces ouvrent déjà un peu)
            float p = std::clamp (std::sqrt (envelope * envGain), 0.0f, 1.0f);
            if (settings.downward)
                p = 1.0f - p;
            target[0] = target[1] = p;
        }

        for (int c = 0; c < 2; ++c)
        {
            // La forme carrée est adoucie, les autres sont déjà continues
            if (settings.source == Source::lfo && settings.shape == Shape::square)
                position[c] += squareSmooth * (target[c] - position[c]);
            else
                position[c] = target[c];
        }

        left  = filter (channels[0], left,  frequencyFor (settings, position[0]));
        right = filter (channels[1], right, frequencyFor (settings, position[1]));
    }

    float getPosition (int channel) const noexcept { return position[channel & 1]; }

private:
    struct Channel { float ic1 = 0.0f, ic2 = 0.0f; };

    float lfo (float ph) const noexcept
    {
        switch (settings.shape)
        {
            case Shape::sine:     return 0.5f - 0.5f * std::cos (6.2831853f * ph);   // part du bas
            case Shape::triangle: return ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;
            case Shape::rampUp:   return ph;
            case Shape::rampDown: return 1.0f - ph;
            case Shape::square:   return ph < 0.5f ? 0.0f : 1.0f;
        }
        return 0.0f;
    }

    float filter (Channel& c, float x, float frequency) const noexcept
    {
        const float g  = std::tan (3.14159265f * std::min (frequency, 0.45f * sr) / sr);
        const float k  = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float v3 = x - c.ic2;
        const float v1 = a1 * c.ic1 + a2 * v3;
        const float v2 = c.ic2 + a2 * c.ic1 + a3 * v3;
        c.ic1 = 2.0f * v1 - c.ic1;
        c.ic2 = 2.0f * v2 - c.ic2;

        switch (settings.type)
        {
            case Type::lowPass:  return v2 * makeup;
            case Type::bandPass: return k * v1 * makeup;   // gain 1 au centre, puis compensation
            case Type::highPass: return (x - k * v1 - v2) * makeup;
        }
        return x;
    }

    Settings settings;
    Channel channels[2];
    float sr = 44100.0f, q = 0.707f, makeup = 1.0f, phase = 0.0f;
    float envelope = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f, envGain = 4.0f, squareSmooth = 0.0f;
    float position[2] = { 0.0f, 0.0f };
};
