// Inclus dans FactoryPresets.cpp (espace de noms anonyme) : la banque « Patch Bay ».
// 50 rigs câblés dans la baie de brassage : plusieurs effets par chemin, dans des ordres variés,
// des splits à différents niveaux de la chaîne, des mixeurs modulés et des boucles de réinjection.

    // Une pédale de la baie (sa section de branche ne compte pas : ce sont les câbles qui décident)
    Fx fx (Effect e, int program = -1, std::vector<P> p = {}) { return { e, program, std::move (p), pre }; }

    // Une pédale « départ d'effet » : 100 % d'effet, le son sec passe par un autre câble
    Fx wet (Effect e, int program = -1, std::vector<P> p = {}) { return { e, program, std::vector<P> { { "mix", 100 } } + p, pre }; }

    // Réglages d'un mixeur (1 à 4) : niveaux A et B (%), panoramiques A et B (−100 = gauche, 100 = droite)
    std::vector<P> mixer (int m, float levelA, float levelB, float panA = 0.0f, float panB = 0.0f)
    {
        static const char* const ids[4][4] = {
            { "mix1levela", "mix1levelb", "mix1pana", "mix1panb" }, { "mix2levela", "mix2levelb", "mix2pana", "mix2panb" },
            { "mix3levela", "mix3levelb", "mix3pana", "mix3panb" }, { "mix4levela", "mix4levelb", "mix4pana", "mix4panb" }
        };
        const auto& id = ids[m - 1];
        return { { id[0], levelA }, { id[1], levelB }, { id[2], panA }, { id[3], panB } };
    }

    // Harmoniseur chromatique d'une seule voix (A), en demi-tons
    std::vector<P> shifted (float semitones, float mix = 100.0f)
    {
        return { { "mode", 0 }, { "asemi", semitones }, { "acents", 0 }, { "adelay", 0 }, { "alevel", 100 }, { "blevel", 0 }, { "mix", mix } };
    }

    // Pose les câbles de la recette et range les pédales : une colonne par étape depuis l'entrée,
    // les chemins parallèles l'un sous l'autre, les pédales inutilisées en haut
    juce::String applyCables (const Recipe& recipe, const std::array<bool, numEffects>& used, ChainProcessor& chain)
    {
        using namespace ChainPatch;

        int items = 0;
        {
            std::istringstream in (recipe.cables);
            std::string item;
            while (in >> item)
                ++items;
        }
        auto patch = Patch::fromString (recipe.cables);
        if ((int) patch.cables.size() != items)
            return "a cable is invalid or listed twice";

        const auto plan = compile (patch);
        if (! plan.live[(size_t) outputNode])
            return "nothing reaches the output";
        for (int e = 0; e < numEffects; ++e)
            if (used[(size_t) e] && ! plan.live[(size_t) e])
                return juce::String (keys()[(size_t) e]) + " is not on a path from the input to the output";

        // Profondeur : le plus long chemin depuis l'entrée (sans les retours de boucle)
        std::array<int, numNodes> depth {};
        int deepest = 0;
        for (int s = 0; s < plan.numSteps; ++s)
        {
            const int node = plan.steps[(size_t) s];
            for (int k = 0; k < plan.numLinks; ++k)
            {
                const auto& link = plan.links[(size_t) k];
                if (link.to == node && ! link.delayed)
                    depth[(size_t) node] = std::max (depth[(size_t) node], depth[link.from] + 1);
            }
            deepest = std::max (deepest, depth[(size_t) node]);
        }
        const float columns = (float) (deepest + 1);

        // Chaque colonne : triée par la hauteur moyenne de ce qui l'alimente (moins de câbles croisés)
        std::array<float, numNodes> y {};
        y[(size_t) inputNode] = 0.6f;
        for (int d = 1; d <= deepest; ++d)
        {
            std::vector<std::pair<float, int>> column;
            for (int s = 0; s < plan.numSteps; ++s)
            {
                const int node = plan.steps[(size_t) s];
                if (depth[(size_t) node] != d)
                    continue;
                float sum = 0.0f;
                int count = 0;
                for (int k = 0; k < plan.numLinks; ++k)
                {
                    const auto& link = plan.links[(size_t) k];
                    if (link.to == node && ! link.delayed)
                    {
                        sum += y[link.from] + 0.01f * (float) link.port;
                        ++count;
                    }
                }
                column.push_back ({ count > 0 ? sum / (float) count : 0.6f, node });
            }
            std::stable_sort (column.begin(), column.end(), [] (auto& a, auto& b) { return a.first < b.first; });

            const int k = (int) column.size();
            const float spacing = k > 1 ? std::min (0.35f, 0.7f / (float) (k - 1)) : 0.0f;
            for (int i = 0; i < k; ++i)
            {
                const int node = column[(size_t) i].second;
                y[(size_t) node] = 0.6f + ((float) i - 0.5f * (float) (k - 1)) * spacing;
                patch.position[(size_t) node] = { (float) d / columns, y[(size_t) node] };
            }
        }
        patch.position[(size_t) inputNode]  = { 0.0f, 0.6f };
        patch.position[(size_t) outputNode] = { 1.0f, 0.6f };

        // Le reste, rangé en haut : pédales puis mixeurs
        std::vector<int> parked;
        for (auto e : Routing::series().order)
            if (! plan.live[(size_t) e])
                parked.push_back (e);
        for (int m = 0; m < numMixers; ++m)
            if (! plan.live[(size_t) mixerNode (m)])
                parked.push_back (mixerNode (m));
        for (size_t i = 0; i < parked.size(); ++i)
            patch.position[(size_t) parked[i]] = { ((float) i + 0.5f) / (float) parked.size(), Patch::parkedRow ((int) i, (int) parked.size()) };

        chain.setPatch (patch);
        return {};
    }

    const std::vector<Recipe>& patchBay()
    {
        static const std::vector<Recipe> list {
            //==================================================================
            // Chemins parallèles : deux chemins de plusieurs effets, dans des ordres différents, réunis par un mixeur
            { "Parallel Paths", "Glass Cathedral",
              { wet (chorus, 1), wet (delay, 6), wet (phaser, 5), wet (reverse, 5), fx (reverb, 0, { { "mix", 25 } }) },
              mixer (1, 70, 70, -60, 60) + mixer (2, 85, 55) + lfo (1, 0.08f, sine),
              { { lfo1, "branches/mix1pana", 30 }, { lfo1, "branches/mix1panb", -30 } },
              "input>amp.0 amp>chorus.0 chorus>delay.0 amp>phaser.0 phaser>reverse.0 delay>mixer1.0 reverse>mixer1.1 "
              "amp>mixer2.0 mixer1>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Parallel Paths", "Twin Amp Voices",
              { fx (filter, 2), fx (chorus, 0), fx (tremolo, 8), fx (delay, 9, { { "mix", 30 } }), wet (reverb, 4) },
              mixer (1, 70, 70, -50, 50) + mixer (2, 85, 35) + env (10, 300),
              { { envelope, "branches/mix1levelb", 25 } },
              "input>amp.0 amp>filter.0 filter>chorus.0 amp>tremolo.0 tremolo>delay.0 chorus>mixer1.0 delay>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Parallel Paths", "Opposite Corners",
              { fx (reverse, 0, { { "mix", 60 } }), fx (bandDelay, 1, { { "mix", 40 } }),
                fx (ultraTap, 1, { { "mix", 40 } }), fx (layered, 1, { { "mix", 40 } }), wet (reverb, 3) },
              mixer (1, 65, 65, -70, 70) + mixer (2, 85, 35) + lfoSync (1, bars2, triangle),
              { { lfo1, "branches/mix1pana", 40 }, { lfo1, "branches/mix1panb", -40 } },
              "input>amp.0 amp>reverse.0 reverse>banddelay.0 amp>ultratap.0 ultratap>layered.0 banddelay>mixer1.0 layered>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Parallel Paths", "Funk Duality",
              { fx (compressor, 2), fx (filter, 1), wet (phaser, 2), fx (chorus, 3), fx (delay, 2, { { "mix", 30 } }) },
              mixer (1, 75, 65, -40, 40) + mixer (2, 70, 55, -30, 30) + env (5, 150),
              { { envelope, "phaser/centre", 30 } },
              "input>compressor.0 compressor>amp.0 amp>filter.0 filter>phaser.0 filter>mixer2.0 phaser>mixer2.1 amp>chorus.0 chorus>delay.0 "
              "mixer2>mixer1.0 delay>mixer1.1 mixer1>output.0" },
            { "Parallel Paths", "Harmony Halo (E Minor)",
              { fx (harmonizer, -1, diatonic (E, naturalMinor, 2, 4, 100)), wet (delay, 9), fx (chorus, 3), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 70, 85) + mixer (2, 55, 60, -60, 60) + lfo (1, 0.15f, sine),
              { { lfo1, "branches/mix2pana", 40 }, { lfo1, "branches/mix2panb", -40 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>mixer2.0 harmonizer>delay.0 delay>mixer2.1 amp>chorus.0 mixer2>mixer1.0 chorus>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Parallel Paths", "Wall of Glass",
              { fx (phaser, 5), wet (reverse, 5), wet (reverb, 6), fx (chorus, 1, { { "mix", 60 } }), fx (delay, 4, { { "mix", 40 } }) },
              mixer (1, 55, 75, -50, 50) + lfo (1, 0.12f, triangle) + lfoSync (2, bars4, sine),
              { { lfo1, "chorus/depth", 30 }, { lfo2, "branches/mix1levela", 30 } },
              "input>amp.0 amp>phaser.0 phaser>reverse.0 reverse>reverb.0 phaser>chorus.0 chorus>delay.0 "
              "reverb>mixer1.0 delay>mixer1.1 mixer1>output.0" },
            { "Parallel Paths", "Ducked Studio Rack",
              { fx (compressor, 0), fx (chorus, 3), fx (reverb, 2, { { "mix", 25 } }), wet (delay, 6), fx (filter, -1, fixedFilter (0, 400, 1800, 20)) },
              mixer (1, 85, 50, -20, 60) + env (5, 400),
              { { envelope, "branches/mix1levelb", -35 } },
              "input>compressor.0 compressor>amp.0 amp>chorus.0 chorus>reverb.0 amp>delay.0 delay>filter.0 "
              "reverb>mixer1.0 filter>mixer1.1 mixer1>output.0" },
            { "Parallel Paths", "Tremolo Crossfire",
              { fx (tremolo, 5), fx (delay, 10, { { "mix", 30 } }), fx (phaser, 8), fx (ultraTap, 0, { { "mix", 30 } }), wet (reverb, 1) },
              mixer (1, 70, 70, -50, 50) + mixer (2, 85, 30) + lfoSync (1, bars2, triangle),
              { { lfo1, "branches/mix1pana", 40 }, { lfo1, "branches/mix1panb", -40 } },
              "input>amp.0 amp>tremolo.0 tremolo>delay.0 amp>phaser.0 phaser>ultratap.0 delay>mixer1.0 ultratap>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Parallel Paths", "Chicken Pickin' Rack",
              { fx (compressor, 1), fx (delay, 2, { { "mix", 35 } }), fx (tremolo, 2), fx (chorus, 3, { { "mix", 60 } }), fx (reverb, 1, { { "mix", 30 } }) },
              mixer (1, 70, 70, -40, 40) + env (5, 200),
              { { envelope, "tremolo/depth", -40 } },
              "input>compressor.0 compressor>amp.0 amp>delay.0 delay>tremolo.0 amp>chorus.0 chorus>reverb.0 "
              "tremolo>mixer1.0 reverb>mixer1.1 mixer1>output.0" },
            { "Parallel Paths", "Liquid Duet",
              { fx (layered, 1, { { "mix", 50 } }), fx (phaser, 1), wet (chorus, 4), fx (bandDelay, 6, { { "mix", 35 } }), wet (reverb, 5) },
              mixer (1, 70, 60, -50, 50) + mixer (2, 85, 30) + lfo (1, 0.2f, sine) + lfo (2, 0.07f, triangle),
              { { lfo1, "phaser/feedback", 30 }, { lfo2, "chorus/rate", 20 } },
              "input>amp.0 amp>layered.0 layered>phaser.0 amp>chorus.0 chorus>banddelay.0 phaser>mixer1.0 banddelay>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },

            //==================================================================
            // Splits imbriqués : un chemin se divise encore, plus loin dans la chaîne
            { "Nested Splits", "Branching Tree",
              { fx (chorus, 3), fx (harmonizer, 2, { { "mix", 100 } }), wet (delay, 9), wet (reverse, 1), fx (reverb, 3, { { "mix", 20 } }) },
              mixer (1, 80, 60) + mixer (2, 50, 50, -70, 70) + lfoSync (1, bars4, sine),
              { { lfo1, "branches/mix2pana", 50 }, { lfo1, "branches/mix2panb", -50 } },
              "input>amp.0 amp>chorus.0 amp>harmonizer.0 harmonizer>delay.0 harmonizer>reverse.0 delay>mixer2.0 reverse>mixer2.1 "
              "chorus>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Nested Splits", "Three-Way Split",
              { fx (compressor, 6), wet (chorus, 1), wet (delay, 5), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 80, 50, 0, -60) + mixer (2, 85, 45, 0, 70) + lfo (1, 0.1f, sine),
              { { lfo1, "delay/feedback", 20 } },
              "input>compressor.0 compressor>amp.0 amp>mixer1.0 amp>chorus.0 chorus>mixer1.1 mixer1>mixer2.0 amp>delay.0 delay>mixer2.1 "
              "mixer2>reverb.0 reverb>output.0" },
            { "Nested Splits", "Late Split Ambience",
              { fx (compressor, 3), fx (filter, -1, fixedFilter (0, 2500, 6000, 10)), fx (phaser, 9), wet (reverb, 6), wet (ultraTap, 6) },
              mixer (1, 60, 60, -60, 60) + mixer (2, 80, 60) + lfo (1, 0.09f, triangle),
              { { lfo1, "ultratap/length", 25 } },
              "input>compressor.0 compressor>amp.0 amp>filter.0 filter>phaser.0 phaser>reverb.0 phaser>ultratap.0 phaser>mixer2.0 "
              "reverb>mixer1.0 ultratap>mixer1.1 mixer1>mixer2.1 mixer2>output.0" },
            { "Nested Splits", "Early & Late Splits (G Major)",
              { fx (harmonizer, -1, diatonic (G, major, 2, 4, 100)), fx (delay, 6, { { "mix", 35 } }), wet (reverse, 0), fx (reverb, 4, { { "mix", 20 } }) },
              mixer (1, 55, 85, -50, 0) + mixer (2, 80, 45, 0, 60) + lfoSync (1, bars2, sine),
              { { lfo1, "branches/mix2levelb", 40 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>mixer1.0 amp>mixer1.1 mixer1>delay.0 delay>mixer2.0 delay>reverse.0 reverse>mixer2.1 "
              "mixer2>reverb.0 reverb>output.0" },
            { "Nested Splits", "Fractal Echoes",
              { wet (delay, 9), wet (bandDelay, 0), wet (ultraTap, 1), fx (reverb, 1, { { "mix", 15 } }) },
              mixer (1, 85, 70) + mixer (2, 45, 45, -70, 70) + lfo (1, 0.15f, sine) + lfoSync (2, bar1, triangle),
              { { lfo1, "banddelay/low", 30 }, { lfo2, "branches/mix2pana", 40 } },
              "input>amp.0 amp>delay.0 delay>banddelay.0 delay>ultratap.0 banddelay>mixer2.0 ultratap>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Nested Splits", "Octave Garden",
              { wet (layered, 2), wet (chorus, 2), wet (reverb, 6), fx (phaser, 9) },
              mixer (1, 85, 60) + mixer (2, 55, 55, -60, 60) + lfo (1, 0.1f, sine) + env (20, 800),
              { { lfo1, "layered/alevel", 30 }, { envelope, "branches/mix1levelb", 25 } },
              "input>amp.0 amp>layered.0 layered>chorus.0 layered>reverb.0 chorus>mixer2.0 reverb>mixer2.1 "
              "amp>phaser.0 phaser>mixer1.0 mixer2>mixer1.1 mixer1>output.0" },
            { "Nested Splits", "Funk Splitter",
              { fx (compressor, 2), fx (filter, 1), wet (phaser, 2), fx (tremolo, 3), fx (delay, 2, { { "mix", 20 } }) },
              mixer (1, 70, 50) + mixer (2, 75, 50, -30, 60) + env (5, 150),
              { { envelope, "phaser/rate", 30 } },
              "input>compressor.0 compressor>amp.0 amp>filter.0 filter>mixer1.0 filter>phaser.0 phaser>mixer1.1 filter>tremolo.0 "
              "mixer1>mixer2.0 tremolo>mixer2.1 mixer2>delay.0 delay>output.0" },
            { "Nested Splits", "Diamond",
              { wet (chorus, 0), wet (phaser, 6), wet (delay, 10), wet (reverb, 3) },
              mixer (1, 60, 60, -50, 50) + mixer (2, 55, 55, -40, 40) + mixer (3, 75, 60) + lfo (1, 0.3f, sine),
              { { lfo1, "delay/time", 3 } },
              "input>amp.0 amp>chorus.0 amp>phaser.0 chorus>mixer1.0 phaser>mixer1.1 mixer1>delay.0 mixer1>reverb.0 "
              "delay>mixer2.0 reverb>mixer2.1 amp>mixer3.0 mixer2>mixer3.1 mixer3>output.0" },
            { "Nested Splits", "Reverse Cascade",
              { wet (reverse, 0), wet (layered, 5), wet (delay, 8), fx (reverb, 7, { { "mix", 25 } }) },
              mixer (1, 85, 60) + mixer (2, 55, 55, -60, 60) + lfo (1, 0.12f, triangle),
              { { lfo1, "reverse/length", 20 } },
              "input>amp.0 amp>reverse.0 reverse>layered.0 reverse>delay.0 layered>mixer2.0 delay>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Nested Splits", "Pitch Tree (A Minor)",
              { fx (harmonizer, -1, diatonic (A, naturalMinor, 2, 4, 100)), wet (delay, 9), wet (chorus, 2), fx (reverb, 4, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 55, -70, 70),
              { { pitch, "branches/mix1levelb", 30 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>delay.0 harmonizer>chorus.0 delay>mixer2.0 chorus>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },

            //==================================================================
            // Mixeurs en mouvement : les niveaux et panoramiques des mixeurs sont modulés
            { "Moving Mixes", "Orbiting Echoes",
              { wet (delay, 10), wet (reverb, 3) },
              mixer (1, 85, 55) + mixer (2, 90, 35) + lfo (1, 0.2f, sine) + lfo (2, 0.05f, triangle),
              { { lfo1, "branches/mix1panb", 80 }, { lfo2, "delay/feedback", 15 } },
              "input>amp.0 amp>mixer1.0 amp>delay.0 delay>mixer1.1 delay>reverb.0 mixer1>mixer2.0 reverb>mixer2.1 mixer2>output.0" },
            { "Moving Mixes", "Crossfade Machine",
              { fx (phaser, 3), fx (chorus, 1, { { "mix", 70 } }), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 60, 60, -30, 30) + lfoSync (1, bar1, sine),
              { { lfo1, "branches/mix1levela", 30 }, { lfo1, "branches/mix1levelb", -30 } },
              "input>amp.0 amp>phaser.0 amp>chorus.0 phaser>mixer1.0 chorus>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Moving Mixes", "Envelope Bloom",
              { wet (reverse, 5), wet (reverb, 6), fx (chorus, 3, { { "mix", 30 } }) },
              mixer (1, 85, 70) + env (30, 1200),
              { { envelope, "branches/mix1levelb", -40 } },
              "input>amp.0 amp>chorus.0 chorus>mixer1.0 amp>reverse.0 reverse>reverb.0 reverb>mixer1.1 mixer1>output.0" },
            { "Moving Mixes", "Random Scatter",
              { fx (chorus, 3), wet (bandDelay, 6), wet (reverb, 1) },
              mixer (1, 80, 55) + mixer (2, 85, 30) + lfo (1, 0.25f, triangle),
              { { random, "branches/mix1panb", 70 }, { lfo1, "banddelay/high", 30 } },
              "input>amp.0 amp>chorus.0 chorus>mixer1.0 amp>banddelay.0 banddelay>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Moving Mixes", "Tidal Stereo",
              { fx (phaser, 4), wet (delay, 5), wet (reverb, 0) },
              mixer (1, 80, 55) + mixer (2, 85, 35) + lfo (1, 0.05f, triangle) + lfo (2, 0.07f, sine),
              { { lfo1, "branches/mix1pana", 40 }, { lfo2, "branches/mix1panb", -60 } },
              "input>amp.0 amp>phaser.0 phaser>mixer1.0 phaser>delay.0 delay>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Moving Mixes", "Pitch-Tracked Blend",
              { fx (harmonizer, 1, { { "mix", 100 } }), fx (delay, 6, { { "mix", 25 } }) },
              mixer (1, 40, 85, -40, 20),
              { { pitch, "branches/mix1levela", 40 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>mixer1.0 amp>mixer1.1 mixer1>delay.0 delay>output.0" },
            { "Moving Mixes", "Gated Harmonies (D Dorian)",
              { fx (harmonizer, -1, diatonic (D, dorian, 2, 4, 100)), fx (reverb, 2, { { "mix", 15 } }) },
              mixer (1, 85, 50) + lfoSync (1, l8, square),
              { { lfo1, "branches/mix1levelb", 50 } },
              "input>amp.0 amp>mixer1.0 amp>harmonizer.0 harmonizer>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Moving Mixes", "Swinging Doors",
              { fx (tremolo, 9), fx (chorus, 0), fx (filter, 3), fx (delay, 9, { { "mix", 30 } }), wet (reverb, 4) },
              mixer (1, 65, 65, -60, 60) + mixer (2, 85, 30) + lfoSync (1, bars2, sine),
              { { lfo1, "branches/mix1pana", 40 }, { lfo1, "branches/mix1levelb", 20 } },
              "input>amp.0 amp>tremolo.0 tremolo>chorus.0 amp>filter.0 filter>delay.0 chorus>mixer1.0 delay>mixer1.1 "
              "mixer1>mixer2.0 mixer1>reverb.0 reverb>mixer2.1 mixer2>output.0" },
            { "Moving Mixes", "Breathing Room",
              { fx (compressor, 6), wet (reverb, 3) },
              mixer (1, 85, 70) + env (40, 1500) + lfo (1, 0.1f, sine),
              { { envelope, "branches/mix1levelb", -40 }, { lfo1, "reverb/modulation", 30 } },
              "input>compressor.0 compressor>amp.0 amp>mixer1.0 amp>reverb.0 reverb>mixer1.1 mixer1>output.0" },
            { "Moving Mixes", "Spectral Pan",
              { wet (reverse, 6), wet (layered, 4), fx (reverb, 5, { { "mix", 20 } }) },
              mixer (1, 50, 50, -50, 50) + mixer (2, 85, 60) + lfo (1, 0.11f, sine) + lfo (2, 0.13f, sine),
              { { lfo1, "branches/mix1pana", 50 }, { lfo2, "branches/mix1panb", -50 } },
              "input>amp.0 amp>reverse.0 amp>layered.0 reverse>mixer1.0 layered>mixer1.1 amp>mixer2.0 mixer1>mixer2.1 "
              "mixer2>reverb.0 reverb>output.0" },

            //==================================================================
            // Boucles de réinjection : un mixeur renvoie la sortie d'un chemin à son entrée
            { "Feedback Networks", "Infinite Climb",
              { fx (harmonizer, 1, { { "mix", 100 } }), wet (delay, 4), fx (reverb, 6, { { "mix", 25 } }) },
              mixer (1, 100, 40) + mixer (2, 85, 55) + lfo (1, 0.1f, sine),
              { { lfo1, "branches/mix1levelb", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>harmonizer.0 harmonizer>delay.0 delay>mixer1.1 "
              "amp>mixer2.0 delay>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Feedback Networks", "Reverse Spiral",
              { wet (reverse, 5), wet (chorus, 2), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 100, 40) + mixer (2, 85, 60, 0, 30) + lfo (1, 0.08f, triangle),
              { { lfo1, "reverse/shift", 8 } },
              "input>amp.0 amp>mixer1.0 mixer1>reverse.0 reverse>chorus.0 chorus>mixer1.1 "
              "amp>mixer2.0 chorus>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Feedback Networks", "Band Loop",
              { wet (bandDelay, 1), fx (filter, -1, fixedFilter (1, 500, 2500, 40)), fx (reverb, 1, { { "mix", 15 } }) },
              mixer (1, 100, 45) + mixer (2, 85, 60) + lfoSync (1, bars2, triangle),
              { { lfo1, "banddelay/high", 30 } },
              "input>amp.0 amp>mixer1.0 mixer1>banddelay.0 banddelay>filter.0 filter>mixer1.1 "
              "amp>mixer2.0 filter>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Feedback Networks", "Cross-Coupled Echoes",
              { wet (delay, 9), wet (ultraTap, 5), fx (reverb, 1, { { "mix", 15 } }) },
              mixer (1, 100, 40) + mixer (2, 100, 40) + mixer (3, 50, 50, -70, 70) + mixer (4, 85, 60) + lfo (1, 0.06f, sine),
              { { lfo1, "branches/mix3pana", 30 }, { lfo1, "branches/mix3panb", -30 } },
              "input>amp.0 amp>mixer1.0 amp>mixer2.0 mixer1>delay.0 delay>mixer2.1 mixer2>ultratap.0 ultratap>mixer1.1 "
              "delay>mixer3.0 ultratap>mixer3.1 amp>mixer4.0 mixer3>mixer4.1 mixer4>reverb.0 reverb>output.0" },
            { "Feedback Networks", "Phaser Drone",
              { wet (phaser, 7), wet (layered, 1), fx (delay, 6, { { "mix", 30 } }) },
              mixer (1, 100, 35) + mixer (2, 85, 55) + lfo (1, 0.05f, sine),
              { { lfo1, "phaser/centre", 25 } },
              "input>amp.0 amp>mixer1.0 mixer1>phaser.0 phaser>layered.0 layered>mixer1.1 "
              "amp>mixer2.0 layered>mixer2.1 mixer2>delay.0 delay>output.0" },
            { "Feedback Networks", "Haunted Hall",
              { wet (reverb, 3), wet (reverse, 0), fx (chorus, 3, { { "mix", 30 } }) },
              mixer (1, 100, 40) + mixer (2, 85, 55) + lfo (1, 0.07f, triangle),
              { { lfo1, "reverse/length", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>reverb.0 reverb>reverse.0 reverse>mixer1.1 "
              "amp>mixer2.0 reverse>mixer2.1 mixer2>chorus.0 chorus>output.0" },
            { "Feedback Networks", "Octave Feedback Pad",
              { wet (layered, 2), wet (reverb, 6), fx (phaser, 9) },
              mixer (1, 100, 30) + mixer (2, 85, 55) + lfo (1, 0.06f, sine),
              { { lfo1, "branches/mix1levelb", 20 } },
              "input>amp.0 amp>mixer1.0 mixer1>layered.0 layered>reverb.0 reverb>mixer1.1 "
              "amp>mixer2.0 reverb>mixer2.1 mixer2>phaser.0 phaser>output.0" },
            { "Feedback Networks", "Filter Feedback Lab",
              { fx (filter, 3), wet (delay, 3), fx (reverb, 1, { { "mix", 15 } }) },
              mixer (1, 100, 45) + mixer (2, 85, 55) + env (10, 500),
              { { envelope, "branches/mix1levelb", 30 } },
              "input>amp.0 amp>mixer1.0 mixer1>filter.0 filter>delay.0 delay>mixer1.1 "
              "amp>mixer2.0 delay>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Feedback Networks", "Ping-Pong Cathedral",
              { fx (delay, 10, { { "mix", 35 } }), wet (reverb, 0), wet (chorus, 2) },
              mixer (1, 100, 35) + mixer (2, 85, 50) + lfo (1, 0.1f, sine),
              { { lfo1, "chorus/depth", 25 } },
              "input>amp.0 amp>delay.0 delay>mixer1.0 mixer1>reverb.0 reverb>chorus.0 chorus>mixer1.1 "
              "delay>mixer2.0 chorus>mixer2.1 mixer2>output.0" },
            { "Feedback Networks", "Tape Loop Drift",
              { wet (delay, 8), fx (tremolo, 2), fx (reverb, 7, { { "mix", 20 } }) },
              mixer (1, 100, 45) + mixer (2, 85, 55) + lfo (1, 0.3f, sine),
              { { random, "delay/time", 4 }, { lfo1, "tremolo/rate", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>delay.0 delay>tremolo.0 tremolo>mixer1.1 "
              "amp>mixer2.0 tremolo>mixer2.1 mixer2>reverb.0 reverb>output.0" },

            //==================================================================
            // Réseaux d'harmonies : plusieurs voix, chacune avec son traitement et sa place dans la stéréo
            { "Pitch Networks", "Octave Choir",
              { fx (harmonizer, 2, { { "mix", 100 } }), wet (layered, 1), wet (reverb, 6) },
              mixer (1, 50, 50, -50, 50) + mixer (2, 85, 55) + mixer (3, 85, 40) + lfo (1, 0.1f, sine),
              { { lfo1, "layered/detune", 20 } },
              "input>amp.0 amp>harmonizer.0 amp>layered.0 harmonizer>mixer1.0 layered>mixer1.1 "
              "amp>mixer2.0 mixer1>mixer2.1 mixer2>mixer3.0 mixer2>reverb.0 reverb>mixer3.1 mixer3>output.0" },
            { "Pitch Networks", "Thirds Left, Sixths Right (C Major)",
              { fx (harmonizer, -1, diatonic (C, major, 2, 5, 100) + std::vector<P> { { "spread", 100 } }), wet (delay, 9), fx (chorus, 3), fx (reverb, 4, { { "mix", 20 } }) },
              mixer (1, 85, 55) + lfoSync (1, bars4, triangle),
              { { lfo1, "delay/mix", -20 } },
              "input>amp.0 amp>chorus.0 chorus>mixer1.0 amp>harmonizer.0 harmonizer>delay.0 delay>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Pitch Networks", "Micro-Pitch Galaxy",
              { fx (harmonizer, 0, { { "mix", 100 } }), wet (chorus, 2), wet (reverse, 6), wet (reverb, 6) },
              mixer (1, 50, 45, -70, 70) + mixer (2, 85, 60) + mixer (3, 85, 35) + lfo (1, 0.07f, sine),
              { { lfo1, "harmonizer/acents", 15 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>chorus.0 amp>reverse.0 chorus>mixer1.0 reverse>mixer1.1 "
              "amp>mixer2.0 mixer1>mixer2.1 mixer2>mixer3.0 mixer2>reverb.0 reverb>mixer3.1 mixer3>output.0" },
            { "Pitch Networks", "Fifth Ladder",
              { wet (layered, 3), wet (ultraTap, 2), fx (phaser, 9), fx (reverb, 3, { { "mix", 20 } }) },
              mixer (1, 85, 55, 0, 40) + lfo (1, 0.12f, triangle),
              { { lfo1, "ultratap/predelay", 20 } },
              "input>amp.0 amp>phaser.0 phaser>mixer1.0 amp>layered.0 layered>ultratap.0 ultratap>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Pitch Networks", "Reverse Octave Swell",
              { wet (reverse, 2), wet (reverb, 6), fx (phaser, 9), fx (chorus, 3, { { "mix", 30 } }) },
              mixer (1, 85, 60) + lfo (1, 0.09f, sine),
              { { lfo1, "reverse/feedback", 20 } },
              "input>amp.0 amp>phaser.0 phaser>mixer1.0 amp>reverse.0 reverse>reverb.0 reverb>mixer1.1 mixer1>chorus.0 chorus>output.0" },
            { "Pitch Networks", "Dual Detune Doubler",
              { fx (compressor, 0), fx (harmonizer, 0, { { "mix", 100 } }), wet (chorus, 3), fx (delay, 2, { { "mix", 15 } }) },
              mixer (1, 45, 45, -80, 80) + mixer (2, 85, 65) + lfo (1, 0.2f, sine),
              { { lfo1, "chorus/depth", 15 } },
              "input>compressor.0 compressor>amp.0 amp>harmonizer.0 amp>chorus.0 harmonizer>mixer1.0 chorus>mixer1.1 "
              "amp>mixer2.0 mixer1>mixer2.1 mixer2>delay.0 delay>output.0" },
            { "Pitch Networks", "Harmonic Minor Maze (E)",
              { fx (harmonizer, -1, diatonic (E, harmonicMinor, 2, 4, 100)), wet (delay, 9), wet (reverse, 4), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 55, -60, 60) + lfoSync (1, bars2, sine),
              { { lfo1, "branches/mix2pana", 40 }, { lfo1, "branches/mix2panb", -40 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>delay.0 harmonizer>reverse.0 delay>mixer2.0 reverse>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Pitch Networks", "Sub & Shimmer",
              { wet (layered, 0), wet (reverb, 6), fx (harmonizer, -1, shifted (-12)), fx (filter, -1, fixedFilter (0, 200, 700, 10)) },
              mixer (1, 50, 45, 0, 0) + mixer (2, 85, 60) + lfo (1, 0.08f, sine),
              { { lfo1, "branches/mix1levela", 20 } },
              "input>amp.0 amp>layered.0 layered>reverb.0 amp>harmonizer.0 harmonizer>filter.0 reverb>mixer1.0 filter>mixer1.1 "
              "amp>mixer2.0 mixer1>mixer2.1 mixer2>output.0" },
            { "Pitch Networks", "Pentatonic Rain (A Minor)",
              { fx (harmonizer, -1, diatonic (A, minorPenta, 1, 2, 100)), wet (ultraTap, 1), fx (bandDelay, 6, { { "mix", 25 } }) },
              mixer (1, 85, 55, 0, 30),
              { { pitch, "ultratap/length", 20 } },
              "input>amp.0 amp>mixer1.0 amp>harmonizer.0 harmonizer>ultratap.0 ultratap>mixer1.1 mixer1>banddelay.0 banddelay>output.0" },
            { "Pitch Networks", "Dorian Tapestry (D)",
              { fx (harmonizer, -1, diatonic (D, dorian, 3, -4, 100)), wet (chorus, 1), wet (delay, 10), fx (reverb, 4, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 55, -60, 60) + lfo (1, 0.1f, sine),
              { { lfo1, "branches/mix2pana", 50 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>chorus.0 harmonizer>delay.0 chorus>mixer2.0 delay>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },

            //==================================================================
            // Réseaux de flanger : le « jet » dans des chemins parallèles, des splits et des boucles
            { "Flanger Networks", "Jet Stream Split",
              { wet (flanger, 0), wet (delay, 9), fx (chorus, 3), fx (reverb, 3, { { "mix", 20 } }) },
              mixer (1, 80, 60) + mixer (2, 55, 50, -60, 60) + lfo (1, 0.08f, sine),
              { { lfo1, "flanger/manual", 20 } },
              "input>amp.0 amp>chorus.0 amp>flanger.0 flanger>delay.0 flanger>mixer2.0 delay>mixer2.1 "
              "chorus>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Flanger Networks", "Through-Zero Feedback",
              { wet (flanger, 1), wet (delay, 4), fx (reverb, 6, { { "mix", 25 } }) },
              mixer (1, 100, 40) + mixer (2, 85, 55) + lfo (1, 0.1f, sine),
              { { lfo1, "branches/mix1levelb", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>flanger.0 flanger>delay.0 delay>mixer1.1 "
              "amp>mixer2.0 delay>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Flanger Networks", "Twin Combs",
              { wet (flanger, 5), wet (phaser, 6), wet (reverb, 4) },
              mixer (1, 55, 55, -70, 70) + mixer (2, 85, 60) + mixer (3, 85, 35) + lfo (1, 0.07f, triangle),
              { { lfo1, "branches/mix1pana", 40 }, { lfo1, "branches/mix1panb", -40 } },
              "input>amp.0 amp>flanger.0 amp>phaser.0 flanger>mixer1.0 phaser>mixer1.1 amp>mixer2.0 mixer1>mixer2.1 "
              "mixer2>mixer3.0 mixer2>reverb.0 reverb>mixer3.1 mixer3>output.0" },
            { "Flanger Networks", "Harmonic Jet (E Minor)",
              { fx (harmonizer, -1, diatonic (E, naturalMinor, 2, 4, 100)), wet (flanger, 0), wet (delay, 9), fx (reverb, 0, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 50, -60, 60),
              { { pitch, "flanger/manual", -25 } },
              "input>amp.0 amp>harmonizer.0 harmonizer>flanger.0 harmonizer>delay.0 flanger>mixer2.0 delay>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Flanger Networks", "Reverse Jet Loop",
              { wet (reverse, 0), wet (flanger, 1), fx (reverb, 3, { { "mix", 20 } }) },
              mixer (1, 100, 40) + mixer (2, 85, 55) + lfo (1, 0.09f, triangle),
              { { lfo1, "reverse/length", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>reverse.0 reverse>flanger.0 flanger>mixer1.1 "
              "amp>mixer2.0 flanger>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Flanger Networks", "Envelope Flange Funk",
              { fx (compressor, 2), fx (filter, 1), wet (flanger, 3), wet (delay, 2) },
              mixer (1, 75, 60, -30, 40) + mixer (2, 85, 35) + env (5, 150),
              { { envelope, "flanger/manual", 30 } },
              "input>compressor.0 compressor>amp.0 amp>filter.0 filter>mixer1.0 filter>flanger.0 flanger>mixer1.1 "
              "mixer1>delay.0 mixer1>mixer2.0 delay>mixer2.1 mixer2>output.0" },

            //==================================================================
            // Réseaux de whammy : la note transposée dans ses propres chemins, splits et boucles
            { "Whammy Networks", "Octave Split",
              { wet (whammy, 2, { { "pedal", 100 } }), wet (delay, 9), fx (reverb, 1, { { "mix", 15 } }) },
              mixer (1, 60, 80, -40, 20) + mixer (2, 85, 40) + lfo (1, 0.1f, sine),
              { { lfo1, "branches/mix1pana", 40 } },
              "input>amp.0 amp>whammy.0 amp>mixer1.1 whammy>mixer1.0 whammy>delay.0 delay>mixer2.1 mixer1>mixer2.0 "
              "mixer2>reverb.0 reverb>output.0" },
            { "Whammy Networks", "Harmony Tree (5th to Octave)",
              { wet (whammy, 7), wet (chorus, 2), wet (delay, 9), fx (reverb, 4, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 50, -60, 60) + env (10, 300),
              { { envelope, "whammy/pedal", 70 } },
              "input>amp.0 amp>whammy.0 whammy>chorus.0 whammy>delay.0 chorus>mixer2.0 delay>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Whammy Networks", "Octave Climb Loop",
              { wet (delay, 4), wet (whammy, 8), fx (reverb, 6, { { "mix", 25 } }) },
              mixer (1, 100, 35) + mixer (2, 85, 50) + lfo (1, 0.08f, sine),
              { { lfo1, "branches/mix1levelb", 15 } },
              "input>amp.0 amp>mixer1.0 mixer1>delay.0 delay>whammy.0 whammy>mixer1.1 "
              "amp>mixer2.0 delay>mixer2.1 mixer2>reverb.0 reverb>output.0" },
            { "Whammy Networks", "Detune Wide Wall",
              { wet (whammy, 11), wet (chorus, 1), wet (reverb, 6) },
              mixer (1, 55, 55, -70, 70) + mixer (2, 85, 60) + mixer (3, 85, 35) + lfo (1, 0.07f, triangle),
              { { lfo1, "whammy/detune", 20 } },
              "input>amp.0 amp>whammy.0 amp>chorus.0 whammy>mixer1.0 chorus>mixer1.1 amp>mixer2.0 mixer1>mixer2.1 "
              "mixer2>mixer3.0 mixer2>reverb.0 reverb>mixer3.1 mixer3>output.0" },
            { "Whammy Networks", "Dive & Reverse",
              { wet (whammy, 3), wet (reverse, 0), wet (delay, 5), fx (reverb, 3, { { "mix", 20 } }) },
              mixer (1, 85, 55) + mixer (2, 55, 50, -60, 60) + lfoSync (1, bars2, sawDown),
              { { lfo1, "whammy/pedal", 100 } },
              "input>amp.0 amp>whammy.0 whammy>reverse.0 whammy>delay.0 reverse>mixer2.0 delay>mixer2.1 "
              "amp>mixer1.0 mixer2>mixer1.1 mixer1>reverb.0 reverb>output.0" },
            { "Whammy Networks", "Octave Trill Duet",
              { wet (whammy, 0, { { "glide", 5 } }), wet (phaser, 6), fx (delay, 9, { { "mix", 25 } }) },
              mixer (1, 55, 55, -60, 60) + mixer (2, 85, 55) + lfoSync (1, l8, square),
              { { lfo1, "whammy/pedal", 100 } },
              "input>amp.0 amp>whammy.0 amp>phaser.0 whammy>mixer1.0 phaser>mixer1.1 amp>mixer2.0 mixer1>mixer2.1 "
              "mixer2>delay.0 delay>output.0" },
        };
        return list;
    }
