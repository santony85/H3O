#pragma once

#include "ChainRouting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// La baie de brassage de H3O Chain, dans l'esprit des câbles de la H3000 :
// chaque pédale a une prise IN et une prise OUT, et des câbles (stéréo) les relient librement.
//
//   - une prise IN accepte plusieurs câbles : ils s'additionnent ;
//   - une prise OUT peut partir vers plusieurs pédales : le son est dupliqué ;
//   - 4 mélangeurs (Mixer 1 à 4) ont deux entrées A et B, avec niveau et panoramique :
//     ils séparent ou réunissent la gauche et la droite ;
//   - les boucles sont permises : le câble qui revient en arrière transporte le son avec 64 échantillons de retard
//     (≈ 1,3 ms), adouci pour ne jamais s'emballer ;
//   - une pédale qui n'est pas sur un chemin de l'entrée à la sortie ne travaille pas.
//
// Chaque pédale a aussi une place sur l'écran (de 0 à 1 en largeur et en hauteur), que l'on choisit en la glissant.
// En C++ pur, sans JUCE, pour pouvoir le tester seul.
namespace ChainPatch
{
    using ChainRouting::numEffects;

    constexpr int numMixers = 4;
    constexpr int inputNode  = numEffects + numMixers;   // l'entrée de la chaîne
    constexpr int outputNode = inputNode + 1;            // la sortie
    constexpr int numNodes   = outputNode + 1;
    constexpr int maxCables  = 64;
    constexpr int feedbackChunk = 64;                    // retard d'un câble qui revient en arrière (échantillons)

    inline constexpr int mixerNode (int m) { return numEffects + m; }
    inline constexpr bool isEffect (int node) { return node >= 0 && node < numEffects; }
    inline constexpr bool isMixer (int node)  { return node >= numEffects && node < numEffects + numMixers; }

    // Nombre de prises IN d'un nœud (l'entrée de la chaîne n'en a pas, les mélangeurs en ont deux)
    inline constexpr int numInputs (int node)  { return node == inputNode ? 0 : (isMixer (node) ? 2 : 1); }
    inline constexpr bool hasOutput (int node) { return node >= 0 && node < numNodes && node != outputNode; }

    inline std::string key (int node)
    {
        if (isEffect (node))
            return ChainRouting::keys()[(size_t) node];
        if (isMixer (node))
            return "mixer" + std::to_string (node - numEffects + 1);
        return node == inputNode ? "input" : "output";
    }

    inline int nodeFromKey (const std::string& k)
    {
        for (int n = 0; n < numNodes; ++n)
            if (key (n) == k)
                return n;
        return -1;
    }

    struct Cable
    {
        std::uint8_t from = 0, to = 0, port = 0;   // de la sortie de « from » vers la prise « port » de « to »

        bool operator== (const Cable& o) const { return from == o.from && to == o.to && port == o.port; }
        bool operator!= (const Cable& o) const { return ! (*this == o); }
    };

    struct Position
    {
        float x = 0.5f, y = 0.5f;
        bool operator== (const Position& o) const { return std::abs (x - o.x) < 1.0e-4f && std::abs (y - o.y) < 1.0e-4f; }
    };

    struct Patch
    {
        std::vector<Cable> cables;
        std::array<Position, numNodes> position {};

        bool operator== (const Patch& o) const { return cables == o.cables && position == o.position; }
        bool operator!= (const Patch& o) const { return ! (*this == o); }

        static bool valid (const Cable& c)
        {
            return hasOutput (c.from) && c.to < numNodes && c.port < numInputs (c.to);
        }

        // Ajoute un câble ; refusé s'il existe déjà, s'il n'a pas de sens, ou s'il y en a trop
        bool connect (int from, int to, int port = 0)
        {
            Cable c { (std::uint8_t) from, (std::uint8_t) to, (std::uint8_t) port };
            if (from < 0 || to < 0 || port < 0 || ! valid (c) || (int) cables.size() >= maxCables
                || std::find (cables.begin(), cables.end(), c) != cables.end())
                return false;
            cables.push_back (c);
            return true;
        }

        bool disconnect (const Cable& c)
        {
            const auto it = std::find (cables.begin(), cables.end(), c);
            if (it == cables.end())
                return false;
            cables.erase (it);
            return true;
        }

        // Pour les sessions : "input>compressor.0 compressor>delay.0 … | compressor@0.050,0.500 …"
        // Un nom inconnu (effet d'une version future) est ignoré ; une place absente garde sa valeur par défaut.
        std::string toString() const
        {
            std::ostringstream s;
            for (size_t i = 0; i < cables.size(); ++i)
                s << (i > 0 ? " " : "") << key (cables[i].from) << '>' << key (cables[i].to) << '.' << (int) cables[i].port;
            s << " |";
            char buffer[64];
            for (int n = 0; n < numNodes; ++n)
            {
                std::snprintf (buffer, sizeof buffer, " %s@%.4f,%.4f", key (n).c_str(), (double) position[(size_t) n].x, (double) position[(size_t) n].y);
                s << buffer;
            }
            return s.str();
        }

        static Patch fromString (const std::string& text)
        {
            Patch p = defaultLayout();
            const auto bar = text.find ('|');
            std::istringstream cablesPart (text.substr (0, bar));
            std::string item;
            while (cablesPart >> item)
            {
                const auto arrow = item.find ('>'), dot = item.rfind ('.');
                if (arrow == std::string::npos || dot == std::string::npos || dot < arrow)
                    continue;
                const int from = nodeFromKey (item.substr (0, arrow));
                const int to   = nodeFromKey (item.substr (arrow + 1, dot - arrow - 1));
                const int port = std::atoi (item.substr (dot + 1).c_str());
                p.connect (from, to, port);
            }

            if (bar != std::string::npos)
            {
                std::istringstream positions (text.substr (bar + 1));
                while (positions >> item)
                {
                    const auto at = item.find ('@'), comma = item.find (',');
                    if (at == std::string::npos || comma == std::string::npos || comma < at)
                        continue;
                    const int node = nodeFromKey (item.substr (0, at));
                    if (node < 0)
                        continue;
                    const float x = (float) std::atof (item.substr (at + 1, comma - at - 1).c_str());
                    const float y = (float) std::atof (item.substr (comma + 1).c_str());
                    p.position[(size_t) node] = { std::clamp (x, 0.0f, 1.0f), std::clamp (y, 0.0f, 1.0f) };
                }
            }
            return p;
        }

        // Rangées en haut : une pédale sur deux un peu plus bas quand elles sont nombreuses (les jacks ne se touchent pas)
        static float parkedRow (int index, int count) { return count > 11 && index % 2 == 1 ? 0.18f : 0.0f; }

        // Une baie sans câble : les pédales rangées en haut (dans l'ordre de la série), les mélangeurs en bas
        static Patch defaultLayout()
        {
            Patch p;
            p.position[(size_t) inputNode]  = { 0.0f, 0.55f };
            p.position[(size_t) outputNode] = { 1.0f, 0.55f };
            const auto order = ChainRouting::Routing::series().order;
            for (int i = 0; i < numEffects; ++i)
                p.position[order[(size_t) i]] = { (float) i / (float) (numEffects - 1), parkedRow (i, numEffects) };
            for (int m = 0; m < numMixers; ++m)
                p.position[(size_t) mixerNode (m)] = { 0.55f + 0.15f * (float) m, 1.0f };
            return p;
        }
    };

    // Le routage à branches traduit en câbles : même son (sauf Cross Feedback et Width, propres aux branches),
    // et le même schéma à l'écran. Wet Only : chaque pédale de B reçoit le son de la séparation, côte à côte.
    // Seules les pédales allumées sont câblées (une pédale éteinte laisse passer le son : rien ne change) ;
    // les autres attendent en haut de la baie.
    struct Conversion
    {
        Patch patch;
        float levelA = 100.0f, levelB = 100.0f, panA = 0.0f, panB = 0.0f;   // réglages de Mixer 1 (%)
    };

    inline Conversion fromRouting (const ChainRouting::Routing& r, float balance, float spread, bool wetOnly,
                                   const std::array<bool, numEffects>& on)
    {
        using namespace ChainRouting;
        Conversion c;
        auto& p = c.patch;
        p = Patch::defaultLayout();

        auto active = [&r, &on] (int lane)
        {
            std::vector<int> list;
            for (auto e : r.effectsIn (lane))
                if (on[(size_t) e])
                    list.push_back (e);
            return list;
        };
        const auto pre = active (ChainRouting::pre), a = active (branchA), b = active (branchB), post = active (ChainRouting::post);
        const bool parallel = r.hasParallel();

        // Colonnes : avant | A et B | mélangeur | après
        const int columns = (int) pre.size() + (parallel ? (int) std::max (a.size(), b.size()) + 1 : 0) + (int) post.size();
        auto x = [columns] (int column) { return 0.06f + 0.88f * (columns > 1 ? (float) column / (float) (columns - 1) : 0.5f); };

        int column = 0, last = inputNode;
        auto chain = [&] (const std::vector<int>& list, float y, int& from)
        {
            for (auto e : list)
            {
                p.position[(size_t) e] = { x (column++), y };
                p.connect (from, e);
                from = e;
            }
        };

        constexpr float rowA = 0.3f, rowMain = 0.55f, rowB = 0.8f;
        chain (pre, rowMain, last);

        if (parallel)
        {
            const int split = last, start = column;
            int endA = split, endB = split;
            chain (a, rowA, endA);
            column = start;
            if (wetOnly)
            {
                for (auto e : b)
                {
                    p.position[(size_t) e] = { x (column++), rowB };
                    p.connect (split, e);
                }
            }
            else
            {
                chain (b, rowB, endB);
            }
            column = start + (int) std::max (a.size(), b.size());

            const int mixer = mixerNode (0);
            p.position[(size_t) mixer] = { x (column++), rowMain };
            p.connect (endA, mixer, 0);
            if (wetOnly)
                for (auto e : b)
                    p.connect (e, mixer, 1);   // B vide en Wet Only : rien (comme avant)
            else
                p.connect (endB, mixer, 1);

            const float bal = std::clamp (balance, 0.0f, 100.0f) * 0.01f;
            c.levelA = 100.0f * (wetOnly ? std::min (1.0f, 2.0f * (1.0f - bal)) : 1.0f - bal);
            c.levelB = 100.0f * (wetOnly ? std::min (1.0f, 2.0f * bal) : bal);
            c.panA = -std::clamp (spread, 0.0f, 100.0f);
            c.panB =  std::clamp (spread, 0.0f, 100.0f);
            last = mixer;

        }

        chain (post, rowMain, last);
        p.connect (last, outputNode);
        return c;
    }

    // Ce que le moteur audio exécute : l'ordre des nœuds et, pour chaque câble, s'il revient en arrière
    struct Plan
    {
        struct Link { std::uint8_t from = 0, to = 0, port = 0; bool delayed = false; };

        int numSteps = 0;
        std::array<std::uint8_t, numNodes> steps {};   // nœuds à calculer, dans l'ordre (sans l'entrée ni la sortie)
        int numLinks = 0;
        std::array<Link, maxCables> links {};
        std::array<bool, numNodes> live {};            // sur un chemin de l'entrée à la sortie
        bool hasDelayed = false;
    };

    inline Plan compile (const Patch& p)
    {
        Plan plan;

        // Pédales sur un chemin : atteintes depuis l'entrée ET menant à la sortie
        std::array<bool, numNodes> fromInput {}, toOutput {};
        fromInput[(size_t) inputNode] = toOutput[(size_t) outputNode] = true;
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto& c : p.cables)
            {
                if (fromInput[c.from] && ! fromInput[c.to])  { fromInput[c.to] = true; changed = true; }
                if (toOutput[c.to] && ! toOutput[c.from])    { toOutput[c.from] = true; changed = true; }
            }
        }
        for (int n = 0; n < numNodes; ++n)
            plan.live[(size_t) n] = fromInput[(size_t) n] && toOutput[(size_t) n];

        // Ordre : parcours en profondeur depuis l'entrée ; un câble vers un nœud encore « en cours » ferme une boucle
        std::array<int, numNodes> state {};   // 0 = pas vu, 1 = en cours, 2 = fini
        std::vector<int> finished;
        std::vector<std::pair<int, int>> backEdges;   // (from, to)

        auto visit = [&] (auto&& self, int node) -> void
        {
            state[(size_t) node] = 1;
            for (auto& c : p.cables)
                if (c.from == node && plan.live[c.to])
                {
                    if (state[c.to] == 0)
                        self (self, c.to);
                    else if (state[c.to] == 1)
                        backEdges.push_back ({ c.from, c.to });
                }
            state[(size_t) node] = 2;
            finished.push_back (node);
        };
        if (plan.live[(size_t) inputNode])
            visit (visit, inputNode);

        std::reverse (finished.begin(), finished.end());
        for (auto n : finished)
            if (n != inputNode && n != outputNode)
                plan.steps[(size_t) plan.numSteps++] = (std::uint8_t) n;

        for (auto& c : p.cables)
            if (plan.live[c.from] && plan.live[c.to])
            {
                Plan::Link link { c.from, c.to, c.port, false };
                link.delayed = std::find (backEdges.begin(), backEdges.end(), std::make_pair ((int) c.from, (int) c.to)) != backEdges.end();
                plan.hasDelayed = plan.hasDelayed || link.delayed;
                plan.links[(size_t) plan.numLinks++] = link;
            }

        return plan;
    }
}
