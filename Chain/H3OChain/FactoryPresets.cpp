#include "FactoryPresets.h"
#include "ChainProcessor.h"
#include "ChainPresets.h"

namespace
{
    using namespace ChainRouting;

    struct P { const char* id; float value; };   // réglage en unités réelles (Hz, ms, %, index d'un choix)

    struct Fx
    {
        Effect effect;
        int program = -1;            // preset d'usine de l'effet, -1 : ses réglages par défaut
        std::vector<P> params {};    // retouches après le preset d'usine
        Lane lane = pre;
    };

    struct Mod { ModulationSources::Source source; const char* destination; float amount; };

    struct Recipe
    {
        const char* category;
        const char* name;
        std::vector<Fx> effects;     // allumés, dans l'ordre du signal ; les autres sont éteints
        std::vector<P> chain {};     // réglages de la chaîne (branches, LFO…)
        std::vector<Mod> mods {};
        const char* cables = nullptr;   // baie de brassage : "input>amp.0 amp>delay.0 …" (port .1 = entrée B d'un mixeur)
    };

    // Raccourcis lisibles pour les recettes
    constexpr auto lfo1 = ModulationSources::lfo1, lfo2 = ModulationSources::lfo2, envelope = ModulationSources::envelope,
                   pitch = ModulationSources::pitch, random = ModulationSources::random;

    // Choix des effets
    enum Key { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };
    enum Scale { major, naturalMinor, harmonicMinor, dorian, phrygian, lydian, mixolydian, locrian, majorPenta, minorPenta };
    enum ReverbType { room, hall, plate };

    // Divisions et formes des LFO de la matrice
    enum LfoDivision { bars8, bars4, bars2, bar1, l2dot, l2, l2t, l4dot, l4, l4t, l8dot, l8, l8t, l16, l16t };
    enum LfoShape { sine, triangle, square, sawUp, sawDown };

    std::vector<P> operator+ (std::vector<P> a, const std::vector<P>& b)
    {
        a.insert (a.end(), b.begin(), b.end());
        return a;
    }

    // Harmonies diatoniques : intervalles en degrés de la gamme (2 = tierce, 4 = quinte, 7 = octave, négatif = en dessous)
    std::vector<P> diatonic (Key key, Scale scale, float a, float b, float mix, float bLevel = 100.0f)
    {
        return { { "mode", 1 }, { "key", (float) key }, { "scale", (float) scale },
                 { "adegrees", a }, { "bdegrees", b }, { "acents", 4 }, { "bcents", -4 },
                 { "adelay", 8 }, { "bdelay", 16 }, { "blevel", bLevel }, { "mix", mix } };
    }

    // Les sections du routage : avant les branches (par défaut), branche A, branche B, après les branches
    Fx inA   (Effect e, int program = -1, std::vector<P> p = {}) { return { e, program, std::move (p), branchA }; }
    Fx inB   (Effect e, int program = -1, std::vector<P> p = {}) { return { e, program, std::move (p), branchB }; }
    Fx after (Effect e, int program = -1, std::vector<P> p = {}) { return { e, program, std::move (p), post }; }

    // Deux branches : balance (0 = tout A, 100 = tout B), écart stéréo (A à gauche, B à droite), réinjection croisée
    std::vector<P> split (float balance, float spread, float cross = 0.0f)
    {
        return { { "branchbalance", balance }, { "branchspread", spread }, { "crossfeedback", cross } };
    }

    // Wet/Dry/Wet : le son sec au centre (A vide), les effets de B à 100 %, côte à côte
    std::vector<P> wetDryWet (float balance = 50.0f, float width = 140.0f, float cross = 0.0f)
    {
        return { { "wetonly", 1 }, { "branchspread", 0 }, { "branchbalance", balance }, { "branchwidth", width }, { "crossfeedback", cross } };
    }

    // Les LFO de la matrice : libres (en Hz) ou calés sur le tempo
    std::vector<P> lfo (int n, float rate, LfoShape shape)
    {
        return { { n == 1 ? "lfo1rate" : "lfo2rate", rate }, { n == 1 ? "lfo1shape" : "lfo2shape", (float) shape },
                 { n == 1 ? "lfo1sync" : "lfo2sync", 0 } };
    }

    std::vector<P> lfoSync (int n, LfoDivision division, LfoShape shape)
    {
        return { { n == 1 ? "lfo1division" : "lfo2division", (float) division }, { n == 1 ? "lfo1shape" : "lfo2shape", (float) shape },
                 { n == 1 ? "lfo1sync" : "lfo2sync", 1 } };
    }

    std::vector<P> env (float attack, float release) { return { { "envattack", attack }, { "envrelease", release } }; }

    const char* const balance = "branches/branchbalance";
    const char* const spread  = "branches/branchspread";
    const char* const width   = "branches/branchwidth";
    const char* const cross   = "branches/crossfeedback";

    // Un filtre fixe (LFO presque arrêté sur une plage étroite) : passe-bas ou passe-bande
    std::vector<P> fixedFilter (int type, float low, float high, float resonance = 30.0f)
    {
        return { { "source", 0 }, { "type", (float) type }, { "rate", 0.02f }, { "low", low }, { "high", high },
                 { "resonance", resonance }, { "mix", 100 } };
    }

    const std::vector<Recipe>& recipes()
    {
        static const std::vector<Recipe> list {
            //==================================================================
            // Harmonies diatoniques : l'harmonie dans une branche, ses échos dans l'autre
            { "Diatonic Harmonies", "Twin Thirds Split (E Minor)",
              { inA (harmonizer, -1, diatonic (E, naturalMinor, 2, 0, 100, 0)),
                after (delay, 6, { { "mix", 20 } }), after (reverb, 0, { { "mix", 18 } }) },
              split (45, 100) },
            { "Diatonic Harmonies", "Thirds & Fifths Wide (A Minor)",
              { inA (harmonizer, -1, diatonic (A, naturalMinor, 2, 4, 100)), inA (delay, 9, { { "mix", 25 } }),
                inB (chorus, 3, { { "mix", 40 } }),
                after (reverb, 4, { { "mix", 18 } }) },
              split (50, 70) },
            { "Diatonic Harmonies", "Sixths Below & Reverse (G Major)",
              { { harmonizer, -1, diatonic (G, major, -5, 0, 45, 0) },
                inA (delay, 6, { { "mix", 30 } }), inB (reverse, 0, { { "mix", 100 } }),
                after (reverb, 3, { { "mix", 20 } }) },
              split (35, 60) },
            { "Diatonic Harmonies", "Harmonic Minor Cascade (A)",
              { inA (harmonizer, -1, diatonic (A, harmonicMinor, 2, 4, 100)), inA (ultraTap, 0, { { "mix", 30 } }),
                after (reverb, 0, { { "mix", 18 } }) },
              split (45, 80, 15) },
            { "Diatonic Harmonies", "Dorian Fourths & Bands (D)",
              { { harmonizer, -1, diatonic (D, dorian, 3, -4, 45) },
                inA (bandDelay, 0, { { "mix", 35 } }), inB (delay, 10, { { "mix", 30 } }),
                after (reverb, 2, { { "mix", 18 } }) },
              split (50, 50) },
            { "Diatonic Harmonies", "Southern Sixths Slap (G Mixolydian)",
              { inA (harmonizer, -1, diatonic (G, mixolydian, 2, 5, 100)), inB (delay, 2),
                after (reverb, 1, { { "mix", 15 } }) },
              split (45, 100) },
            { "Diatonic Harmonies", "Pentatonic Stacks & Taps (E Minor)",
              { { harmonizer, -1, diatonic (E, minorPenta, 1, 2, 45) },
                inA (ultraTap, 1, { { "mix", 35 } }), inB (chorus, 0, { { "mix", 40 } }),
                after (reverb, 4, { { "mix", 18 } }) },
              split (50, 60) },
            { "Diatonic Harmonies", "Lydian Dream Pad (C)",
              { { harmonizer, -1, diatonic (C, lydian, 2, 4, 45) },
                inA (layered, 1), inB (reverse, 5, { { "mix", 40 } }),
                after (reverb, 6, { { "mix", 28 } }) },
              split (45, 70) + lfo (1, 0.1f, sine),
              { { lfo1, balance, 25 } } },
            { "Diatonic Harmonies", "Phrygian Shadow Dub (E)",
              { { harmonizer, -1, diatonic (E, phrygian, -2, 4, 45) },
                inA (delay, 4, { { "mix", 35 } }), inB (bandDelay, 5, { { "mix", 35 } }),
                after (reverb, 7, { { "mix", 22 } }) },
              split (50, 70, 20) },
            { "Diatonic Harmonies", "Octave & Tenth Halo (D Major)",
              { inA (harmonizer, -1, diatonic (D, major, 7, 9, 100, 70)), inA (reverb, 6, { { "mix", 50 } }),
                after (delay, 6, { { "mix", 18 } }) },
              split (60, 40) },
            { "Diatonic Harmonies", "Harmony Choir Ensemble (B Minor)",
              { { harmonizer, -1, diatonic (B, naturalMinor, 2, 4, 45) },
                inA (chorus, 1, { { "mix", 50 } }), inB (layered, 1, { { "mix", 40 } }),
                after (reverb, 3, { { "mix", 26 } }) },
              split (50, 80) },
            { "Diatonic Harmonies", "Harmony Echoes Cross-Fed (A Minor)",
              { inA (harmonizer, -1, diatonic (A, naturalMinor, 2, 4, 100)
                                        + std::vector<P> { { "adelay", 90 }, { "bdelay", 180 }, { "feedback", 25 } }),
                inB (delay, 5, { { "mix", 40 } }),
                after (reverb, 0, { { "mix", 18 } }) },
              split (55, 60, 25) },

            //==================================================================
            // Pitch & Detune
            { "Pitch & Detune", "Micro Shift Panorama",
              { { harmonizer, 0 }, inA (delay, 6, { { "mix", 25 } }), inB (ultraTap, 4, { { "mix", 30 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 80) },
            { "Pitch & Detune", "Micro Shift Beside Dry",
              { inA (harmonizer, 0, { { "mix", 100 } }),
                after (delay, 10, { { "mix", 22 } }), after (reverb, 0, { { "mix", 12 } }) },
              split (55, 60) },
            { "Pitch & Detune", "Tight Doubler Wide",
              { inA (harmonizer, -1, { { "acents", 7 }, { "bcents", -7 }, { "adelay", 15 }, { "bdelay", 28 }, { "mix", 100 } }),
                inB (chorus, 3, { { "mix", 30 } }),
                after (reverb, 1, { { "mix", 15 } }) },
              split (50, 100) },
            { "Pitch & Detune", "Octave Down Sub Split",
              { inA (harmonizer, -1, { { "asemi", -12 }, { "acents", 0 }, { "adelay", 0 }, { "blevel", 0 }, { "spread", 0 }, { "mix", 100 } }),
                inA (filter, -1, fixedFilter (0, 600, 700)),
                after (chorus, 3, { { "mix", 25 } }), after (reverb, 1, { { "mix", 15 } }) },
              split (55, 0) },
            { "Pitch & Detune", "Octave Up Sparkle Verb",
              { inA (harmonizer, -1, { { "asemi", 12 }, { "acents", 0 }, { "blevel", 0 }, { "spread", 30 }, { "mix", 100 } }),
                inA (reverb, 4, { { "mix", 60 } }),
                after (delay, 6, { { "mix", 18 } }) },
              split (65, 60) },
            { "Pitch & Detune", "Power Fifths Crunch",
              { { harmonizer, -1, { { "asemi", 7 }, { "acents", 0 }, { "bsemi", -12 }, { "bcents", 0 }, { "blevel", 70 }, { "mix", 45 } } },
                inA (delay, 2), inB (bandDelay, 5, { { "mix", 25 } }),
                after (reverb, 7, { { "mix", 15 } }) },
              split (50, 70) },
            { "Pitch & Detune", "Octaves Both Ways Swirl",
              { { harmonizer, 2 }, inA (chorus, 0), inB (reverse, 2, { { "mix", 35 } }),
                after (reverb, 3, { { "mix", 20 } }) },
              split (45, 80) + lfo (1, 0.15f, sine),
              { { lfo1, balance, 20 } } },
            { "Pitch & Detune", "Octave Cascade Echo",
              { inA (harmonizer, 4, { { "mix", 100 } }), inA (delay, 5, { { "mix", 35 } }),
                after (reverb, 0, { { "mix", 15 } }) },
              split (60, 60, 20) },
            { "Pitch & Detune", "Octave & Fifth Taps",
              { { harmonizer, 1 }, inA (ultraTap, 0, { { "mix", 30 } }), inB (delay, 9, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (50, 80) },
            { "Pitch & Detune", "Layered Shimmer Split",
              { inA (layered, 0, { { "mix", 100 } }), inA (reverb, 3, { { "mix", 60 } }),
                after (chorus, 3, { { "mix", 25 } }) },
              split (60, 50) },
            { "Pitch & Detune", "Wide Detune Pad Pan",
              { { layered, 1 }, inA (chorus, 2, { { "mix", 40 } }), inB (delay, 6, { { "mix", 30 } }),
                after (reverb, 6, { { "mix", 25 } }) },
              split (50, 100) + lfo (1, 0.08f, sine),
              { { lfo1, balance, 30 } } },
            { "Pitch & Detune", "Fifths in Motion Dub",
              { { layered, 3 }, inA (delay, 4, { { "mix", 35 } }), inB (bandDelay, 2, { { "mix", 30 } }),
                after (reverb, 7, { { "mix", 20 } }) },
              split (50, 70, 15) },
            { "Pitch & Detune", "Seasick Reverse",
              { inA (layered, 4, { { "mix", 100 } }), inB (reverse, 6, { { "mix", 50 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (55, 90) },
            { "Pitch & Detune", "Rising Cascade Wash",
              { { layered, 5 }, inA (ultraTap, 6, { { "mix", 35 } }), inB (reverse, 5, { { "mix", 35 } }),
                after (reverb, 6, { { "mix", 28 } }) },
              split (50, 60) },

            //==================================================================
            // Reverse
            { "Reverse", "Backwards Guitar Split",
              { inA (reverse, 0, { { "mix", 100 } }),
                after (delay, 6, { { "mix", 18 } }), after (reverb, 0, { { "mix", 15 } }) },
              split (55, 100) },
            { "Reverse", "Backwards Solo Halo",
              { inA (reverse, 1), after (chorus, 3, { { "mix", 25 } }), after (reverb, 3, { { "mix", 25 } }) },
              split (50, 60) },
            { "Reverse", "Reverse Octave Choir",
              { { harmonizer, -1, { { "asemi", 12 }, { "acents", 0 }, { "blevel", 0 }, { "mix", 30 } } },
                inA (reverse, 2, { { "mix", 60 } }), inB (chorus, 1, { { "mix", 35 } }),
                after (reverb, 6, { { "mix", 25 } }) },
              split (50, 80) },
            { "Reverse", "Reverse Octave Drone",
              { inA (reverse, 3, { { "mix", 70 } }), inA (filter, -1, fixedFilter (0, 500, 600)),
                inB (delay, 4, { { "mix", 30 } }),
                after (reverb, 7, { { "mix", 25 } }) },
              split (50, 70) },
            { "Reverse", "Reverse Fifth Swell Taps",
              { inA (reverse, 4), inB (ultraTap, 3, { { "mix", 35 } }), after (reverb, 3, { { "mix", 22 } }) },
              split (50, 80) },
            { "Reverse", "Long Reverse Cathedral",
              { { reverse, 5 }, inA (delay, 5, { { "mix", 30 } }), inB (layered, 1, { { "mix", 30 } }),
                after (reverb, 0, { { "decay", 7 }, { "mix", 32 } }) },
              split (50, 70, 15) },
            { "Reverse", "Reverse Micro-Detune Chorus",
              { inA (reverse, 6), inB (harmonizer, 0, { { "mix", 40 } }),
                after (chorus, 3, { { "mix", 25 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 90) },
            { "Reverse", "Backwards & Forwards",
              { inA (reverse, 0, { { "length", 600 }, { "mix", 100 } }),
                inB (delay, -1, { { "time", 600 }, { "feedback", 30 }, { "mix", 35 } }),
                after (reverb, 0, { { "mix", 15 } }) },
              split (55, 100) },

            //==================================================================
            // Ultra-Tap
            { "Ultra-Tap", "Tap Rhythm Split",
              { inA (ultraTap, 0), inB (delay, 10, { { "mix", 25 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 80) },
            { "Ultra-Tap", "Bouncing Ball Stereo",
              { inA (ultraTap, 1), inB (bandDelay, 4), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 100) },
            { "Ultra-Tap", "Slowing Echoes Reverse",
              { inA (ultraTap, 2), inB (reverse, 0, { { "mix", 40 } }), after (reverb, 3, { { "mix", 18 } }) },
              split (50, 70) },
            { "Ultra-Tap", "Swelling Taps Shimmer",
              { { ultraTap, 3 }, inA (layered, 0, { { "mix", 30 } }), inB (chorus, 2, { { "mix", 30 } }),
                after (reverb, 0, { { "mix", 20 } }) },
              split (50, 60) },
            { "Ultra-Tap", "Slapback Cluster Twang",
              { inA (ultraTap, 4), inB (delay, 2), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 90) },
            { "Ultra-Tap", "Ping-Pong Cascade Harmony",
              { { harmonizer, -1, diatonic (E, naturalMinor, 2, 0, 35, 0) },
                inA (ultraTap, 5), inB (delay, 9, { { "mix", 25 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (50, 70) },
            { "Ultra-Tap", "Ambient Smear Pan",
              { inA (ultraTap, 6), inB (reverse, 5, { { "mix", 30 } }), after (reverb, 6, { { "mix", 25 } }) },
              split (50, 80) + lfo (1, 0.1f, triangle),
              { { lfo1, balance, 25 } } },
            { "Ultra-Tap", "Tap Burst Plate",
              { inA (ultraTap, -1, { { "taps", 16 }, { "length", 250 }, { "taper", -80 }, { "slope", 0 }, { "mix", 45 } }),
                inB (bandDelay, 6, { { "mix", 25 } }),
                after (reverb, 4, { { "mix", 20 } }) },
              split (50, 70) },

            //==================================================================
            // Band Delays
            { "Band Delays", "Rising Bands Split",
              { inA (bandDelay, 0), inB (delay, 6, { { "mix", 25 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 90) },
            { "Band Delays", "Falling Colors Chorus",
              { { chorus, 0, { { "mix", 30 } } }, inA (bandDelay, 1), inB (ultraTap, 2, { { "mix", 25 } }),
                after (reverb, 0, { { "mix", 15 } }) },
              split (50, 80) },
            { "Band Delays", "Wah Echoes Funk",
              { { filter, 1, { { "mix", 70 } } }, inA (bandDelay, 2), inB (delay, 2, { { "mix", 25 } }),
                after (reverb, 1, { { "mix", 12 } }) },
              split (50, 70) },
            { "Band Delays", "Telephone Taps Lo-Fi",
              { inA (bandDelay, 3), after (delay, 8, { { "mix", 20 } }), after (reverb, 7, { { "mix", 15 } }) },
              split (45, 100) },
            { "Band Delays", "Resonant Ping-Pong Harmony",
              { { harmonizer, 0, { { "mix", 30 } } }, inA (bandDelay, 4), inB (chorus, 3, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 18 } }) },
              split (50, 60) },
            { "Band Delays", "Dark Dub Bands Cross",
              { inA (bandDelay, 5), inB (delay, 4), after (reverb, 7, { { "mix", 18 } }) },
              split (50, 80, 30) },
            { "Band Delays", "Sparkle Taps Shimmer",
              { inA (bandDelay, 6), inB (layered, 0, { { "mix", 30 } }), after (reverb, 4, { { "mix", 20 } }) },
              split (50, 70) },
            { "Band Delays", "Alternating Bands Sweep",
              { { chorus, 0, { { "mix", 30 } } },
                inA (bandDelay, -1, { { "bands", 6 }, { "pattern", 2 }, { "feedback", 35 }, { "time", 500 }, { "mix", 35 } }),
                inB (filter, 3, { { "mix", 60 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 70) + lfoSync (1, bars2, triangle),
              { { lfo1, "banddelay/low", 25 } } },

            //==================================================================
            // Delays
            { "Delays", "Warm Analog Twins",
              { inA (delay, 1), inB (ultraTap, 4, { { "mix", 25 } }), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 90) },
            { "Delays", "Rockabilly Slap Room",
              { { delay, 2 }, inB (chorus, 3, { { "mix", 30 } }), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 80) },
            { "Delays", "Laser Echo Bands",
              { inA (delay, 3), inB (bandDelay, 0, { { "mix", 25 } }), after (reverb, 2, { { "mix", 12 } }) },
              split (50, 80) },
            { "Delays", "Tape Space Flutter",
              { { delay, 4 }, inA (chorus, 4, { { "mix", 20 } }), inB (reverse, 0, { { "mix", 25 } }),
                after (reverb, 1, { { "mix", 18 } }) },
              split (50, 70) + std::vector<P> { { "randomrate", 6 } },
              { { random, "delay/time", 1.5f } } },
            { "Delays", "Long Lead Ping Hall",
              { { delay, 5 }, inA (harmonizer, 0, { { "mix", 35 } }), after (reverb, 0, { { "mix", 18 } }) },
              split (50, 60) },
            { "Delays", "Smooth Chorus Echoes",
              { inA (chorus, 3), inB (delay, 6), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 100) },
            { "Delays", "Runaway Feedback Filter",
              { { delay, 7 }, inA (filter, 2), inB (bandDelay, 5, { { "mix", 20 } }),
                after (reverb, 7, { { "mix", 15 } }) },
              split (50, 60, 10) },
            { "Delays", "Underwater Reverse",
              { { chorus, 4, { { "mix", 25 } } }, inA (delay, 8), inB (reverse, 3, { { "mix", 30 } }),
                after (reverb, 7, { { "mix", 20 } }) },
              split (50, 70) },
            { "Delays", "Dotted Eighth Anthem",
              { inA (delay, 9), inB (ultraTap, 0, { { "mix", 25 } }), after (reverb, 3, { { "mix", 18 } }) },
              split (50, 80) },
            { "Delays", "Quarter Ping-Pong Harmony",
              { { harmonizer, -1, diatonic (A, naturalMinor, 2, 0, 30, 0) },
                inA (delay, 10), inB (chorus, 0, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 12 } }) },
              split (50, 60) },

            //==================================================================
            // Reverbs : la réverbe dans sa branche, le son sec à côté
            { "Reverbs", "Concert Hall Echo Branch",
              { inA (delay, 6, { { "mix", 100 } }), inA (reverb, 0, { { "mix", 100 } }) },
              split (70, 30) },
            { "Reverbs", "Small Room Doubler",
              { { reverb, 1 }, inA (harmonizer, -1, { { "acents", 6 }, { "bcents", -6 }, { "mix", 30 } }) },
              split (50, 80) },
            { "Reverbs", "Studio Room Stereo",
              { inA (reverb, 2), inB (chorus, 3, { { "mix", 25 } }), after (delay, 6, { { "mix", 12 } }) },
              split (50, 100) },
            { "Reverbs", "Large Hall Bloom",
              { inA (reverb, 3, { { "mix", 100 } }) },
              split (65, 40) + env (20, 900),
              { { envelope, balance, 25 } } },
            { "Reverbs", "Bright Plate Shimmer",
              { inA (layered, -1, { { "asemi", 12 }, { "blevel", 0 }, { "feedback", 40 }, { "mix", 100 } }),
                inA (reverb, 4, { { "mix", 100 } }) },
              split (70, 30) },
            { "Reverbs", "Vocal Plate Slap",
              { inA (ultraTap, 4, { { "mix", 25 } }), after (reverb, 5) },
              split (50, 60) },
            { "Reverbs", "Ambient Wash Reverse",
              { inA (reverse, 5, { { "mix", 100 } }), after (reverb, 6) },
              split (60, 70) },
            { "Reverbs", "Dark Chamber Dub",
              { inA (delay, 4, { { "mix", 100 } }), inA (filter, -1, fixedFilter (0, 900, 1000)), after (reverb, 7) },
              split (65, 60) },
            { "Reverbs", "Shimmer Hall",
              { inA (layered, -1, { { "asemi", 12 }, { "adelay", 0 }, { "blevel", 0 }, { "feedback", 50 }, { "tone", 7000 }, { "mix", 100 } }),
                after (reverb, 3, { { "decay", 6 }, { "mix", 40 } }) },
              split (70, 30) },
            { "Reverbs", "Endless Pad",
              { { chorus, 2, { { "mix", 35 } } },
                inA (reverb, -1, { { "type", hall }, { "decay", 16 }, { "modulation", 60 }, { "damping", 5000 }, { "mix", 100 } }),
                inB (reverse, 5, { { "mix", 40 } }) },
              split (50, 70) },

            //==================================================================
            // Filtres balayés
            { "Swept Filters", "Auto-Wah Split",
              { inA (filter, 0), after (delay, 2, { { "mix", 20 } }), after (reverb, 1, { { "mix", 12 } }) },
              split (45, 100) },
            { "Swept Filters", "Funk Envelope Doubled",
              { inA (filter, 1), inB (harmonizer, -1, { { "acents", 8 }, { "bcents", -8 }, { "mix", 40 } }),
                after (reverb, 1, { { "mix", 10 } }) },
              split (50, 90) },
            { "Swept Filters", "Slow Sweep Echoes",
              { { delay, 6, { { "mix", 30 } } }, inA (filter, 2), inB (chorus, 2, { { "mix", 35 } }),
                after (reverb, 0, { { "mix", 15 } }) },
              split (50, 80) },
            { "Swept Filters", "Stereo Wah Swirl Taps",
              { { filter, 3 }, inA (ultraTap, 0, { { "mix", 25 } }), inB (bandDelay, 2, { { "mix", 25 } }),
                after (reverb, 2, { { "mix", 12 } }) },
              split (50, 70) },
            { "Swept Filters", "Choppy Square Delays",
              { { filter, 4 }, inA (delay, 9, { { "mix", 30 } }), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 60) },
            { "Swept Filters", "Closing Envelope Reverse",
              { inA (filter, 5), inB (reverse, 0, { { "mix", 40 } }), after (reverb, 3, { { "mix", 15 } }) },
              split (50, 90) },
            { "Swept Filters", "High-Pass Riser Wash",
              { { filter, 6 }, inA (reverse, 5, { { "mix", 30 } }), inB (delay, 5, { { "mix", 25 } }),
                after (reverb, 6, { { "mix", 25 } }) },
              split (50, 70) },
            { "Swept Filters", "Filtered Echoes",
              { { delay, -1, { { "time", 420 }, { "feedback", 50 }, { "mix", 40 } } },
                inA (filter, -1, { { "source", 0 }, { "type", 1 }, { "rate", 0.3f }, { "low", 300 }, { "high", 3000 }, { "resonance", 55 }, { "mix", 100 } }),
                after (reverb, 2, { { "mix", 12 } }) },
              split (50, 100) },

            //==================================================================
            // Modulation : la matrice fait bouger les branches et les réglages, au tempo ou au jeu
            { "Modulated", "Pulsing Branches",
              { inA (delay, 10, { { "mix", 40 } }), inB (ultraTap, 0, { { "mix", 30 } }), after (reverb, 2, { { "mix", 12 } }) },
              split (50, 60) + lfoSync (1, l8, square) + lfoSync (2, l4, triangle),
              { { lfo1, balance, 40 }, { lfo2, "delay/feedback", 15 } } },
            { "Modulated", "Pitch Wobble (Sync)",
              { inA (harmonizer, -1, { { "acents", 0 }, { "bcents", 0 }, { "blevel", 0 }, { "mix", 100 } }),
                after (chorus, 3, { { "mix", 20 } }), after (reverb, 2, { { "mix", 12 } }) },
              split (55, 80) + lfoSync (1, l4, triangle),
              { { lfo1, "harmonizer/acents", 20 } } },
            { "Modulated", "Envelope Shimmer",
              { inA (layered, 0, { { "mix", 100 } }), after (reverb, 3, { { "mix", 25 } }) },
              split (45, 40) + env (30, 600),
              { { envelope, balance, 30 } } },
            { "Modulated", "Random Diatonic Steps (E Minor)",
              { inA (harmonizer, -1, diatonic (E, naturalMinor, 2, 0, 100, 0)), inA (delay, 6, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (50, 80) + std::vector<P> { { "randomrate", 4 } },
              { { random, "harmonizer/adegrees", 15 } } },
            { "Modulated", "Climbing Arpeggio (A Minor)",
              { inA (harmonizer, -1, diatonic (A, naturalMinor, 2, 0, 100, 0)), inA (ultraTap, 0, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 18 } }) },
              split (50, 70) + lfoSync (1, bar1, sawUp),
              { { lfo1, "harmonizer/adegrees", 25 } } },
            { "Modulated", "Ducking Echoes",
              { inA (delay, 5, { { "mix", 100 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (45, 70) + env (10, 400),
              { { envelope, balance, 30 } } },
            { "Modulated", "Breathing Reverb",
              { inA (reverb, 3, { { "mix", 100 } }), inB (chorus, 3, { { "mix", 20 } }) },
              split (45, 50) + env (15, 700),
              { { envelope, balance, 30 } } },
            { "Modulated", "Sync Filter Pulse",
              { inA (filter, -1, fixedFilter (0, 400, 600, 60)), inB (delay, 9, { { "mix", 30 } }),
                after (reverb, 1, { { "mix", 12 } }) },
              split (50, 70) + lfoSync (1, l8, sawDown) + lfoSync (2, bar1, sine),
              { { lfo1, "filter/low", 40 }, { lfo2, balance, 25 } } },
            { "Modulated", "Pitch-Tracked Delays",
              { inA (delay, -1, { { "time", 450 }, { "feedback", 35 }, { "mix", 40 } }), inB (bandDelay, 0, { { "mix", 25 } }),
                after (reverb, 2, { { "mix", 12 } }) },
              split (50, 80),
              { { pitch, "delay/time", -15 }, { pitch, "banddelay/low", 20 } } },
            { "Modulated", "Tape Flutter",
              { { delay, 1 }, inA (chorus, 4, { { "mix", 20 } }), after (reverb, 7, { { "mix", 15 } }) },
              split (50, 60) + std::vector<P> { { "randomrate", 6 } } + lfo (1, 0.4f, sine),
              { { random, "delay/time", 1.5f }, { lfo1, "delay/highcut", -10 } } },
            { "Modulated", "Band Sweep (Sync)",
              { inA (bandDelay, 0), inB (reverse, 0, { { "mix", 30 } }), after (reverb, 3, { { "mix", 15 } }) },
              split (50, 40) + lfoSync (1, bars2, triangle) + lfoSync (2, bar1, sine),
              { { lfo1, "banddelay/low", 30 }, { lfo2, spread, 30 } } },

            //==================================================================
            // Wet/Dry/Wet : le rig de studio de Los Angeles (sec au centre, effets 100 % sur les côtés)
            { "Wet-Dry-Wet Rigs", "LA Micro Shift & Delay",
              { inB (harmonizer, 0), inB (delay, 6) },
              wetDryWet() + lfo (1, 0.05f, sine),
              { { lfo1, width, 20 } } },
            { "Wet-Dry-Wet Rigs", "LA Stereo Ping & Plate",
              { inB (delay, 10), inB (chorus, 3), inB (reverb, 4) }, wetDryWet (35) },
            { "Wet-Dry-Wet Rigs", "LA Chorus & Hall",
              { inB (chorus, 0), inB (reverb, 0), after (delay, 2, { { "mix", 15 } }) }, wetDryWet (45) },
            { "Wet-Dry-Wet Rigs", "LA Harmony Lead (E Minor)",
              { inB (harmonizer, -1, diatonic (E, naturalMinor, 2, 4, 100)), inB (delay, 9), inB (reverb, 3) }, wetDryWet (35) },
            { "Wet-Dry-Wet Rigs", "LA Reverse Ambience",
              { inB (reverse, 5), inB (reverb, 6) }, wetDryWet (40) + env (20, 500),
              { { envelope, balance, -15 } } },
            { "Wet-Dry-Wet Rigs", "LA Ultra-Tap Space",
              { inB (ultraTap, 6), inB (reverb, 3) }, wetDryWet (32) },
            { "Wet-Dry-Wet Rigs", "LA Band Delays",
              { inB (bandDelay, 4), inB (chorus, 2) }, wetDryWet (45) },
            { "Wet-Dry-Wet Rigs", "LA Shimmer",
              { inB (layered, 0), inB (reverb, 6) }, wetDryWet (40) },
            { "Wet-Dry-Wet Rigs", "LA Octave Clean",
              { { filter, -1, fixedFilter (2, 120, 140, 0) + std::vector<P> { { "mix", 100 } } },
                inB (harmonizer, 2), inB (reverb, 5) }, wetDryWet (40) },
            { "Wet-Dry-Wet Rigs", "LA Dotted Eighth",
              { inB (delay, 9), inB (chorus, 3), inB (reverb, 2) }, wetDryWet (35) + env (10, 400),
              { { envelope, balance, -15 } } },
            { "Wet-Dry-Wet Rigs", "LA Cross-Fed Lead",
              { inB (delay, 5), inB (reverb, 3) }, wetDryWet (45, 140, 20) + lfoSync (1, bar1, triangle),
              { { lfo1, cross, 10 } } },
        };
        return list;
    }

    // La banque « Multi-FX » : des rigs complets, classiques ou originaux, rangés par style
    const std::vector<Recipe>& multiFx()
    {
        static const std::vector<Recipe> list {
            //==================================================================
            { "Clean & Pop", "Sparkling Clean",
              { { compressor, 7 }, { chorus, 3, { { "mix", 25 } } }, inA (delay, 6, { { "mix", 25 } }), after (reverb, 4, { { "mix", 18 } }) },
              split (50, 60) },
            { "Clean & Pop", "Chorus Arpeggios",
              { { compressor, 0 }, { chorus, 0 }, inA (delay, 9, { { "mix", 25 } }), inB (ultraTap, 0, { { "mix", 15 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 70) },
            { "Clean & Pop", "Glassy Doubler",
              { inA (harmonizer, -1, { { "acents", 6 }, { "bcents", -6 }, { "adelay", 12 }, { "bdelay", 22 }, { "mix", 100 } }),
                inB (chorus, 3, { { "mix", 30 } }), after (reverb, 5, { { "mix", 15 } }) },
              split (50, 100) },
            { "Clean & Pop", "Pop Ballad Clean",
              { { chorus, 2, { { "mix", 25 } } }, inA (delay, 5, { { "mix", 25 } }), after (reverb, 3, { { "mix", 25 } }) },
              split (50, 50) },
            { "Clean & Pop", "Funky Pop Wah",
              { { compressor, 2 }, inA (filter, 0), inB (chorus, 3, { { "mix", 25 } }),
                after (delay, 2, { { "mix", 15 } }), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 80) },
            { "Clean & Pop", "Twelve-String Shimmer",
              { { compressor, 0 }, inA (harmonizer, -1, { { "asemi", 12 }, { "acents", 4 }, { "adelay", 6 }, { "alevel", 60 }, { "blevel", 0 }, { "mix", 100 } }),
                inB (chorus, 3, { { "mix", 20 } }), after (reverb, 4, { { "mix", 15 } }) },
              split (55, 60) },
            { "Clean & Pop", "Bright Plate Arps",
              { inA (delay, 10, { { "mix", 30 } }), inB (ultraTap, 5, { { "mix", 20 } }), after (reverb, 4, { { "mix", 20 } }) },
              split (50, 80) },
            { "Clean & Pop", "Sunday Morning",
              { { chorus, 1, { { "mix", 30 } } }, { tremolo, 1, { { "depth", 45 } } }, inA (layered, 1, { { "mix", 25 } }), after (reverb, 0, { { "mix", 20 } }) },
              split (50, 60) },
            { "Clean & Pop", "City Pop Ensemble",
              { { compressor, 3 }, { chorus, 1 }, inA (delay, 6, { { "mix", 20 } }), inB (harmonizer, 0, { { "mix", 30 } }),
                after (reverb, 5, { { "mix", 15 } }) },
              split (50, 70) },
            { "Clean & Pop", "Clean Studio W-D-W",
              { { compressor, 6 }, inB (chorus, 3), inB (delay, 6), inB (reverb, 2) }, wetDryWet (25) },

            //==================================================================
            { "Rock & Lead", "Arena Lead",
              { { compressor, 5 }, { harmonizer, 0, { { "mix", 30 } } }, inA (delay, 5), after (reverb, 3, { { "mix", 20 } }) },
              split (50, 70) },
            { "Rock & Lead", "Classic Rock Solo",
              { { compressor, 5 }, { phaser, 1, { { "mix", 40 } } }, inA (delay, 1, { { "mix", 30 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 40) },
            { "Rock & Lead", "Twin Harmony Lead (E Minor)",
              { inA (harmonizer, -1, diatonic (E, naturalMinor, 2, 0, 100, 0)),
                after (delay, 6, { { "mix", 20 } }), after (reverb, 0, { { "mix", 15 } }) },
              split (50, 100) },
            { "Rock & Lead", "Thirds & Sixths Lead (A Minor)",
              { inA (harmonizer, -1, diatonic (A, naturalMinor, 2, -5, 100)), inB (delay, 2),
                after (reverb, 3, { { "mix", 15 } }) },
              split (50, 90) },
            { "Rock & Lead", "Shred Lead Delay",
              { { compressor, 5 }, inA (delay, 5, { { "mix", 30 } }), inB (ultraTap, 2, { { "mix", 20 } }), after (reverb, 4, { { "mix", 15 } }) },
              split (50, 70) },
            { "Rock & Lead", "Power Ballad Lead",
              { { compressor, 3 }, { harmonizer, 0, { { "mix", 25 } } }, inA (delay, 9, { { "mix", 30 } }), inB (chorus, 2, { { "mix", 25 } }),
                after (reverb, 3, { { "mix", 25 } }) },
              split (50, 70) },
            { "Rock & Lead", "Octave Down Lead",
              { { harmonizer, -1, { { "asemi", -12 }, { "acents", 0 }, { "adelay", 0 }, { "blevel", 0 }, { "mix", 35 } } },
                inA (delay, 6, { { "mix", 25 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 60) },
            { "Rock & Lead", "Wah Solo",
              { { filter, 0, { { "mix", 70 } } }, inA (delay, 5, { { "mix", 25 } }), after (reverb, 0, { { "mix", 15 } }) },
              split (50, 60) },
            { "Rock & Lead", "Stadium Rhythm",
              { inA (harmonizer, 0, { { "mix", 100 } }), after (reverb, 2, { { "mix", 12 } }) },
              split (50, 100) },
            { "Rock & Lead", "Lead W-D-W Rig",
              { inB (harmonizer, 0), inB (delay, 5), inB (reverb, 3) }, wetDryWet (40) },

            //==================================================================
            { "Blues & Country", "Juke Joint Slap",
              { { delay, 2 }, inB (chorus, 4, { { "mix", 15 } }), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 40) },
            { "Blues & Country", "Tweed Room",
              { inA (delay, 1, { { "mix", 20 } }), after (reverb, 1, { { "mix", 20 } }) },
              split (50, 30) },
            { "Blues & Country", "Chicken Pickin' Slap",
              { { compressor, 1 }, inA (delay, 2), inB (ultraTap, 4, { { "mix", 20 } }), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 60) },
            { "Blues & Country", "Pedal Steel Swell",
              { { compressor, 3 }, inA (layered, 3, { { "mix", 30 } }), after (delay, 6, { { "mix", 20 } }), after (reverb, 5, { { "mix", 20 } }) },
              split (50, 60) },
            { "Blues & Country", "Nashville Octave",
              { { compressor, 1 }, inA (harmonizer, -1, { { "asemi", 12 }, { "acents", 0 }, { "alevel", 50 }, { "blevel", 0 }, { "mix", 100 } }),
                inB (chorus, 3, { { "mix", 25 } }), after (reverb, 4, { { "mix", 12 } }) },
              split (55, 70) },
            { "Blues & Country", "Slow Blues Hall",
              { { chorus, 3, { { "mix", 15 } } }, inA (delay, 1, { { "mix", 25 } }), after (reverb, 0, { { "mix", 25 } }) },
              split (50, 50) },
            { "Blues & Country", "Western Twang",
              { { compressor, 1 }, { tremolo, 0 }, inA (delay, 2), inB (reverb, -1, { { "type", plate }, { "decay", 2.5f }, { "mix", 50 } }) },
              split (50, 70) },
            { "Blues & Country", "Blues Harmony (A Pentatonic)",
              { { harmonizer, -1, diatonic (A, minorPenta, 1, 0, 35, 0) }, inA (delay, 1), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 50) },
            { "Blues & Country", "Honky Tonk Vibrato",
              { { chorus, 4, { { "mix", 50 } } }, inA (delay, 2), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 50) },
            { "Blues & Country", "Southern Twin (G Mixolydian)",
              { inA (harmonizer, -1, diatonic (G, mixolydian, 2, 0, 100, 0)), inB (delay, 1, { { "mix", 25 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 100) },

            //==================================================================
            { "Funk & Soul", "Funk Envelope Wide",
              { { compressor, 2 }, inA (filter, 1), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 80) },
            { "Funk & Soul", "Disco Wah Chorus",
              { { compressor, 2 }, { filter, 0, { { "mix", 65 } } }, inA (chorus, 0), inB (delay, 2, { { "mix", 20 } }), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 70) },
            { "Funk & Soul", "Soul Tremble",
              { { chorus, 4, { { "mix", 30 } } }, { tremolo, 2, { { "rate", 5.0f }, { "depth", 50 } } },
                inA (delay, 6, { { "mix", 20 } }), after (reverb, 5, { { "mix", 15 } }) },
              split (50, 80) },
            { "Funk & Soul", "Talking Filter",
              { { compressor, 2 }, inA (filter, -1, { { "source", 1 }, { "type", 1 }, { "low", 300 }, { "high", 2500 }, { "resonance", 80 }, { "sensitivity", 6 }, { "mix", 100 } }),
                inB (harmonizer, -1, { { "asemi", -12 }, { "acents", 0 }, { "adelay", 0 }, { "blevel", 0 }, { "mix", 35 } }),
                after (reverb, 1, { { "mix", 10 } }) },
              split (50, 60) },
            { "Funk & Soul", "Sync Funk Chop",
              { inA (filter, 4), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 60) + lfoSync (1, l16, square),
              { { lfo1, balance, 30 } } },
            { "Funk & Soul", "Neo-Soul Chords",
              { { phaser, 0, { { "mix", 35 } } }, { chorus, 2, { { "mix", 30 } } }, inA (delay, 9, { { "mix", 20 } }), inB (layered, 1, { { "mix", 25 } }),
                after (reverb, 5, { { "mix", 18 } }) },
              split (50, 70) },
            { "Funk & Soul", "Motown Doubler",
              { { compressor, 4 }, inA (harmonizer, -1, { { "acents", 8 }, { "bcents", -8 }, { "adelay", 20 }, { "bdelay", 35 }, { "mix", 100 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 90) },
            { "Funk & Soul", "Octave Funk",
              { { harmonizer, -1, { { "asemi", -12 }, { "acents", 0 }, { "adelay", 0 }, { "blevel", 0 }, { "mix", 40 } } },
                inA (filter, 1), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 60) },
            { "Funk & Soul", "Envelope Echoes",
              { inA (filter, 1), inA (delay, 10, { { "mix", 30 } }), after (reverb, 2, { { "mix", 10 } }) },
              split (50, 70) },
            { "Funk & Soul", "Soul W-D-W",
              { { compressor, 6 }, inB (chorus, 0), inB (ultraTap, 4), inB (reverb, 5) }, wetDryWet (25) },

            //==================================================================
            { "Ambient & Post-Rock", "Post-Rock Crescendo",
              { { compressor, 6 }, { chorus, 2, { { "mix", 25 } } }, inA (delay, 9, { { "mix", 40 } }), inB (reverse, 5, { { "mix", 30 } }),
                after (reverb, 3, { { "decay", 6 }, { "mix", 35 } }) },
              split (50, 70) },
            { "Ambient & Post-Rock", "Glacier",
              { inA (reverse, 5, { { "mix", 100 } }), inB (layered, 1, { { "mix", 30 } }), after (reverb, 6, { { "mix", 35 } }) },
              split (55, 70) },
            { "Ambient & Post-Rock", "Cathedral Echoes",
              { inA (ultraTap, 6), inB (delay, 5, { { "mix", 35 } }), after (reverb, 0, { { "decay", 8 }, { "mix", 35 } }) },
              split (50, 70, 15) },
            { "Ambient & Post-Rock", "Dawn Swell",
              { inA (reverse, 4), after (layered, 0, { { "mix", 25 } }), after (reverb, 6, { { "mix", 30 } }) },
              split (55, 60) },
            { "Ambient & Post-Rock", "Infinite Shimmer",
              { inA (layered, -1, { { "asemi", 12 }, { "blevel", 0 }, { "feedback", 60 }, { "mix", 100 } }),
                inA (reverb, 3, { { "decay", 12 }, { "mix", 100 } }) },
              split (65, 40) },
            { "Ambient & Post-Rock", "Slow Motion",
              { { layered, 1, { { "mix", 20 } } }, inA (delay, 4, { { "mix", 40 } }), inB (reverse, 6, { { "mix", 30 } }),
                after (reverb, 6, { { "mix", 30 } }) },
              split (50, 70) },
            { "Ambient & Post-Rock", "Northern Lights",
              { inA (harmonizer, -1, diatonic (D, major, 4, 7, 100)), inA (reverb, 6, { { "mix", 80 } }),
                inB (delay, 6, { { "mix", 25 } }) },
              split (55, 70) + lfo (1, 0.07f, sine),
              { { lfo1, balance, 20 } } },
            { "Ambient & Post-Rock", "Tidal",
              { inA (delay, 6, { { "mix", 100 } }), inB (reverb, 6, { { "mix", 50 } }), after (chorus, 2, { { "mix", 20 } }), after (tremolo, 8) },
              split (50, 70) + lfo (1, 0.05f, sine),
              { { lfo1, balance, 30 } } },
            { "Ambient & Post-Rock", "Echo Field",
              { inA (ultraTap, -1, { { "taps", 24 }, { "length", 1800 }, { "taper", -30 }, { "slope", -40 }, { "spread", 100 }, { "mix", 50 } }),
                inB (bandDelay, 1, { { "mix", 25 } }), after (reverb, 6, { { "mix", 30 } }) },
              split (50, 70) },
            { "Ambient & Post-Rock", "Ambient W-D-W",
              { inB (reverse, 5), inB (layered, 0), inB (reverb, 6) }, wetDryWet (45) },

            //==================================================================
            { "Shoegaze & Dream", "Wall of Haze",
              { { layered, 1 }, inA (reverse, 6),
                inB (reverb, -1, { { "type", hall }, { "decay", 8 }, { "modulation", 70 }, { "mix", 100 } }),
                after (chorus, 5, { { "mix", 20 } }) },
              split (50, 80) },
            { "Shoegaze & Dream", "Glide Guitar",
              { { layered, -1, { { "detune", 25 }, { "sweeprate", 0.3f }, { "sweepdepth", 25 }, { "mix", 40 } } },
                inA (reverb, 6, { { "mix", 70 } }), inB (delay, 8) },
              split (50, 70) + lfo (1, 0.2f, triangle),
              { { lfo1, "layered/sweepdepth", 20 } } },
            { "Shoegaze & Dream", "Dream Pop Chorus",
              { { chorus, 1 }, inA (delay, 6, { { "mix", 30 } }), inB (reverse, 6, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 25 } }) },
              split (50, 70) },
            { "Shoegaze & Dream", "Bent Tape Reverb",
              { { chorus, 4, { { "mix", 30 } } }, inA (reverb, 6, { { "mix", 100 } }) },
              split (55, 50) + std::vector<P> { { "randomrate", 1 } },
              { { random, "chorus/depth", 20 } } },
            { "Shoegaze & Dream", "Pink Haze",
              { { filter, -1, fixedFilter (0, 1800, 1900) }, inA (layered, 4), inB (reverb, 7, { { "mix", 60 } }),
                after (delay, 4, { { "mix", 20 } }) },
              split (50, 70) },
            { "Shoegaze & Dream", "Velvet Swirl",
              { { phaser, 3, { { "mix", 40 } } }, { chorus, 5, { { "mix", 40 } } }, inA (delay, 6), inB (reverb, 6, { { "mix", 50 } }) },
              split (50, 50) + lfo (1, 0.2f, sine),
              { { lfo1, spread, 25 } } },
            { "Shoegaze & Dream", "Lo-Fi Dream",
              { { bandDelay, 3, { { "mix", 25 } } }, inA (chorus, 4, { { "mix", 40 } }), inB (reverb, 7, { { "mix", 50 } }),
                after (delay, 1, { { "mix", 20 } }) },
              split (50, 70) },
            { "Shoegaze & Dream", "Reverse Dream (E Major)",
              { inA (harmonizer, -1, diatonic (E, major, 2, 4, 100)), inA (reverse, 1), after (reverb, 6, { { "mix", 30 } }) },
              split (55, 70) },
            { "Shoegaze & Dream", "Sea of Strings",
              { { layered, 2, { { "mix", 25 } } }, inA (chorus, 1), inB (reverb, 3, { { "mix", 50 } }),
                after (delay, 6, { { "mix", 20 } }) },
              split (50, 70) },
            { "Shoegaze & Dream", "Dream W-D-W",
              { inB (layered, 1), inB (reverse, 6), inB (reverb, 6) }, wetDryWet (30) },

            //==================================================================
            { "Psychedelic", "Backwards Trip",
              { inA (reverse, 1), inB (chorus, 5, { { "mix", 40 } }), after (reverb, 3, { { "mix", 20 } }) },
              split (50, 90) },
            { "Psychedelic", "Kaleidoscope",
              { { harmonizer, 1, { { "mix", 30 } } }, inA (phaser, 6), inA (chorus, 5), inB (reverse, 2, { { "mix", 30 } }),
                after (delay, 6, { { "mix", 20 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 80) + lfo (1, 0.2f, sine),
              { { lfo1, balance, 25 } } },
            { "Psychedelic", "Seasick Sitar",
              { { layered, 4 }, inA (bandDelay, 2), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 60) },
            { "Psychedelic", "Flanged Echo Tunnel",
              { { chorus, 5 }, inA (delay, 4), after (reverb, 7, { { "mix", 20 } }) },
              split (50, 60, 15) },
            { "Psychedelic", "Melting Clocks",
              { inA (delay, 8), inB (reverse, 3, { { "mix", 40 } }), after (reverb, 6, { { "mix", 20 } }) },
              split (50, 70) + std::vector<P> { { "randomrate", 1 } },
              { { random, "delay/time", 3 } } },
            { "Psychedelic", "Paisley Wah",
              { { filter, 3 }, inA (chorus, 4, { { "mix", 50 } }), inB (delay, 6, { { "mix", 30 } }),
                after (reverb, 2, { { "mix", 15 } }) },
              split (50, 70) },
            { "Psychedelic", "Rotary Swirl",
              { inA (chorus, 4, { { "mix", 100 } }), after (tremolo, 10, { { "depth", 45 } }), after (reverb, 1, { { "mix", 12 } }) },
              split (50, 100) + lfo (1, 5.5f, sine),
              { { lfo1, balance, 35 } } },
            { "Psychedelic", "Phasing Octaves",
              { { harmonizer, 2, { { "mix", 30 } } }, inA (phaser, 5), after (delay, 6, { { "mix", 20 } }) },
              split (50, 70) },
            { "Psychedelic", "Third Eye (E Phrygian)",
              { { harmonizer, -1, diatonic (E, phrygian, 2, -3, 35) }, inA (reverse, 0), inB (delay, 4, { { "mix", 30 } }),
                after (reverb, 3, { { "mix", 25 } }) },
              split (50, 70) },
            { "Psychedelic", "Acid Wash",
              { { filter, 2 }, inA (layered, 3), inB (delay, 8), after (reverb, 6, { { "mix", 25 } }) },
              split (50, 70) },

            //==================================================================
            { "Dub & Reggae", "Skank Echo",
              { { compressor, 2 }, { phaser, 2 }, inA (delay, 10, { { "mix", 35 } }), after (reverb, 4, { { "mix", 15 } }) },
              split (50, 90) },
            { "Dub & Reggae", "Dub Siren Delay",
              { inA (delay, -1, { { "time", 375 }, { "feedback", 70 }, { "highcut", 3000 }, { "mix", 45 } }),
                after (reverb, 7, { { "mix", 15 } }) },
              split (50, 70) + lfo (1, 0.3f, triangle),
              { { lfo1, "delay/time", 8 } } },
            { "Dub & Reggae", "Roots Spring",
              { { delay, 2 }, inB (reverb, -1, { { "type", plate }, { "decay", 2.8f }, { "damping", 4000 }, { "mix", 60 } }) },
              split (50, 60) },
            { "Dub & Reggae", "Dub Throw",
              { inA (delay, 4, { { "mix", 100 } }), after (reverb, 7, { { "mix", 20 } }) },
              split (55, 70) + env (10, 500),
              { { envelope, balance, 30 } } },
            { "Dub & Reggae", "Filter Dub",
              { inA (delay, 4), inA (filter, 2), after (reverb, 7, { { "mix", 15 } }) },
              split (55, 70, 20) },
            { "Dub & Reggae", "Steppers Taps",
              { inA (ultraTap, 0), inB (delay, 10, { { "mix", 30 } }), after (reverb, 7, { { "mix", 15 } }) },
              split (50, 70) },
            { "Dub & Reggae", "Melodica Echo",
              { { chorus, 4, { { "mix", 30 } } }, inA (delay, 5, { { "mix", 35 } }), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 70) },
            { "Dub & Reggae", "Bass Bin Bands",
              { inA (bandDelay, 5), after (reverb, 7, { { "mix", 15 } }) },
              split (50, 80) },
            { "Dub & Reggae", "Echo Chamber Feedback",
              { inA (delay, 4), inB (bandDelay, 5), after (reverb, 7, { { "mix", 15 } }) },
              split (50, 80, 35) },
            { "Dub & Reggae", "Dub W-D-W",
              { inB (delay, 4), inB (bandDelay, 5), inB (reverb, 7) }, wetDryWet (40, 160) },

            //==================================================================
            { "Experimental", "Glass Cathedral",
              { { layered, 0 }, inA (reverse, 5), inB (ultraTap, 6), after (reverb, 3, { { "decay", 9 }, { "mix", 35 } }) },
              split (50, 70, 20) },
            { "Experimental", "Broken Tape Choir",
              { { chorus, 4, { { "mix", 30 } } }, { delay, 1 },
                inA (harmonizer, -1, diatonic (C, major, 2, 4, 60)), inB (reverse, 6) },
              split (50, 70) + std::vector<P> { { "randomrate", 3 } },
              { { random, "delay/time", 2 } } },
            { "Experimental", "Granular Rain",
              { inA (ultraTap, -1, { { "taps", 24 }, { "length", 1200 }, { "taper", -20 }, { "slope", -60 }, { "spread", 100 }, { "mix", 60 } }),
                inB (bandDelay, 6), after (reverb, 6, { { "mix", 25 } }) },
              split (50, 70),
              { { random, "ultratap/length", 10 } } },
            { "Experimental", "Bell Tower",
              { inA (harmonizer, -1, { { "asemi", 12 }, { "acents", 0 }, { "bsemi", 19 }, { "bcents", 0 }, { "blevel", 60 }, { "mix", 100 } }),
                inA (reverb, 4, { { "mix", 70 } }), after (delay, 10, { { "mix", 15 } }) },
              split (65, 50) },
            { "Experimental", "Octave Staircase",
              { inA (harmonizer, 4), inA (delay, 5, { { "mix", 30 } }), inB (bandDelay, 0), after (reverb, 0, { { "mix", 15 } }) },
              split (50, 70, 30) },
            { "Experimental", "Envelope Bloom",
              { inA (layered, 0, { { "mix", 50 } }), inA (reverb, 6, { { "mix", 100 } }) },
              split (40, 50) + env (20, 1500),
              { { envelope, balance, 40 } } },
            { "Experimental", "Stutter Grid",
              { inA (ultraTap, -1, { { "taps", 8 }, { "length", 500 }, { "taper", 0 }, { "slope", 0 }, { "mix", 100 } }),
                after (reverb, 1, { { "mix", 10 } }) },
              split (50, 60) + lfoSync (1, l16, square),
              { { lfo1, balance, 50 } } },
            { "Experimental", "Dark Matter",
              { { filter, -1, fixedFilter (0, 700, 800, 50) }, inA (reverse, 3), inB (delay, 4),
                after (reverb, 7, { { "mix", 35 } }) },
              split (50, 70, 30) },
            { "Experimental", "Sidechain Pump",
              { inA (reverb, 3, { { "mix", 100 } }), inB (delay, 9, { { "mix", 30 } }) },
              split (50, 60) + lfoSync (1, l4, sawUp),
              { { lfo1, balance, 40 } } },
            { "Experimental", "Infinite Reverse Loop",
              { inA (reverse, -1, { { "length", 1500 }, { "feedback", 70 }, { "mix", 100 } }), after (reverb, 6, { { "mix", 30 } }) },
              split (60, 70, 20) },

            //==================================================================
            { "Rhythmic", "Sixteenth Gate",
              { inA (filter, -1, fixedFilter (1, 800, 1000, 40)), after (delay, 9, { { "mix", 20 } }) },
              split (50, 60) + lfoSync (1, l16, square),
              { { lfo1, balance, 45 } } },
            { "Rhythmic", "Arpeggiator (A Minor)",
              { inA (harmonizer, -1, diatonic (A, naturalMinor, 2, 0, 100, 0)), inA (delay, 9, { { "mix", 25 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (50, 70) + lfoSync (1, l8, sawUp),
              { { lfo1, "harmonizer/adegrees", 30 } } },
            { "Rhythmic", "Triplet Taps",
              { inA (ultraTap, 0), inB (delay, 10, { { "mix", 25 } }) },
              split (50, 40) + lfoSync (1, l8t, square),
              { { lfo1, spread, 40 } } },
            { "Rhythmic", "Galloping Filters",
              { { filter, -1, { { "source", 0 }, { "type", 1 }, { "rate", 0.02f }, { "low", 300 }, { "high", 2500 }, { "resonance", 60 }, { "mix", 100 } } },
                inA (delay, 9, { { "mix", 25 } }), inB (ultraTap, 0, { { "mix", 20 } }) },
              split (50, 70) + lfoSync (1, l16, sawDown),
              { { lfo1, "filter/low", 35 } } },
            { "Rhythmic", "Bar-Long Swell",
              { inA (reverse, 4), inB (delay, 10, { { "mix", 25 } }), after (reverb, 3, { { "mix", 20 } }) },
              split (50, 70) + lfoSync (1, bar1, sawUp),
              { { lfo1, balance, 30 } } },
            { "Rhythmic", "Quarter Note Pump",
              { { tremolo, 6, { { "division", 3 }, { "depth", 55 } } }, inA (layered, 1, { { "mix", 100 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 60) + lfoSync (1, l4, sawDown),
              { { lfo1, balance, 40 } } },
            { "Rhythmic", "Dotted Ping Harmony (G Major)",
              { inA (harmonizer, -1, diatonic (G, major, 2, 4, 100)), inB (delay, 9), after (reverb, 4, { { "mix", 15 } }) },
              split (50, 50) + lfoSync (1, bars2, triangle),
              { { lfo1, spread, 30 } } },
            { "Rhythmic", "Pattern Filter",
              { { filter, 4 }, inA (bandDelay, 4), inB (delay, 10, { { "mix", 25 } }), after (reverb, 1, { { "mix", 10 } }) },
              split (50, 70) },
            { "Rhythmic", "Two-Step Harmony (D Dorian)",
              { inA (harmonizer, -1, diatonic (D, dorian, 2, 0, 100, 0)), inA (ultraTap, 0, { { "mix", 30 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (50, 70) + lfoSync (1, l4, square) + lfoSync (2, bar1, triangle),
              { { lfo1, "harmonizer/adegrees", 10 }, { lfo2, "harmonizer/adegrees", 15 } } },
            { "Rhythmic", "Rhythm W-D-W",
              { inB (delay, 10), inB (ultraTap, 0), inB (chorus, 3) },
              wetDryWet (40) + lfoSync (1, l8, square),
              { { lfo1, width, 30 } } },

            //==================================================================
            // Le flanger : du « jet » des années 70 au through-zero des bandes magnétiques
            { "Flanger", "Brown Sound Jet",
              { { compressor, 5 }, { flanger, 0, { { "mix", 45 } } }, inA (delay, 5, { { "mix", 25 } }), inB (ultraTap, 2, { { "mix", 15 } }),
                after (reverb, 3, { { "mix", 15 } }) },
              split (50, 60) },
            { "Flanger", "Tape Flange Psych",
              { { flanger, 1 }, inA (reverse, 0, { { "mix", 40 } }), inB (delay, 4, { { "mix", 30 } }), after (reverb, 6, { { "mix", 25 } }) },
              split (45, 70) + lfo (1, 0.05f, sine),
              { { lfo1, "flanger/manual", 20 } } },
            { "Flanger", "Metal Rhythm Flange",
              { { compressor, 5 }, inA (flanger, 2, { { "mix", 40 }, { "feedback", 60 } }), inB (chorus, 3, { { "mix", 30 } }), after (delay, 2, { { "mix", 12 } }) },
              split (50, 60) },
            { "Flanger", "Hollow Funk",
              { { compressor, 2 }, { filter, 1, { { "mix", 60 } } }, inA (flanger, 3), inB (phaser, 2, { { "mix", 40 } }), after (delay, 2, { { "mix", 15 } }) },
              split (50, 60) + env (5, 150),
              { { envelope, "flanger/manual", 30 } } },
            { "Flanger", "Stereo Flange Wash",
              { inA (flanger, 5), inB (chorus, 1, { { "mix", 40 } }), after (reverb, 6, { { "mix", 25 } }) },
              split (50, 100) },
            { "Flanger", "Synced Jet Echoes",
              { { flanger, 7 }, inA (delay, 9, { { "mix", 30 } }), inB (bandDelay, 0, { { "mix", 25 } }), after (reverb, 1, { { "mix", 15 } }) },
              split (50, 70) },
            { "Flanger", "Through-Zero Swells",
              { { compressor, 6 }, inA (flanger, 8), inB (reverse, 5, { { "mix", 50 } }), after (reverb, 3, { { "mix", 20 } }) },
              split (50, 70) },
            { "Flanger", "Warble Lo-Fi",
              { { flanger, 6 }, inA (delay, 8, { { "mix", 30 } }), inB (tremolo, 2), after (reverb, 7, { { "mix", 20 } }) },
              split (50, 60),
              { { random, "flanger/manual", 15 } } },
            { "Flanger", "Static Comb Lead (A Minor)",
              { { compressor, 5 }, { flanger, 9 }, inA (harmonizer, -1, diatonic (A, naturalMinor, 2, 0, 100, 0)), inB (delay, 5, { { "mix", 25 } }),
                after (reverb, 4, { { "mix", 15 } }) },
              split (45, 70),
              { { pitch, "flanger/manual", -30 } } },
            { "Flanger", "Deep Space Flange Pad",
              { inA (flanger, 10), inA (layered, 1, { { "mix", 40 } }), inB (reverb, 6, { { "mix", 100 } }), after (delay, 6, { { "mix", 20 } }) },
              split (40, 80) + lfoSync (1, bars4, sine),
              { { lfo1, balance, 30 } } },
            { "Flanger", "Flange & Phase Swirl",
              { inA (phaser, 6), inB (flanger, 5), after (delay, 6, { { "mix", 20 } }), after (reverb, 2, { { "mix", 15 } }) },
              split (50, 100, 10) + lfo (1, 0.1f, sine),
              { { lfo1, "flanger/feedback", 20 } } },
            { "Flanger", "Jet Plane Dub",
              { { filter, -1, fixedFilter (1, 500, 2000, 30) }, inA (delay, 4, { { "mix", 40 } }), inB (flanger, 0, { { "mix", 100 } }),
                after (reverb, 7, { { "mix", 20 } }) },
              split (50, 60) + lfoSync (1, bars2, triangle),
              { { lfo1, "flanger/manual", 25 } } },
        };
        return list;
    }

#include "FactoryPatchBay.inc"

    // Les banques, dans l'ordre des dossiers
    struct Entry { const char* bank; const Recipe* recipe; };

    const std::vector<Entry>& entries()
    {
        static const std::vector<Entry> list = []
        {
            std::vector<Entry> all;
            for (auto& r : recipes())
                all.push_back ({ FactoryPresets::folderName, &r });
            for (auto& r : multiFx())
                all.push_back ({ FactoryPresets::multiFolderName, &r });
            for (auto& r : patchBay())
                all.push_back ({ FactoryPresets::patchFolderName, &r });
            return all;
        }();
        return list;
    }

    juce::String setParameter (juce::AudioProcessor& processor, const juce::String& where, const P& p)
    {
        for (auto* param : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                if (ranged->getParameterID() == p.id)
                {
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (p.value));
                    return {};
                }
        return where + ": no parameter \"" + p.id + "\"";
    }
}

namespace FactoryPresets
{
    int count()                       { return (int) entries().size(); }
    juce::String bank (int index)     { return entries()[(size_t) index].bank; }
    juce::String category (int index) { return entries()[(size_t) index].recipe->category; }
    juce::String name (int index)     { return entries()[(size_t) index].recipe->name; }

    juce::String apply (int index, ChainProcessor& chain)
    {
        const auto& recipe = *entries()[(size_t) index].recipe;
        const juce::String where (recipe.name);

        // Les effets de la recette allumés, les autres éteints (l'Amp n'est pas touché : il reste celui de l'utilisateur)
        std::array<bool, numEffects> used {};
        for (auto& fx : recipe.effects)
            used[fx.effect] = true;

        for (int e = 0; e < numEffects; ++e)
            if (e != amp)
                chain.parameters.getParameter (ChainProcessor::onParamId (e))->setValueNotifyingHost (used[(size_t) e] ? 1.0f : 0.0f);

        for (auto& fx : recipe.effects)
        {
            auto& processor = chain.effect (fx.effect);
            if (fx.program >= 0)
            {
                if (fx.program >= processor.getNumPrograms())
                    return where + ": no preset " + juce::String (fx.program) + " in " + keys()[fx.effect];
                processor.setCurrentProgram (fx.program);
            }
            for (auto& p : fx.params)
                if (auto error = setParameter (processor, where, p); error.isNotEmpty())
                    return error;
        }

        // Routage : Amp en tête (précédé du compresseur s'il est utilisé avant les branches, comme une pédale
        // devant l'ampli), puis les effets de la recette dans leurs sections, les effets éteints juste après l'Amp
        std::array<std::vector<int>, numLanes> lanes;
        const bool compressorFirst = std::any_of (recipe.effects.begin(), recipe.effects.end(),
                                                  [] (const Fx& fx) { return fx.effect == compressor && fx.lane == pre; });
        if (compressorFirst)
            lanes[pre].push_back (compressor);
        lanes[pre].push_back (amp);
        for (auto& fx : recipe.effects)
            if (! (compressorFirst && fx.effect == compressor))
                lanes[fx.lane].push_back (fx.effect);
        auto& tail = lanes[pre];
        const auto afterAmp = (compressorFirst ? 2 : 1);
        for (int e = 0; e < numEffects; ++e)
            if (! used[(size_t) e] && e != amp)
                tail.insert (tail.begin() + afterAmp, e);   // éteints, ils ne s'entendent pas

        size_t total = 0;
        for (auto& lane : lanes)
            total += lane.size();
        if (total != numEffects)
            return where + ": an effect is listed twice";
        chain.setRouting (make (lanes[pre], lanes[branchA], lanes[branchB], lanes[post]));

        // Baie de brassage : les câbles de la recette, les pédales rangées colonne par colonne
        if (recipe.cables != nullptr)
        {
            chain.setPatchMode (true);
            if (auto error = applyCables (recipe, used, chain); error.isNotEmpty())
                return where + ": " + error;
        }

        for (auto& p : recipe.chain)
            if (auto error = setParameter (chain, where, p); error.isNotEmpty())
                return error;

        int slot = 0;
        for (auto& m : recipe.mods)
        {
            const int destination = chain.findDestination (m.destination);
            if (destination < 0)
                return where + ": no modulation destination \"" + m.destination + "\"";
            chain.setSlotDestination (slot, destination);
            setParameter (chain, where, { ChainProcessor::slotParamId (slot, "source").toRawUTF8(), (float) m.source });
            setParameter (chain, where, { ChainProcessor::slotParamId (slot, "amount").toRawUTF8(), m.amount });
            ++slot;
        }
        return {};
    }

    juce::String writeAll (const juce::File& presetsFolder)
    {
        // Seulement les dossiers d'usine : les presets de l'utilisateur ne sont pas touchés
        for (auto* folder : { folderName, multiFolderName, patchFolderName })
            presetsFolder.getChildFile (folder).deleteRecursively();

        for (int i = 0; i < count(); ++i)
        {
            ChainProcessor chain;
            if (auto error = apply (i, chain); error.isNotEmpty())
                return error;

            const auto file = presetsFolder.getChildFile (bank (i)).getChildFile (category (i)).getChildFile (name (i) + ChainPresets::extension);
            if (const auto result = file.getParentDirectory().createDirectory(); result.failed())
                return result.getErrorMessage();

            auto state = chain.createState();
            state.removeProperty ("amp", nullptr);   // pas d'Amp : le tien reste chargé
            state.setProperty ("presetname", bank (i) + "/" + category (i) + "/" + name (i), nullptr);
            state.setProperty ("presetversion", 1, nullptr);

            const auto xml = state.createXml();
            if (xml == nullptr || ! xml->writeTo (file))
                return "Can't write " + file.getFullPathName();
        }
        return {};
    }
}
