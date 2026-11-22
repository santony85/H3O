#include <algorithm>
// Tests du routage de H3O Chain : ordre libre, branches parallèles A / B, état des sessions.
//   ./ChainTest        Cible Xcode : ChainTest
#include "ChainProcessor.h"
#include "FactoryPresets.h"

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "OK   " : "ECHEC", what);
        if (! ok)
            ++failures;
    }

    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 150;   // 1,6 s

    // Guitare imitée : une note de 220 Hz avec harmoniques, attaque et extinction, un peu différente à droite
    float source (int channel, int i)
    {
        const double t = i / rate;
        const double env = std::exp (-3.0 * std::fmod (t, 0.8)) * std::min (1.0, std::fmod (t, 0.8) * 200.0);
        double x = 0.0;
        for (int h = 1; h <= 4; ++h)
            x += std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * h * t + channel * 0.3) / h;
        return (float) (0.3 * env * x);
    }

    using Signal = std::array<std::vector<float>, 2>;

    // Fait passer le signal de test dans un processeur, bloc par bloc
    Signal run (juce::AudioProcessor& p, int blocks = numBlocks, bool silenceAfterFirstSecond = false)
    {
        p.setRateAndBufferSizeDetails (rate, blockSize);
        p.prepareToPlay (rate, blockSize);

        Signal out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const int pos = b * blockSize + i;
                    buffer.setSample (ch, i, silenceAfterFirstSecond && pos > (int) rate ? 0.0f : source (ch, pos));
                }

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
        }
        return out;
    }

    // Les deux signaux passent l'un après l'autre dans les effets donnés (référence calculée hors de la chaîne)
    Signal runSeries (std::initializer_list<juce::AudioProcessor*> list)
    {
        for (auto* p : list)
        {
            p->setRateAndBufferSizeDetails (rate, blockSize);
            p->prepareToPlay (rate, blockSize);
        }

        Signal out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, source (ch, b * blockSize + i));

            for (auto* p : list)
                p->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
        }
        return out;
    }

    Signal input()
    {
        Signal s;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numBlocks * blockSize; ++i)
                s[(size_t) ch].push_back (source (ch, i));
        return s;
    }

    float maxDifference (const Signal& a, const Signal& b, size_t from = 0)
    {
        float d = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = from; i < a[(size_t) ch].size(); ++i)
                d = std::max (d, std::abs (a[(size_t) ch][i] - b[(size_t) ch][i]));
        return d;
    }

    float peak (const Signal& s, int channel, size_t from = 0)
    {
        float p = 0.0f;
        for (size_t i = from; i < s[(size_t) channel].size(); ++i)
            p = std::max (p, std::abs (s[(size_t) channel][i]));
        return p;
    }

    void setParam (ChainProcessor& chain, const char* id, float value)
    {
        auto* param = chain.parameters.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Seuls les effets donnés sont allumés
    void onlyOn (ChainProcessor& chain, std::initializer_list<int> ids)
    {
        for (int e = 0; e < ChainRouting::numEffects; ++e)
            setParam (chain, ChainProcessor::onParamId (e), 0.0f);
        for (auto e : ids)
            setParam (chain, ChainProcessor::onParamId (e), 1.0f);
    }

    void routingModel()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Modèle de routage ---\n");

        const auto series = Routing::series();
        check (! series.hasParallel(), "par défaut : tout en série");
        check (Routing::unpack (series.pack()) == series, "série : empaqueté puis dépaqueté à l'identique");
        check (Routing::fromString (series.toString()) == series, "série : texte de session relu à l'identique");

        for (auto& t : templates())
        {
            check (Routing::unpack (t.routing.pack()) == t.routing && Routing::fromString (t.routing.toString()) == t.routing,
                   (std::string ("modèle ") + t.name + " : empaqueté et relu à l'identique").c_str());
        }

        auto r = series;
        r.move (delay, branchB, 0);
        r.move (harmonizer, branchA, 0);
        r.move (reverb, post, 0);
        check (r.hasParallel() && r.effectsIn (branchA) == std::vector<int> { harmonizer }
               && r.effectsIn (branchB) == std::vector<int> { delay } && r.effectsIn (post) == std::vector<int> { reverb },
               "déplacements vers A, B et Après");
        check ((int) r.effectsIn (pre).size() == numEffects - 3, "les autres restent avant la séparation");

        r.move (amp, pre, 0);
        r.move (filter, pre, 99);
        check (r.effectsIn (pre).front() == amp && r.effectsIn (pre).back() == filter, "ordre libre : Amp avant, Filter en dernier");
        check (Routing::unpack (r.pack()) == r && Routing::fromString (r.toString()) == r, "routage modifié : relu à l'identique");

        check (Routing::fromString ("") == series, "session sans routage : tout en série");
        check (Routing::unpack (0) == series, "valeur invalide : tout en série");

        // Session d'une version future avec un effet inconnu, et sans la réverbe : rien ne se perd
        const auto partial = Routing::fromString ("delay:a,unknown:b,amp:pre");
        check (partial.laneOf (delay) == branchA && partial.laneOf (amp) == pre && partial.laneOf (reverb) == pre
               && partial.effectsIn (pre)[1] == amp,
               "session incomplète : les effets manquants se rangent en série");

        // Session d'avant le compresseur : il arrive en tête de chaîne, le reste ne bouge pas
        const auto old = Routing::fromString ("filter:pre,amp:pre,harmonizer:a,reverse:pre,layered:pre,chorus:pre,"
                                              "delay:b,banddelay:pre,ultratap:pre,reverb:post");
        check (old.effectsIn (pre).front() == compressor && old.effectsIn (pre)[1] == filter && old.laneOf (harmonizer) == branchA
               && old.laneOf (delay) == branchB && old.laneOf (reverb) == post && Routing::unpack (old.pack()) == old,
               "session d'avant le compresseur : compresseur en tête, routage intact");
    }

    void seriesOrder()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Ordre des effets en série ---\n");
        Signal forward, backward;

        // La chaîne par défaut (Harmonizer puis Delay allumés) = les deux effets seuls, l'un après l'autre
        {
            ChainProcessor chain;
            HarmonizerProcessor harmonizerAlone;
            DelayProcessor delayAlone;
            const auto expected = runSeries ({ &harmonizerAlone, &delayAlone });
            const auto got = run (chain);
            const float d = maxDifference (got, expected);
            std::printf ("        écart max %.2e\n", d);
            check (d < 1.0e-6f, "par défaut : Harmonizer puis Delay, comme avant");
            forward = got;
        }

        // Ordre inversé : Delay puis Harmonizer
        {
            ChainProcessor chain;
            auto r = chain.getRouting();
            r.move (delay, pre, r.indexInLane (harmonizer));
            chain.setRouting (r);

            HarmonizerProcessor harmonizerAlone;
            DelayProcessor delayAlone;
            const auto expected = runSeries ({ &delayAlone, &harmonizerAlone });
            const auto got = run (chain);
            const float d = maxDifference (got, expected);
            std::printf ("        écart max %.2e\n", d);
            check (d < 1.0e-6f, "Delay déplacé avant l'Harmonizer : le son suit l'ordre");
            backward = got;
        }

        std::printf ("        écart entre les deux ordres %.3f\n", maxDifference (forward, backward));
        check (maxDifference (forward, backward) > 1.0e-3f, "les deux ordres sonnent différemment");

        // Tout dans « Après » sans branche : même son qu'en « Avant »
        {
            ChainProcessor a, b;
            auto r = Routing::series();
            for (int i = 0; i < numEffects; ++i)
                r.move (r.order[0], post, 99);
            b.setRouting (r);
            check (maxDifference (run (a), run (b)) < 1.0e-9f, "tout après la fusion, sans branche : identique");
        }
    }

    void parallel()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Branches parallèles ---\n");
        const auto dry = input();

        // Deux branches vides : le son ressort intact, quels que soient Balance et Spread
        {
            ChainProcessor chain;
            onlyOn (chain, { delay });
            auto r = Routing::series();
            r.move (delay, branchB, 0);
            chain.setRouting (r);
            setParam (chain, "delayon", 0.0f);
            setParam (chain, "branchbalance", 30.0f);
            check (maxDifference (run (chain), dry) < 1.0e-6f, "A et B sans effet allumé : son intact");
        }

        // Delay dans B, A vide : moitié son sec, moitié delay
        {
            ChainProcessor chain;
            onlyOn (chain, { delay });
            auto r = Routing::series();
            r.move (delay, branchB, 0);
            chain.setRouting (r);

            DelayProcessor delayAlone;
            const auto wet = runSeries ({ &delayAlone });
            const auto got = run (chain);

            Signal expected;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < dry[0].size(); ++i)
                    expected[(size_t) ch].push_back (0.5f * dry[(size_t) ch][i] + 0.5f * wet[(size_t) ch][i]);

            const float d = maxDifference (got, expected);
            std::printf ("        écart max %.2e\n", d);
            check (d < 1.0e-5f, "Balance 50 % : moitié A (sec), moitié B (delay)");

            // Balance à 100 % : seulement B
            ChainProcessor chain2;
            onlyOn (chain2, { delay });
            chain2.setRouting (r);
            setParam (chain2, "branchbalance", 100.0f);
            check (maxDifference (run (chain2), wet) < 1.0e-5f, "Balance 100 % : seulement la branche B");

            // Spread 100 % : A à gauche, B à droite
            ChainProcessor chain3;
            onlyOn (chain3, { delay });
            chain3.setRouting (r);
            setParam (chain3, "branchspread", 100.0f);
            const auto spread = run (chain3);

            float leftError = 0.0f, rightError = 0.0f;
            for (size_t i = 0; i < dry[0].size(); ++i)
            {
                leftError  = std::max (leftError,  std::abs (spread[0][i] - dry[0][i]));
                rightError = std::max (rightError, std::abs (spread[1][i] - wet[1][i]));
            }
            std::printf ("        écarts %.2e / %.2e\n", leftError, rightError);
            check (leftError < 1.0e-5f && rightError < 1.0e-5f, "Spread 100 % : A (sec) à gauche, B (delay) à droite");
        }

        // Retour croisé au maximum, avec delay et réverbe : reste stable une fois le son coupé
        {
            ChainProcessor chain;
            onlyOn (chain, { delay, reverb, ultraTap });
            auto r = Routing::series();
            r.move (delay, branchA, 0);
            r.move (ultraTap, branchA, 1);
            r.move (reverb, branchB, 0);
            chain.setRouting (r);
            setParam (chain, "crossfeedback", 90.0f);

            const auto out = run (chain, (int) (10.0 * rate / blockSize), true);   // 1 s de son, puis 9 s de silence

            bool finite = true;
            for (auto& ch : out)
                for (auto x : ch)
                    finite = finite && std::isfinite (x);

            const float during = std::max (peak (out, 0), peak (out, 1));
            const float end = std::max (peak (out, 0, out[0].size() - (size_t) rate), peak (out, 1, out[1].size() - (size_t) rate));
            std::printf ("        crête %.3f, dernière seconde %.4f\n", during, end);
            check (finite && during < 4.0f, "retour croisé 90 % : pas d'emballement");
            check (end < during * 0.5f, "retour croisé 90 % : le son finit par s'éteindre");

            // Sans retour croisé, le même routage ne garde rien de B dans A
            ChainProcessor plain;
            onlyOn (plain, { delay, reverb, ultraTap });
            plain.setRouting (r);
            check (maxDifference (run (plain), run (chain)) > 1.0e-3f, "le retour croisé change bien le son");
        }
    }

    void modulationSources()
    {
        std::printf ("\n--- Sources de modulation ---\n");
        ModulationSources src;
        src.prepare (rate);

        ModulationSources::Settings settings;
        settings.lfoRate[0] = 1.0f;
        settings.lfoShape[0] = ModulationSources::sine;
        settings.lfoRate[1] = 1.0f;
        settings.lfoShape[1] = ModulationSources::square;
        src.setSettings (settings);

        // 220 Hz à −6 dB pendant 0,25 s : quart de période du LFO à 1 Hz
        std::vector<float> note ((size_t) (rate * 0.25));
        for (size_t i = 0; i < note.size(); ++i)
            note[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / rate);

        for (size_t i = 0; i < note.size(); i += 64)
            src.process (note.data() + i, (int) std::min<size_t> (64, note.size() - i));

        std::printf ("        LFO 1 %.3f, LFO 2 %.3f, Envelope %.3f, Pitch %.3f (attendu %.3f)\n",
                     src.get (ModulationSources::lfo1), src.get (ModulationSources::lfo2),
                     src.get (ModulationSources::envelope), src.get (ModulationSources::pitch), (57.0f - 40.0f) / 48.0f);
        check (std::abs (src.get (ModulationSources::lfo1) - 1.0f) < 0.01f, "LFO sinus à 1 Hz : sommet au quart de période");
        check (src.get (ModulationSources::lfo2) == 1.0f, "LFO carré : +1 sur la première moitié");
        check (std::abs (src.get (ModulationSources::envelope) - (48.0f - 6.0f) / 48.0f) < 0.03f, "Envelope : −6 dB → 0,875");
        check (std::abs (src.get (ModulationSources::pitch) - (57.0f - 40.0f) / 48.0f) < 0.01f, "Pitch : la note jouée (A3)");

        // Silence : l'enveloppe retombe, la note est gardée
        std::vector<float> silence ((size_t) rate, 0.0f);
        for (size_t i = 0; i < silence.size(); i += 64)
            src.process (silence.data() + i, 64);
        check (src.get (ModulationSources::envelope) < 0.01f, "Envelope : retombe à 0 dans le silence");
        check (std::abs (src.get (ModulationSources::pitch) - (57.0f - 40.0f) / 48.0f) < 0.01f, "Pitch : garde la dernière note");

        // Random : reste entre −1 et +1, et bouge
        float lo = 1.0f, hi = -1.0f;
        for (int i = 0; i < 2000; ++i)
        {
            src.process (silence.data(), 64);
            lo = std::min (lo, src.get (ModulationSources::random));
            hi = std::max (hi, src.get (ModulationSources::random));
        }
        std::printf ("        Random entre %.2f et %.2f\n", lo, hi);
        check (lo >= -1.0f && hi <= 1.0f && hi - lo > 0.8f, "Random : bouge, entre −1 et +1");
    }

    void modulationMatrix()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Matrice de modulation ---\n");
        const auto dry = input();

        {
            ChainProcessor chain;
            check (chain.findDestination ("delay/time") >= 0 && chain.findDestination ("reverb/decay") >= 0
                   && chain.findDestination ("branches/branchspread") >= 0,
                   "destinations : réglages des effets et des branches");
            check (chain.findDestination ("delay/sync") < 0 && chain.findDestination ("reverb/type") < 0,
                   "pas de menus ni d'interrupteurs");
            std::printf ("        %d destinations\n", (int) chain.getDestinations().size());
        }

        // LFO carré (+1 au début) → Mix du Delay −100 % : le delay ne laisse passer que le son sec
        {
            ChainProcessor chain;
            onlyOn (chain, { delay });
            setParam (chain, "lfo1rate", 0.02f);
            setParam (chain, "lfo1shape", (float) ModulationSources::square);
            setParam (chain, "mod1source", (float) ModulationSources::lfo1);
            setParam (chain, "mod1amount", -100.0f);
            chain.setSlotDestination (0, chain.findDestination ("delay/mix"));

            const float baseMix = chain.delay.parameters.getRawParameterValue ("mix")->load();
            const auto got = run (chain);

            float error = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = (size_t) (0.2 * rate); i < dry[0].size(); ++i)
                    error = std::max (error, std::abs (got[(size_t) ch][i] - dry[(size_t) ch][i]));

            std::printf ("        écart avec le son sec %.2e (Mix réglé à %.0f %%)\n", error, baseMix);
            check (error < 1.0e-4f, "LFO 1 → Delay Mix −100 % : le delay est coupé");
            check (chain.delay.parameters.getRawParameterValue ("mix")->load() == baseMix, "le réglage du Mix n'est pas touché");

            juce::MemoryBlock state;
            chain.delay.getStateInformation (state);
            DelayProcessor copy;
            copy.setStateInformation (state.getData(), (int) state.getSize());
            check (copy.parameters.getRawParameterValue ("mix")->load() == baseMix, "la session garde le réglage, pas la valeur modulée");

            // La ligne enlevée : le delay revient
            chain.setSlotDestination (0, -1);
            const auto after = run (chain);
            check (maxDifference (after, dry) > 1.0e-2f, "ligne enlevée : le réglage d'origine revient");
        }

        // LFO carré → Spread des branches +100 % : A (sec) à gauche
        {
            ChainProcessor chain;
            onlyOn (chain, { delay });
            auto r = Routing::series();
            r.move (delay, branchB, 0);
            chain.setRouting (r);
            setParam (chain, "lfo1rate", 0.02f);
            setParam (chain, "lfo1shape", (float) ModulationSources::square);
            setParam (chain, "mod1source", (float) ModulationSources::lfo1);
            setParam (chain, "mod1amount", 100.0f);
            chain.setSlotDestination (0, chain.findDestination ("branches/branchspread"));

            const auto got = run (chain);
            float error = 0.0f;
            for (size_t i = (size_t) (0.2 * rate); i < dry[0].size(); ++i)
                error = std::max (error, std::abs (got[0][i] - dry[0][i]));
            check (error < 1.0e-4f, "LFO 1 → Spread +100 % : branche A seule à gauche");
        }

        // Sessions : destinations enregistrées par leur nom
        {
            ChainProcessor chain;
            setParam (chain, "mod3source", (float) ModulationSources::envelope);
            chain.setSlotDestination (2, chain.findDestination ("reverb/size"));

            juce::MemoryBlock state;
            chain.getStateInformation (state);
            ChainProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            check (restored.getSlotDestination (2) == chain.findDestination ("reverb/size") && restored.isSlotActive (2),
                   "ligne 3 (Envelope → Reverb Size) enregistrée puis relue");

            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            xml->removeAttribute ("mod3dest");
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*xml, oldState);
            ChainProcessor old;
            old.setStateInformation (oldState.getData(), (int) oldState.getSize());
            check (old.getSlotDestination (2) < 0, "ancienne session : aucune modulation");
        }
    }

    // Imite Logic : tempo, signature, lecture et position dans le morceau
    struct TestPlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;
        int numerator = 4;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            info.setTimeSignature (TimeSignature { numerator, 4 });
            return info;
        }
    };

    void tempoSync()
    {
        std::printf ("\n--- LFO synchronisés au tempo ---\n");

        check (ModulationSources::divisionQuarters (8) == 1.0 && ModulationSources::divisionQuarters (3) == 4.0
               && ModulationSources::divisionQuarters (3, 3.0) == 3.0 && std::abs (ModulationSources::divisionQuarters (9) - 2.0 / 3.0) < 1e-9,
               "divisions : 1/4 = une noire, 1 Bar = 4 noires (3 en 3/4), triolet");

        const double secondsPerBlock = blockSize / rate;

        // Morceau en lecture, 120 BPM, LFO 1 en dent de scie sur une noire : sa phase suit la position
        {
            ChainProcessor chain;
            TestPlayHead head;
            head.ppq = 7.3;   // lecture lancée au milieu d'un temps
            chain.setPlayHead (&head);
            setParam (chain, "lfo1sync", 1.0f);
            setParam (chain, "lfo1division", 8.0f);   // 1/4
            setParam (chain, "lfo1shape", (float) ModulationSources::sawUp);
            setParam (chain, "mod1source", (float) ModulationSources::lfo1);
            chain.setSlotDestination (0, chain.findDestination ("delay/mix"));

            chain.setRateAndBufferSizeDetails (rate, blockSize);
            chain.prepareToPlay (rate, blockSize);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;

            float worst = 0.0f;
            for (int b = 0; b < 100; ++b)
            {
                buffer.clear();
                chain.processBlock (buffer, midi);
                head.ppq += secondsPerBlock * head.bpm / 60.0;   // position à la fin du bloc = début du suivant

                const float expected = ModulationSources::shapeAt (ModulationSources::sawUp, head.ppq - std::floor (head.ppq));
                float error = std::abs (chain.getSourceValue (ModulationSources::lfo1) - expected);
                error = std::min (error, 2.0f - error);   // au passage de +1 à −1, les deux sont justes
                worst = std::max (worst, error);
            }
            std::printf ("        écart max avec la position du morceau %.2e\n", worst);
            check (worst < 1.0e-3f, "en lecture : le LFO suit la position du morceau (1/4 à 120 BPM)");

            // Saut dans le morceau (boucle) : le LFO repart au bon endroit
            head.ppq = 32.25;
            chain.processBlock (buffer, midi);
            head.ppq += secondsPerBlock * 2.0;
            const float expected = ModulationSources::shapeAt (ModulationSources::sawUp, head.ppq - std::floor (head.ppq));
            check (std::abs (chain.getSourceValue (ModulationSources::lfo1) - expected) < 1.0e-3f, "retour en début de boucle : le LFO repart calé");
            chain.setPlayHead (nullptr);
        }

        // Morceau arrêté, 90 BPM, 1 Bar en 3/4 : tourne seul à 90 / 60 / 3 = 0,5 Hz
        {
            ChainProcessor chain;
            TestPlayHead head;
            head.bpm = 90.0;
            head.playing = false;
            head.numerator = 3;
            chain.setPlayHead (&head);
            setParam (chain, "lfo2sync", 1.0f);
            setParam (chain, "lfo2division", 3.0f);   // 1 Bar
            setParam (chain, "lfo2shape", (float) ModulationSources::sawUp);

            chain.setRateAndBufferSizeDetails (rate, blockSize);
            chain.prepareToPlay (rate, blockSize);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;

            const int blocks = 150;
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                chain.processBlock (buffer, midi);
            }
            const double cycles = 0.5 * blocks * secondsPerBlock;
            const float expected = ModulationSources::shapeAt (ModulationSources::sawUp, cycles - std::floor (cycles));
            std::printf ("        LFO 2 %.4f, attendu %.4f (tempo lu %.0f BPM)\n", chain.getSourceValue (ModulationSources::lfo2), expected, chain.getTempo());
            check (std::abs (chain.getSourceValue (ModulationSources::lfo2) - expected) < 1.0e-3f && chain.getTempo() == 90.0f,
                   "arrêté : tourne seul au tempo (1 Bar en 3/4 à 90 BPM = 0,5 Hz)");
            chain.setPlayHead (nullptr);
        }

        // Sans hôte (application autonome) : 120 BPM
        {
            ChainProcessor chain;
            check (chain.getTempo() == 120.0f, "sans hôte : 120 BPM");
        }
    }

    void steppedModulation()
    {
        std::printf ("\n--- Modulation des réglages par crans ---\n");

        ChainProcessor chain;
        juce::StringArray stepped;
        for (auto& d : chain.getDestinations())
            if (d.numSteps > 0)
                stepped.add (d.key + " (" + juce::String (d.numSteps) + ")");
        std::printf ("        %s\n", stepped.joinIntoString (", ").toRawUTF8());

        const int semi = chain.findDestination ("harmonizer/asemi");
        check (semi >= 0 && chain.getDestinations()[(size_t) semi].numSteps > 0
               && chain.findDestination ("harmonizer/adegrees") >= 0 && chain.findDestination ("ultratap/taps") >= 0,
               "demi-tons, intervalles et nombres de taps sont des destinations");

        // LFO carré (+1) → Harmonizer voix A +7 demi-tons : la quinte au-dessus du réglage
        auto* target = chain.harmonizer.modulation.find ("asemi");
        const int steps = chain.getDestinations()[(size_t) semi].numSteps;
        const float base = target->base->load();

        setParam (chain, "lfo1rate", 0.02f);
        setParam (chain, "lfo1shape", (float) ModulationSources::square);
        setParam (chain, "mod1source", (float) ModulationSources::lfo1);
        setParam (chain, "mod1amount", 100.0f * 7.0f / (float) steps);
        chain.setSlotDestination (0, semi);
        run (chain, 10);
        std::printf ("        réglage %+.0f, valeur jouée %+.3f\n", base, target->value.load());
        check (target->value.load() == base + 7.0f, "LFO 1 → +7 demi-tons : exactement une quinte au-dessus");
        check (target->base->load() == base, "le réglage des demi-tons n'est pas touché");

        // LFO sinus lent sur ±12 demi-tons : la valeur jouée est toujours un demi-ton entier, et parcourt la plage
        ChainProcessor sweep;
        setParam (sweep, "lfo1rate", 1.0f);
        setParam (sweep, "lfo1shape", (float) ModulationSources::sine);
        setParam (sweep, "mod1source", (float) ModulationSources::lfo1);
        setParam (sweep, "mod1amount", 100.0f * 12.0f / (float) steps);
        sweep.setSlotDestination (0, semi);

        auto* t = sweep.harmonizer.modulation.find ("asemi");
        sweep.setRateAndBufferSizeDetails (rate, blockSize);
        sweep.prepareToPlay (rate, blockSize);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        bool whole = true;
        float lo = 100.0f, hi = -100.0f;
        int changes = 0;
        float last = t->value.load();
        for (int b = 0; b < (int) (rate / blockSize); ++b)   // une seconde = un cycle
        {
            buffer.clear();
            sweep.processBlock (buffer, midi);
            const float v = t->value.load();
            whole = whole && v == std::round (v);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
            changes += v != last ? 1 : 0;
            last = v;
        }
        std::printf ("        de %+.0f à %+.0f demi-tons, %d changements\n", lo, hi, changes);
        check (whole, "LFO sinus : toujours un demi-ton entier (par crans)");
        check (lo <= base - 11.0f && hi >= base + 11.0f, "LFO sinus ±12 : parcourt la plage d'une octave en dessous à une au-dessus");

        // Destination par crans enregistrée par son nom
        juce::MemoryBlock state;
        chain.getStateInformation (state);
        ChainProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getSlotDestination (0) == semi, "destination Harmonizer · demi-tons enregistrée puis relue");
    }

    void wetDryWet()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Wet/Dry/Wet ---\n");
        const auto dry = input();

        const Routing* wdw = nullptr;
        for (auto& t : templates())
            if (std::string (t.name) == "Wet/Dry/Wet")
                wdw = &t.routing;
        check (wdw != nullptr && wdw->effectsIn (branchA).empty() && wdw->laneOf (amp) == pre && wdw->laneOf (reverb) == branchB,
               "modèle Wet/Dry/Wet : ampli avant, A sec, effets dans B (réverbe comprise)");

        // Un effet à 100 % à côté du son sec, tous deux à plein volume (Balance 50 %)
        auto wetOf = [] (std::initializer_list<int> ids)
        {
            Signal sum;
            for (auto id : ids)
            {
                std::unique_ptr<juce::AudioProcessor> fx;
                if (id == delay)  fx = std::make_unique<DelayProcessor>();
                if (id == reverb) fx = std::make_unique<ReverbProcessor>();
                auto* apvts = id == delay ? &static_cast<DelayProcessor&> (*fx).parameters : &static_cast<ReverbProcessor&> (*fx).parameters;
                auto* mix = apvts->getParameter ("mix");
                mix->setValueNotifyingHost (1.0f);
                const auto out = runSeries ({ fx.get() });
                for (int ch = 0; ch < 2; ++ch)
                {
                    sum[(size_t) ch].resize (out[(size_t) ch].size(), 0.0f);
                    for (size_t i = 0; i < out[(size_t) ch].size(); ++i)
                        sum[(size_t) ch][i] += out[(size_t) ch][i];
                }
            }
            return sum;
        };

        auto plusDry = [&dry] (const Signal& wet)
        {
            Signal s;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < dry[0].size(); ++i)
                    s[(size_t) ch].push_back (dry[(size_t) ch][i] + wet[(size_t) ch][i]);
            return s;
        };

        auto makeChain = [wdw] (ChainProcessor& chain, std::initializer_list<int> on)
        {
            onlyOn (chain, on);
            chain.setRouting (*wdw);
            setParam (chain, "wetonly", 1.0f);
        };

        {
            ChainProcessor chain;
            makeChain (chain, { delay });
            const float baseMix = chain.delay.parameters.getRawParameterValue ("mix")->load();
            const auto got = run (chain);
            const auto expected = plusDry (wetOf ({ delay }));
            std::printf ("        écart max %.2e dès le début, %.2e après 0,1 s\n", maxDifference (got, expected), maxDifference (got, expected, (size_t) (0.1 * rate)));
            const float d = maxDifference (got, expected, (size_t) (0.1 * rate));
            check (d < 1.0e-4f, "Wet Only : son sec + delay 100 % effet, chacun à plein volume");
            check (chain.delay.parameters.getRawParameterValue ("mix")->load() == baseMix, "le Mix du Delay garde ton réglage");
        }

        {
            ChainProcessor chain;
            makeChain (chain, { delay, reverb });
            const float d = maxDifference (run (chain), plusDry (wetOf ({ delay, reverb })), (size_t) (0.1 * rate));   // après le glissement du Mix
            std::printf ("        écart max %.2e\n", d);
            check (d < 1.0e-4f, "Wet Only : delay et réverbe côte à côte (la réverbe ne reçoit pas que les échos)");
        }

        // Balance 0 % : le son sec seul
        {
            ChainProcessor chain;
            makeChain (chain, { delay, reverb });
            setParam (chain, "branchbalance", 0.0f);
            check (maxDifference (run (chain), dry) < 1.0e-5f, "Balance 0 % : le son sec seul");
        }

        // Width 0 % : les effets de B en mono (gauche = droite)
        {
            ChainProcessor chain;
            makeChain (chain, { reverb });
            setParam (chain, "branchbalance", 100.0f);
            setParam (chain, "branchwidth", 0.0f);
            const auto got = run (chain);
            float side = 0.0f, level = 0.0f;
            for (size_t i = (size_t) (0.1 * rate); i < got[0].size(); ++i)
            {
                side  = std::max (side, std::abs (got[0][i] - got[1][i]));
                level = std::max (level, std::abs (got[0][i]));
            }
            std::printf ("        écart gauche/droite %.2e (niveau %.3f)\n", side, level);
            check (side < 1.0e-5f && level > 1.0e-3f, "Width 0 % : effets en mono");

            ChainProcessor wide;
            makeChain (wide, { reverb });
            setParam (wide, "branchbalance", 100.0f);
            setParam (wide, "branchwidth", 200.0f);
            ChainProcessor normal;
            makeChain (normal, { reverb });
            setParam (normal, "branchbalance", 100.0f);

            auto sideLevel = [] (const Signal& s)
            {
                double e = 0.0;
                for (size_t i = 0; i < s[0].size(); ++i)
                    e += 0.25 * (s[0][i] - s[1][i]) * (s[0][i] - s[1][i]);
                return e;
            };
            const double ratio = sideLevel (run (wide)) / sideLevel (run (normal));
            std::printf ("        énergie stéréo ×%.2f\n", ratio);
            check (std::abs (ratio - 4.0) < 0.05, "Width 200 % : la stéréo des effets doublée");
        }
    }

    // Les 100 presets d'usine « H3000 Inspired » : rangés par catégorie, sans Amp, et à un niveau raisonnable
    void factoryPresets()
    {
        std::printf ("\n--- Presets d'usine ---\n");
        int h3000 = 0;
        for (int i = 0; i < FactoryPresets::count(); ++i)
            h3000 += FactoryPresets::bank (i) == FactoryPresets::folderName ? 1 : 0;
        check (h3000 == 100 && FactoryPresets::count() == 200, "100 presets H3000 Inspired + 100 Multi-FX");

        juce::StringArray paths;
        for (int i = 0; i < FactoryPresets::count(); ++i)
            paths.add (FactoryPresets::bank (i) + "/" + FactoryPresets::category (i) + "/" + FactoryPresets::name (i));
        auto unique = paths;
        unique.removeDuplicates (false);
        check (unique.size() == paths.size(), "noms tous différents");

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChainTestFactory");
        folder.deleteRecursively();
        const auto error = FactoryPresets::writeAll (folder);
        if (error.isNotEmpty())
            std::printf ("  %s\n", error.toRawUTF8());
        check (error.isEmpty(), "toutes les recettes s'appliquent et s'écrivent");

        ChainProcessor chain;
        chain.presets.setFolder (folder);
        const auto files = chain.presets.list();
        check (files.size() == 200, "200 fichiers, sous-dossiers compris");

        // Ton Amp reste : allumé, avec son état (modèle, IR)
        setParam (chain, "ampon", 1.0f);
        setParam (chain, "input", 2.0f);
        chain.amp.parameters.getParameter ("bass")->setValueNotifyingHost (0.8f);
        check (chain.presets.load (files[0]).isEmpty(), "charger un preset d'usine");
        check (chain.isAmpOn() && chain.amp.parameters.getParameter ("bass")->getValue() == 0.8f,
               "l'Amp n'est pas touché (allumé, réglages gardés)");
        check (chain.presetName.startsWith (juce::String (FactoryPresets::folderName) + "/") && chain.presets.current() == files[0],
               "le nom du preset est son chemin (catégorie comprise)");
        check (chain.presets.neighbour (1) == files[1] && chain.presets.neighbour (-1) == files.getLast(), "précédent / suivant à travers les catégories");
        check (chain.presets.saveTo (chain.presets.current()).isEmpty() && chain.presets.list().size() == 200,
               "Save réenregistre le preset dans son sous-dossier");
        check (chain.presets.saveTo (folder.getParentDirectory().getChildFile ("x.scpreset")).isNotEmpty(), "pas d'écriture hors du dossier");

        // Le niveau : une suite de notes pincées, ni saturation ni silence
        const double rate = 48000.0;
        const int block = 512, seconds = 4, total = (int) rate * seconds;
        juce::AudioBuffer<float> input (2, total);
        const float notes[] = { 82.4f, 98.0f, 110.0f, 146.8f, 164.8f, 196.0f, 220.0f, 329.6f };
        for (int i = 0; i < total; ++i)
        {
            const int note = i / (int) (rate * 0.5);
            const double t = (i % (int) (rate * 0.5)) / rate;
            const double phase = std::fmod (t * notes[note % 8], 1.0);
            const auto v = (float) (0.3 * std::exp (-t * 5.0) * (2.0 * phase - 1.0));
            input.setSample (0, i, v);
            input.setSample (1, i, v);
        }
        const float inputRms = input.getRMSLevel (0, 0, total);

        int parallel = 0, modulated = 0;
        for (auto& file : files)
        {
            ChainProcessor fx;
            fx.presets.setFolder (folder);
            fx.presets.load (file);
            parallel += fx.getRouting().hasParallel() ? 1 : 0;
            for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
                if (fx.getSlotDestination (slot) >= 0)
                {
                    ++modulated;
                    break;
                }
        }
        std::printf ("  %d presets en branches parallèles, %d avec modulation\n", parallel, modulated);
        check (parallel >= files.size() - 5, "presque tous les presets utilisent les branches A / B");

        int bad = 0;
        float lowest = 100.0f, highest = -100.0f;
        for (auto& file : files)
        {
            ChainProcessor fx;
            fx.presets.setFolder (folder);
            fx.presets.load (file);
            fx.prepareToPlay (rate, block);
            juce::AudioBuffer<float> out (input);
            juce::MidiBuffer midi;
            for (int start = 0; start < total; start += block)
            {
                juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, start, juce::jmin (block, total - start));
                fx.processBlock (part, midi);
            }

            bool finite = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < total; ++i)
                    finite = finite && std::isfinite (out.getSample (c, i));
            const float peak = out.getMagnitude (0, total);
            const float gain = juce::Decibels::gainToDecibels (0.5f * (out.getRMSLevel (0, 0, total) + out.getRMSLevel (1, 0, total)) / inputRms);
            lowest = juce::jmin (lowest, gain);
            highest = juce::jmax (highest, gain);
            if (! finite || peak > 1.5f || gain < -12.0f || gain > 6.0f)
            {
                ++bad;
                std::printf ("  hors limites : %-55s crête %.2f, niveau %+.1f dB\n", fx.presets.nameOf (file).toRawUTF8(), peak, gain);
            }
        }
        std::printf ("  niveaux de %+.1f à %+.1f dB par rapport au son sec\n", lowest, highest);
        check (bad == 0, "aucun preset ne sature ni ne s'éteint");
        folder.deleteRecursively();
    }

    // Le compresseur dans la chaîne : il comprime, et la session le retrouve
    void compressorInChain()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Compresseur dans la chaîne ---\n");

        ChainProcessor chain;
        for (int e = 0; e < numEffects; ++e)
            setParam (chain, ChainProcessor::onParamId (e), 0.0f);
        setParam (chain, "compressoron", 1.0f);
        check (chain.getRouting().effectsIn (pre).front() == compressor, "le compresseur est en tête de chaîne");

        chain.compressor.setCurrentProgram (1);   // Country Squash
        chain.compressor.parameters.getParameter ("automakeup")->setValueNotifyingHost (0.0f);
        chain.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        float lastPeak = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) (block * 512 + i) / 48000.0f);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            chain.processBlock (buffer, midi);
            lastPeak = buffer.getMagnitude (0, 512);
        }
        std::printf ("  réduction %.1f dB, crête de sortie %.2f (entrée 0.50)\n", chain.compressor.getGainReduction(), lastPeak);
        check (chain.compressor.getGainReduction() > 6.0f && lastPeak < 0.25f, "un son fort est comprimé");

        // La session garde ses réglages
        chain.compressor.parameters.getParameter ("ratio")->setValueNotifyingHost (
            chain.compressor.parameters.getParameter ("ratio")->convertTo0to1 (12.0f));
        juce::MemoryBlock session;
        chain.getStateInformation (session);
        ChainProcessor reopened;
        reopened.setStateInformation (session.getData(), (int) session.getSize());
        check (std::abs (reopened.compressor.parameters.getRawParameterValue ("ratio")->load() - 12.0f) < 0.05f
               && reopened.isEffectOn (compressor), "la session retrouve le compresseur et ses réglages");
    }

    // Le trémolo dans la chaîne : entre le chorus et le délai, il fait onduler le volume, et la session le retrouve
    void tremoloInChain()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Trémolo dans la chaîne ---\n");

        const auto s = Routing::series();
        const auto pre0 = s.effectsIn (pre);
        const auto at = [&pre0] (int e) { return std::find (pre0.begin(), pre0.end(), e) - pre0.begin(); };
        check (at (tremolo) == at (chorus) + 1 && at (delay) == at (tremolo) + 1, "en série : Chorus → Tremolo → Delay");

        const auto old = Routing::fromString ("compressor:pre,filter:pre,amp:pre,harmonizer:a,reverse:pre,layered:pre,chorus:pre,"
                                              "delay:b,banddelay:pre,ultratap:pre,reverb:post");
        check (old.laneOf (harmonizer) == branchA && old.laneOf (delay) == branchB && old.laneOf (reverb) == post
               && Routing::unpack (old.pack()) == old, "session d'avant le trémolo : routage intact");

        ChainProcessor chain;
        onlyOn (chain, { tremolo });
        chain.tremolo.setCurrentProgram (3);   // Choppy Square
        chain.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        float lo = 1.0f, hi = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.5f);
            chain.processBlock (buffer, midi);
            if (block >= 10)
                for (int i = 0; i < 512; ++i)
                {
                    lo = std::min (lo, buffer.getSample (0, i));
                    hi = std::max (hi, buffer.getSample (0, i));
                }
        }
        std::printf ("  volume de %.2f à %.2f (entrée 0.50)\n", lo, hi);
        check (lo < 0.1f && hi > 0.45f, "le volume ondule");

        chain.tremolo.parameters.getParameter ("depth")->setValueNotifyingHost (
            chain.tremolo.parameters.getParameter ("depth")->convertTo0to1 (42.0f));
        juce::MemoryBlock session;
        chain.getStateInformation (session);
        ChainProcessor reopened;
        reopened.setStateInformation (session.getData(), (int) session.getSize());
        check (std::abs (reopened.tremolo.parameters.getRawParameterValue ("depth")->load() - 42.0f) < 0.05f
               && reopened.isEffectOn (tremolo), "la session retrouve le trémolo et ses réglages");
    }

    void presets()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Presets ---\n");

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChainTestPresets");
        folder.deleteRecursively();

        // Un son complet : routage, réglages d'une pédale, modulation, branches
        ChainProcessor original;
        original.presets.setFolder (folder);
        for (auto& t : templates())
            if (std::string (t.name) == "Wet/Dry/Wet")
                original.setRouting (t.routing);
        setParam (original, "wetonly", 1.0f);
        setParam (original, "branchwidth", 150.0f);
        setParam (original, "reverbon", 1.0f);
        original.delay.parameters.getParameter ("mix")->setValueNotifyingHost (0.8f);
        setParam (original, "mod1source", (float) ModulationSources::lfo1);
        original.setSlotDestination (0, original.findDestination ("delay/time"));

        check (original.presets.save ("Lead / Solo").isEmpty(), "enregistrer « Lead / Solo »");
        const auto file = original.presets.fileFor ("Lead / Solo");
        check (file.existsAsFile() && file.getParentDirectory() == folder && ! file.getFileName().contains ("/"),
               "un fichier .scpreset dans le dossier des presets (nom de fichier valide)");
        check (original.presetName == file.getFileNameWithoutExtension(), "le preset enregistré devient le preset actuel");
        check (original.presets.save ("   ").isNotEmpty(), "nom vide refusé");

        // Une autre chaîne, branchée en Mono (R), charge le preset
        ChainProcessor other;
        other.presets.setFolder (folder);
        setParam (other, "input", 2.0f);   // Mono (R)
        check (other.presets.load (file).isEmpty(), "charger le preset");

        auto comparable = [] (ChainProcessor& c)
        {
            auto state = c.createState();
            state.removeProperty ("presetname", nullptr);
            auto params = state.getChildWithName ("CHAIN_PARAMETERS");
            for (int i = params.getNumChildren(); --i >= 0;)
                if (params.getChild (i).getProperty ("id").toString() == "input")
                    params.removeChild (i, nullptr);
            return state.toXmlString();
        };

        check (comparable (other) == comparable (original), "le son chargé est identique (pédales, routage, modulation, branches)");
        check (other.getRouting() == original.getRouting() && other.getSlotDestination (0) == original.getSlotDestination (0)
               && other.delay.parameters.getParameter ("mix")->getValue() == 0.8f,
               "routage, ligne de modulation et Mix du Delay retrouvés");
        check ((int) other.parameters.getRawParameterValue ("input")->load() == 2, "l'entrée reste Mono (R) : le preset ne la change pas");
        check (other.presetName == original.presetName, "le nom du preset chargé est affiché");

        // Le nom du preset est gardé avec la session
        juce::MemoryBlock session;
        other.getStateInformation (session);
        ChainProcessor reopened;
        reopened.setStateInformation (session.getData(), (int) session.getSize());
        check (reopened.presetName == original.presetName, "la session se souvient du preset");

        // Plusieurs presets : ordre alphabétique (naturel), précédent / suivant en boucle
        original.presets.save ("Clean");
        original.presets.save ("Ambient 2");
        original.presets.save ("Ambient 10");
        juce::StringArray names;
        for (auto& f : original.presets.list())
            names.add (f.getFileNameWithoutExtension());
        std::printf ("        %s\n", names.joinIntoString (", ").toRawUTF8());
        check (names.joinIntoString ("|") == "Ambient 2|Ambient 10|Clean|Lead - Solo", "liste par ordre alphabétique (2 avant 10)");

        original.presetName = "Clean";
        check (original.presets.neighbour (1).getFileNameWithoutExtension() == "Lead - Solo"
               && original.presets.neighbour (-1).getFileNameWithoutExtension() == "Ambient 10", "précédent et suivant");
        original.presetName = "Lead - Solo";
        check (original.presets.neighbour (1).getFileNameWithoutExtension() == "Ambient 2", "après le dernier : retour au premier");

        // Enregistrer sous le même nom remplace le preset
        setParam (original, "branchbalance", 20.0f);
        original.presets.save ("Clean");
        ChainProcessor check2;
        check2.presets.load (original.presets.fileFor ("Clean"));
        check (std::abs (check2.parameters.getRawParameterValue ("branchbalance")->load() - 20.0f) < 0.01f
               && original.presets.list().size() == 4, "même nom : le preset est remplacé");

        // Un fichier qui n'est pas un preset ne change rien
        const auto junk = folder.getChildFile (juce::String ("junk") + ChainPresets::extension);
        junk.replaceWithText ("<NOT_A_PRESET/>");
        const auto before = comparable (check2);
        check (check2.presets.load (junk).isNotEmpty() && comparable (check2) == before, "fichier invalide : refusé, rien ne change");

        folder.deleteRecursively();
    }

    void sessions()
    {
        using namespace ChainRouting;
        std::printf ("\n--- Sessions ---\n");

        ChainProcessor original;
        const auto routing = templates()[1].routing;
        original.setRouting (routing);
        setParam (original, "branchspread", 70.0f);

        juce::MemoryBlock state;
        original.getStateInformation (state);

        ChainProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getRouting() == routing, "routage enregistré puis relu");
        check (std::abs (restored.parameters.getRawParameterValue ("branchspread")->load() - 70.0f) < 0.01f, "Spread enregistré puis relu");

        // Ancienne session : l'état d'avant le routage (pas de propriété « routing »)
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        xml->removeAttribute ("routing");
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*xml, oldState);

        ChainProcessor old;
        old.setRouting (routing);
        old.setStateInformation (oldState.getData(), (int) oldState.getSize());
        check (old.getRouting() == Routing::series(), "ancienne session : tout en série, comme avant");
    }
}

// ./ChainTest --dump : liste les réglages et les presets d'usine de chaque effet de la chaîne
static void dumpParameters()
{
    ChainProcessor chain;
    for (int e = 0; e < ChainRouting::numEffects; ++e)
    {
        auto& fx = chain.effect (e);
        std::printf ("\n=== %s\n", ChainRouting::keys()[(size_t) e]);
        for (auto* p : fx.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto& range = r->getNormalisableRange();
                juce::String extra;
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
                    extra = " [" + c->choices.joinIntoString ("|") + "]";
                std::printf ("  %-14s %-22s %9.2f .. %-9.2f def %-8.2f %s%s\n", r->getParameterID().toRawUTF8(), r->getName (30).toRawUTF8(),
                             range.start, range.end, r->convertFrom0to1 (r->getDefaultValue()), r->getLabel().toRawUTF8(), extra.toRawUTF8());
            }
        juce::StringArray programs;
        for (int i = 0; i < fx.getNumPrograms(); ++i)
            programs.add (juce::String (i) + ":" + fx.getProgramName (i));
        std::printf ("  presets: %s\n", programs.joinIntoString (", ").toRawUTF8());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    // ./ChainTest --factory : écrit les presets d'usine dans ~/Music/H3O/Chain Presets/H3000 Inspired
    if (argc > 1 && juce::String (argv[1]) == "--factory")
    {
        const auto error = FactoryPresets::writeAll (ChainPresets::defaultFolder());
        std::printf ("%s\n", error.isEmpty() ? "presets d'usine écrits" : error.toRawUTF8());
        return error.isEmpty() ? 0 : 1;
    }

    if (argc > 1 && juce::String (argv[1]) == "--dump")
    {
        dumpParameters();
        return 0;
    }

    routingModel();
    seriesOrder();
    parallel();
    sessions();
    modulationSources();
    modulationMatrix();
    tempoSync();
    steppedModulation();
    wetDryWet();
    presets();
    compressorInChain();
    tremoloInChain();
    factoryPresets();

    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUT EST BON" : "PROBLÈMES", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
