#pragma once

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Routage de H3O Chain, comme sur le H3000 : l'ordre des effets est libre,
// et le signal peut se séparer en deux branches parallèles A et B.
//
//            ┌─ A : effets… ─┐
//   Avant ───┤               ├─── Après
//            └─ B : effets… ─┘
//
// Chaque effet appartient à une des quatre sections (Avant, A, B, Après) et a une place dans sa section.
// Une branche vide laisse passer le son sans effet (signal « sec »).
// En C++ pur, sans JUCE, pour pouvoir le tester seul.
namespace ChainRouting
{
    constexpr int numEffects = 11;

    // Les identifiants des effets : leur ordre d'origine dans la chaîne.
    // Ne jamais changer l'ordre ni les noms : ils sont enregistrés dans les sessions.
    // (le compresseur, arrivé plus tard, a le dernier identifiant mais se place en tête de chaîne)
    enum Effect : std::uint8_t { filter, amp, harmonizer, reverse, layered, chorus, delay, bandDelay, ultraTap, reverb, compressor };

    inline const std::array<const char*, numEffects>& keys()
    {
        static const std::array<const char*, numEffects> k {
            "filter", "amp", "harmonizer", "reverse", "layered", "chorus", "delay", "banddelay", "ultratap", "reverb", "compressor"
        };
        return k;
    }

    enum Lane : std::uint8_t { pre = 0, branchA, branchB, post, numLanes };

    inline const char* laneKey (int lane)
    {
        static const char* k[] = { "pre", "a", "b", "post" };
        return k[lane & 3];
    }

    struct Routing
    {
        std::array<std::uint8_t, numEffects> order {};   // identifiants des effets, dans l'ordre du signal
        std::array<std::uint8_t, numEffects> lane {};    // section de chaque effet (indexé par identifiant)

        // Tout en série, dans l'ordre d'origine (le compresseur en tête, comme une pédale en début de chaîne)
        static Routing series()
        {
            Routing r;
            r.order[0] = compressor;
            for (int i = 0; i < numEffects; ++i)
            {
                if (i != compressor)
                    r.order[(size_t) (i < compressor ? i + 1 : i)] = (std::uint8_t) i;
                r.lane[(size_t) i] = pre;
            }
            return r;
        }

        bool operator== (const Routing& o) const { return order == o.order && lane == o.lane; }
        bool operator!= (const Routing& o) const { return ! (*this == o); }

        // Les effets d'une section, dans l'ordre du signal
        std::vector<int> effectsIn (int l) const
        {
            std::vector<int> list;
            for (auto e : order)
                if (lane[e] == l)
                    list.push_back (e);
            return list;
        }

        // Le signal passe par deux branches dès qu'un effet est placé dans A ou B
        bool hasParallel() const
        {
            for (auto l : lane)
                if (l == branchA || l == branchB)
                    return true;
            return false;
        }

        int laneOf (int effect) const { return lane[(size_t) effect]; }

        // Place d'un effet dans sa section (0 = le premier)
        int indexInLane (int effect) const
        {
            const auto list = effectsIn (lane[(size_t) effect]);
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i] == effect)
                    return (int) i;
            return 0;
        }

        // Déplace un effet dans une section, à la place « index » parmi les autres effets de cette section
        void move (int effect, int newLane, int index)
        {
            if (effect < 0 || effect >= numEffects || newLane < 0 || newLane >= numLanes)
                return;

            std::array<std::vector<int>, numLanes> lanes;
            for (auto e : order)
                if (e != effect)
                    lanes[lane[e]].push_back (e);

            auto& target = lanes[(size_t) newLane];
            index = index < 0 ? 0 : (index > (int) target.size() ? (int) target.size() : index);
            target.insert (target.begin() + index, effect);
            lane[(size_t) effect] = (std::uint8_t) newLane;

            rebuildOrder (lanes);
        }

        // Range l'ordre section par section (Avant, A, B, Après) en gardant l'ordre dans chaque section
        void normalise()
        {
            std::array<std::vector<int>, numLanes> lanes;
            for (auto e : order)
                lanes[lane[e] & 3].push_back (e);
            rebuildOrder (lanes);
        }

        // Tient dans 56 bits : 11 × 4 bits pour l'ordre, puis le nombre d'effets dans Avant, A et B (4 bits chacun).
        // L'ordre étant rangé section par section, les sections se déduisent de ces nombres.
        // Permet de le passer au fil audio dans un seul std::atomic<uint64_t>.
        std::uint64_t pack() const
        {
            Routing r = *this;
            r.normalise();

            std::uint64_t v = 0;
            int counts[numLanes] = {};
            for (int i = 0; i < numEffects; ++i)
            {
                v |= (std::uint64_t) (r.order[(size_t) i] & 15) << (4 * i);
                ++counts[r.lane[(size_t) i] & 3];
            }
            for (int l = 0; l < 3; ++l)
                v |= (std::uint64_t) (counts[l] & 15) << (4 * numEffects + 4 * l);
            return v;
        }

        static Routing unpack (std::uint64_t v)
        {
            Routing r;
            unsigned seen = 0;
            for (int i = 0; i < numEffects; ++i)
            {
                r.order[(size_t) i] = (std::uint8_t) ((v >> (4 * i)) & 15);
                if (r.order[(size_t) i] < numEffects)
                    seen |= 1u << r.order[(size_t) i];
            }

            int counts[3];
            for (int l = 0; l < 3; ++l)
                counts[l] = (int) ((v >> (4 * numEffects + 4 * l)) & 15);

            if (seen != (1u << numEffects) - 1 || counts[0] + counts[1] + counts[2] > numEffects)
                return series();

            int position = 0;
            for (int l = 0; l < numLanes; ++l)
            {
                const int n = l < 3 ? counts[l] : numEffects - position;
                for (int k = 0; k < n; ++k, ++position)
                    r.lane[r.order[(size_t) position]] = (std::uint8_t) l;
            }
            return r;
        }

        // Pour les sessions : "filter:pre,amp:pre,harmonizer:a,…"
        // Lisible, et robuste : un nom inconnu est ignoré, un effet absent (ajouté plus tard) se met à la fin, en série.
        std::string toString() const
        {
            std::string s;
            for (auto e : order)
            {
                if (! s.empty())
                    s += ',';
                s += keys()[e];
                s += ':';
                s += laneKey (lane[e]);
            }
            return s;
        }

        static Routing fromString (const std::string& text)
        {
            Routing r;
            int count = 0;
            bool placed[numEffects] = {};

            std::stringstream stream (text);
            std::string item;
            while (std::getline (stream, item, ','))
            {
                const auto colon = item.find (':');
                const auto name = item.substr (0, colon);
                const auto laneName = colon == std::string::npos ? std::string ("pre") : item.substr (colon + 1);

                int effect = -1;
                for (int e = 0; e < numEffects; ++e)
                    if (name == keys()[(size_t) e])
                        effect = e;

                if (effect < 0 || placed[effect])
                    continue;

                int l = pre;
                for (int i = 0; i < numLanes; ++i)
                    if (laneName == laneKey (i))
                        l = i;

                placed[effect] = true;
                r.order[(size_t) count++] = (std::uint8_t) effect;
                r.lane[(size_t) effect] = (std::uint8_t) l;
            }

            if (count == 0)
                return series();

            // Effet absent de la session : à la fin du signal (le compresseur, lui, en tête)
            bool anyPost = false;
            for (int i = 0; i < count; ++i)
                anyPost = anyPost || r.lane[r.order[(size_t) i]] == post;

            for (int e = 0; e < numEffects; ++e)
                if (! placed[e])
                {
                    if (e == compressor)
                    {
                        for (int i = count; i > 0; --i)
                            r.order[(size_t) i] = r.order[(size_t) i - 1];
                        r.order[0] = (std::uint8_t) e;
                        r.lane[(size_t) e] = pre;
                        ++count;
                        continue;
                    }
                    r.order[(size_t) count++] = (std::uint8_t) e;
                    r.lane[(size_t) e] = anyPost ? post : pre;
                }

            r.normalise();
            return r;
        }

    private:
        void rebuildOrder (const std::array<std::vector<int>, numLanes>& lanes)
        {
            size_t i = 0;
            for (auto& list : lanes)
                for (auto e : list)
                    order[i++] = (std::uint8_t) e;
        }
    };

    // Routages tout prêts, pour le menu Routing
    struct Template
    {
        const char* name;
        Routing routing;
    };

    inline Routing make (std::vector<int> preList, std::vector<int> a, std::vector<int> b, std::vector<int> postList)
    {
        Routing r = Routing::series();
        size_t i = 0;
        int l = pre;
        for (auto* list : { &preList, &a, &b, &postList })
        {
            for (auto e : *list)
            {
                r.order[i++] = (std::uint8_t) e;
                r.lane[(size_t) e] = (std::uint8_t) l;
            }
            ++l;
        }
        return i == numEffects ? r : Routing::series();
    }

    inline const std::vector<Template>& templates()
    {
        static const std::vector<Template> list {
            { "Series",
              Routing::series() },
            // Les hauteurs d'un côté, les échos de l'autre : chacun garde sa netteté
            { "Pitch | Delays",
              make ({ compressor, filter, amp }, { harmonizer, reverse, layered }, { chorus, delay, bandDelay, ultraTap }, { reverb }) },
            // Le son sec garde son attaque, les effets s'ajoutent à côté
            { "Dry | Effects",
              make ({ compressor, filter, amp }, {}, { harmonizer, reverse, layered, chorus, delay, bandDelay, ultraTap }, { reverb }) },
            // Échos et réverbération en parallèle : la réverbe ne brouille pas les répétitions
            { "Delays | Reverb",
              make ({ compressor, filter, amp, harmonizer, reverse, layered, chorus }, { delay, bandDelay, ultraTap }, { reverb }, {}) },
            // Le rack de Bob Bradshaw : le son sec de l'ampli au centre, les effets à 100 % autour.
            // Choisi dans le menu, il allume aussi « Wet Only » (branche B sans son sec)
            { "Wet/Dry/Wet",
              make ({ compressor, filter, amp }, {}, { harmonizer, reverse, layered, chorus, delay, bandDelay, ultraTap, reverb }, {}) },
            // Deux harmonies différentes, une à gauche et une à droite (avec Spread)
            { "Harmonizer | Layered",
              make ({ compressor, filter, amp }, { harmonizer, chorus }, { layered, reverse }, { delay, bandDelay, ultraTap, reverb }) },
        };
        return list;
    }
}
