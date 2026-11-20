#pragma once

#include <array>
#include <cmath>

// Calcul des intervalles « diatoniques », comme le Diatonic Shift du H3000 :
// au lieu d'un intervalle fixe en demi-tons, on demande « une tierce au-dessus dans la gamme ».
// La tierce sera majeure (4 demi-tons) ou mineure (3) selon la note jouée.
//
// En C++ pur, sans JUCE, pour pouvoir le tester seul.
namespace DiatonicScale
{
    struct Scale
    {
        const char* name;
        int numNotes;
        std::array<int, 7> steps;   // demi-tons depuis la tonique (numNotes premières valeurs)
    };

    inline const std::array<Scale, 10>& scales()
    {
        static const std::array<Scale, 10> list {{
            { "Major",            7, { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural Minor",    7, { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic Minor",   7, { 0, 2, 3, 5, 7, 8, 11 } },
            { "Dorian",           7, { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",         7, { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian",           7, { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian",       7, { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian",          7, { 0, 1, 3, 5, 6, 8, 10 } },
            { "Major Pentatonic", 5, { 0, 2, 4, 7, 9 } },
            { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10 } },
        }};
        return list;
    }

    inline const char* noteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midiNote % 12) + 12) % 12];
    }

    // Position d'une note MIDI dans la gamme : (octave × nombre de notes + degré).
    // Une note hors gamme est rattachée à la note de la gamme juste en dessous.
    inline int degreeIndexOf (int midiNote, int key, const Scale& s)
    {
        const int rel    = midiNote - key;
        const int octave = (int) std::floor (rel / 12.0);
        const int pc     = rel - octave * 12;

        int degree = 0;
        for (int d = 0; d < s.numNotes; ++d)
            if (s.steps[(size_t) d] <= pc)
                degree = d;

        return octave * s.numNotes + degree;
    }

    // Note MIDI correspondant à une position dans la gamme
    inline int noteAtDegreeIndex (int index, int key, const Scale& s)
    {
        const int octave = (int) std::floor ((double) index / s.numNotes);
        const int degree = index - octave * s.numNotes;
        return key + octave * 12 + s.steps[(size_t) degree];
    }

    // Décalage en demi-tons pour jouer « degrees » crans de gamme au-dessus (ou en dessous si négatif)
    // de la note jouée. Une note hors gamme garde son écart (ex. une note de passage chromatique).
    inline int semitonesFor (int playedNote, int key, const Scale& s, int degrees)
    {
        const int index  = degreeIndexOf (playedNote, key, s);
        const int inScale = noteAtDegreeIndex (index, key, s);
        const int target = noteAtDegreeIndex (index + degrees, key, s);
        return target - inScale;
    }

    // Nom d'un intervalle en crans de gamme : +2 → "+3rd", -4 → "-5th", 7 → "+Oct" (gamme de 7 notes)
    inline const char* ordinalSuffix (int n)
    {
        if (n % 100 >= 11 && n % 100 <= 13) return "th";
        switch (n % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; default: return "th"; }
    }
}

// Suit la note jouée à partir des mesures du détecteur, sans sautiller :
// une nouvelle note n'est retenue que si elle se confirme sur deux mesures (~10 ms),
// ou tout de suite si l'écart dépasse un demi-ton franc. Sans note détectée, la dernière est gardée.
class NoteTracker
{
public:
    void reset() noexcept { current = -1; candidate = -1; count = 0; }

    // midi : note mesurée (avec décimales), ou une valeur négative si aucune note n'est détectée
    void update (float midi) noexcept
    {
        if (midi < 0.0f)
            return;

        const int nearest = (int) std::lround (midi);

        if (current < 0 || std::abs (midi - (float) current) > 1.0f)
        {
            current = nearest;
            count = 0;
            return;
        }

        if (nearest == current)
        {
            count = 0;
            return;
        }

        if (nearest == candidate && ++count >= 1)
        {
            current = nearest;
            count = 0;
        }
        else if (nearest != candidate)
        {
            candidate = nearest;
            count = 0;
        }
    }

    int getNote() const noexcept { return current; }   // -1 tant qu'aucune note n'a été jouée

private:
    int current = -1, candidate = -1, count = 0;
};
