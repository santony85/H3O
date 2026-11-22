#pragma once

#include <algorithm>
#include <cmath>
#include "Harmonizer/PitchShifter.h"

// Octaver, en C++ pur (testable sans JUCE) : une octave et deux octaves en dessous, une octave au-dessus,
// mélangées au son direct.
//
//   Analog : le principe des pédales octaver analogiques. Le signal, filtré pour ne garder que la fondamentale,
//            fait basculer une bascule (flip-flop) à chaque période : elle change d'état une période sur deux,
//            soit la moitié de la fréquence (une octave plus bas). Une deuxième bascule divise encore par deux.
//            La bascule inverse la polarité du signal filtré : l'octave suit le volume et l'attaque de la note.
//            L'octave du dessus vient du redressement double alternance (la valeur absolue double la fréquence).
//            Monophonique : une note à la fois, avec le grain typique des octavers analogiques.
//   Poly   : les octaves par le pitch shifter de l'harmoniseur : les accords restent justes.
//
// Tone : un passe-bas sur les octaves (pas sur le son direct), pour arrondir le grain.
class OctaverEngine
{
public:
    enum Mode { analog = 0, poly, numModes };

    struct Settings
    {
        int mode = analog;
        float dry = 1.0f;        // 0 à 1
        float sub1 = 0.8f;       // une octave en dessous, 0 à 1
        float sub2 = 0.0f;       // deux octaves en dessous, 0 à 1
        float up = 0.0f;         // une octave au-dessus, 0 à 1
        float toneHz = 1200.0f;  // 200 à 8000
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& s : shifters)
            s.prepare (fs, 0.005, windowSeconds);
        reset();
    }

    void reset()
    {
        for (auto& s : shifters)
            s.reset();
        for (auto& c : channels)
            c = Channel();
    }

    void setSettings (const Settings& s)
    {
        settings = s;
        settings.mode = std::clamp (s.mode, 0, numModes - 1);
        settings.dry = std::clamp (s.dry, 0.0f, 1.0f);
        settings.sub1 = std::clamp (s.sub1, 0.0f, 1.0f);
        settings.sub2 = std::clamp (s.sub2, 0.0f, 1.0f);
        settings.up = std::clamp (s.up, 0.0f, 1.0f);
        settings.toneHz = std::clamp (s.toneHz, 200.0f, 8000.0f);
    }

    static constexpr double windowSeconds = 0.040;

    // Traite un bloc en place ; right peut être nul (mono)
    void process (float* left, float* right, int numSamples)
    {
        const int numChannels = right != nullptr ? 2 : 1;
        const float tone = coefficient (settings.toneHz);
        const float track = coefficient (trackingHz);
        const float dcBlock = std::exp (-2.0f * pi * 30.0f / (float) fs);
        const float attack = 1.0f - std::exp (-1.0f / (0.001f * (float) fs));
        const float release = 1.0f - std::exp (-1.0f / (0.060f * (float) fs));
        const float window = (float) (windowSeconds * fs);
        const bool octavesOff = settings.sub1 <= 0.0f && settings.sub2 <= 0.0f && settings.up <= 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = ch == 0 ? left : right;
            auto& c = channels[(size_t) ch];
            auto& shifter = shifters[(size_t) ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const float x = data[i];

                // Sans octave, le son direct passe tel quel
                if (octavesOff)
                {
                    data[i] = settings.dry * x;
                    continue;
                }

                // Le signal sans continu
                const float hp = x - c.dcIn + dcBlock * c.dcState;
                c.dcIn = x;
                c.dcState = hp;

                float s1 = 0.0f, s2 = 0.0f, u = 0.0f;
                if (settings.mode == analog)
                {
                    // La fondamentale : deux passe-bas, puis une bascule de Schmitt dont le seuil suit le volume
                    c.track1 += track * (hp - c.track1);
                    c.track2 += track * (c.track1 - c.track2);
                    const float f = c.track2;

                    const float level = std::abs (f);
                    c.envelope += (level > c.envelope ? attack : release) * (level - c.envelope);
                    const float threshold = 0.15f * c.envelope + 1.0e-5f;

                    if (! c.high && f > threshold)
                    {
                        c.high = true;
                        c.flip1 = -c.flip1;             // une bascule toutes les deux périodes : f / 2
                        if (c.flip1 > 0.0f)
                            c.flip2 = -c.flip2;         // et encore deux fois moins souvent : f / 4
                    }
                    else if (c.high && f < -threshold)
                    {
                        c.high = false;
                    }

                    // La bascule inverse la polarité du signal filtré : l'octave garde son volume et son attaque
                    s1 = c.flip1 * f * 2.0f;
                    s2 = c.flip2 * f * 2.0f;

                    // Une octave au-dessus : valeur absolue (fréquence doublée), sans son continu
                    const float rectified = std::abs (hp) * 2.0f;
                    u = rectified - c.upDcIn + dcBlock * c.upDcState;
                    c.upDcIn = rectified;
                    c.upDcState = u;
                }
                else
                {
                    shifter.write (hp);
                    s1 = settings.sub1 > 0.0f ? shifter.process (c.voices[0], 0.5f, 0.0f, window) : 0.0f;
                    s2 = settings.sub2 > 0.0f ? shifter.process (c.voices[1], 0.25f, 0.0f, window) : 0.0f;
                    u  = settings.up   > 0.0f ? shifter.process (c.voices[2], 2.0f, 0.0f, window * 0.5f) : 0.0f;
                }

                // Tone : deux passe-bas sur les octaves seulement
                const float wet = settings.sub1 * s1 + settings.sub2 * s2 + settings.up * u;
                c.tone1 += tone * (wet - c.tone1);
                c.tone2 += tone * (c.tone1 - c.tone2);

                data[i] = settings.dry * x + c.tone2;
            }
        }
    }

private:
    static constexpr float pi = 3.14159265f;
    static constexpr float trackingHz = 500.0f;   // au-dessus des fondamentales de la guitare, sous leurs harmoniques

    float coefficient (float hz) const { return 1.0f - std::exp (-2.0f * pi * hz / (float) fs); }

    struct Channel
    {
        float dcIn = 0.0f, dcState = 0.0f;
        float track1 = 0.0f, track2 = 0.0f, envelope = 0.0f;
        bool high = false;
        float flip1 = 1.0f, flip2 = 1.0f;
        float upDcIn = 0.0f, upDcState = 0.0f;
        float tone1 = 0.0f, tone2 = 0.0f;
        PitchShifter::Voice voices[3];
    };

    Settings settings;
    double fs = 44100.0;
    PitchShifter shifters[2];
    Channel channels[2];
};
