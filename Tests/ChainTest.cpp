// Tests du routage de Simple Chain : ordre libre, branches parallèles A / B, état des sessions.
//   ./ChainTest        Cible Xcode : ChainTest
#include "ChainProcessor.h"

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

    float maxDifference (const Signal& a, const Signal& b)
    {
        float d = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < a[(size_t) ch].size(); ++i)
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
        check (r.effectsIn (pre).size() == 7, "les autres restent avant la séparation");

        r.move (amp, pre, 0);
        r.move (filter, pre, 99);
        check (r.effectsIn (pre).front() == amp && r.effectsIn (pre).back() == filter, "ordre libre : Amp avant, Filter en dernier");
        check (Routing::unpack (r.pack()) == r && Routing::fromString (r.toString()) == r, "routage modifié : relu à l'identique");

        check (Routing::fromString ("") == series, "session sans routage : tout en série");
        check (Routing::unpack (0) == series, "valeur invalide : tout en série");

        // Session d'une version future avec un effet inconnu, et sans la réverbe : rien ne se perd
        const auto partial = Routing::fromString ("delay:a,unknown:b,amp:pre");
        check (partial.laneOf (delay) == branchA && partial.laneOf (amp) == pre && partial.laneOf (reverb) == pre
               && partial.effectsIn (pre).front() == amp,
               "session incomplète : les effets manquants se rangent en série");
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

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    routingModel();
    seriesOrder();
    parallel();
    sessions();

    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUT EST BON" : "PROBLÈMES", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
