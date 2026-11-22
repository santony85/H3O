#include "ChainProcessor.h"
#include "ChainEditor.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (
            InputMode::createParameter(),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "harmonizeron", 1 }, "Harmonizer On", true),
            // Amp, Reverse et Chorus coupés par défaut : tes réglages et sessions existants sonnent comme avant
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ampon", 1 }, "Amp On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "filteron", 1 }, "Filter On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "reverseon", 1 }, "Reverse On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "layeredon", 1 }, "Layered Shift On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "choruson", 1 }, "Chorus On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delayon", 1 }, "Delay On", true),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "banddelayon", 1 }, "Band Delay On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ultratapon", 1 }, "Ultra-Tap On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "reverbon", 1 }, "Reverb On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "compressoron", 1 }, "Compressor On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "tremoloon", 1 }, "Tremolo On", false),
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "phaseron", 1 }, "Phaser On", false),
            // Branches parallèles A et B
            std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "branchbalance", 1 }, "Branch Balance",
                                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("%")),
            std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "branchspread", 1 }, "Branch Spread",
                                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("%")),
            // Wet/Dry/Wet : effets de B à 100 %, côte à côte ; largeur stéréo de B
            std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "wetonly", 1 }, "Branch B Wet Only", false),
            std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "branchwidth", 1 }, "Branch B Width",
                                                         juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("%")),
            std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "crossfeedback", 1 }, "Cross Feedback",
                                                         juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withLabel ("%")));

        // Baie de brassage : les 4 mélangeurs (niveau et panoramique de chaque entrée)
        for (int m = 0; m < ChainPatch::numMixers; ++m)
        {
            const juce::String n (m + 1);
            for (auto in : { "a", "b" })
            {
                const juce::String upper = juce::String (in).toUpperCase();
                layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ChainProcessor::mixerParamId (m, "level") + in, 1 },
                                                                         "Mixer " + n + " Level " + upper,
                                                                         juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                                         juce::AudioParameterFloatAttributes().withLabel ("%")));
                layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ChainProcessor::mixerParamId (m, "pan") + in, 1 },
                                                                         "Mixer " + n + " Pan " + upper,
                                                                         juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                                                         juce::AudioParameterFloatAttributes().withLabel ("%")));
            }
        }

        // Sources de modulation
        auto rateRange = [] (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi, 0.01f);
            r.setSkewForCentre (centre);
            return r;
        };

        juce::StringArray shapes;
        for (int i = 0; i < ModulationSources::numShapes; ++i)
            shapes.add (ModulationSources::shapeName (i));

        juce::StringArray divisions;
        for (int i = 0; i < ModulationSources::numDivisions; ++i)
            divisions.add (ModulationSources::divisionName (i));

        for (int l = 0; l < 2; ++l)
        {
            const juce::String n (l + 1);
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "lfo" + n + "rate", 1 }, "LFO " + n + " Rate",
                                                                     rateRange (0.02f, 20.0f, 1.0f), l == 0 ? 0.5f : 3.0f,
                                                                     juce::AudioParameterFloatAttributes().withLabel ("Hz")));
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lfo" + n + "shape", 1 }, "LFO " + n + " Shape",
                                                                      shapes, l == 0 ? ModulationSources::sine : ModulationSources::triangle));
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lfo" + n + "sync", 1 }, "LFO " + n + " Sync", false));
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lfo" + n + "division", 1 }, "LFO " + n + " Division",
                                                                      divisions, ModulationSources::defaultDivision));
        }

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "randomrate", 1 }, "Random Rate",
                                                                 rateRange (0.1f, 20.0f, 2.0f), 2.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "envattack", 1 }, "Envelope Attack",
                                                                 rateRange (1.0f, 200.0f, 20.0f), 10.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "envrelease", 1 }, "Envelope Release",
                                                                 rateRange (20.0f, 2000.0f, 250.0f), 250.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Les lignes de la matrice : source et quantité (la destination est enregistrée à part, par son nom)
        juce::StringArray sourceNames;
        for (int i = 0; i < ModulationSources::numSources; ++i)
            sourceNames.add (ModulationSources::sourceName (i));

        for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
        {
            const juce::String n (slot + 1);
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ChainProcessor::slotParamId (slot, "source"), 1 },
                                                                      "Mod " + n + " Source", sourceNames, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ChainProcessor::slotParamId (slot, "amount"), 1 },
                                                                     "Mod " + n + " Amount",
                                                                     juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 50.0f,
                                                                     juce::AudioParameterFloatAttributes().withLabel ("%")));
        }

        return layout;
    }
}

ChainProcessor::ChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "CHAIN_PARAMETERS", createParameterLayout())
{
    harmonizerOn = parameters.getRawParameterValue ("harmonizeron");
    ampOn        = parameters.getRawParameterValue ("ampon");
    filterOn     = parameters.getRawParameterValue ("filteron");
    reverseOn    = parameters.getRawParameterValue ("reverseon");
    layeredOn    = parameters.getRawParameterValue ("layeredon");
    chorusOn     = parameters.getRawParameterValue ("choruson");
    delayOn      = parameters.getRawParameterValue ("delayon");
    bandDelayOn  = parameters.getRawParameterValue ("banddelayon");
    ultraTapOn   = parameters.getRawParameterValue ("ultratapon");
    reverbOn     = parameters.getRawParameterValue ("reverbon");
    compressorOn = parameters.getRawParameterValue ("compressoron");
    tremoloOn    = parameters.getRawParameterValue ("tremoloon");
    phaserOn     = parameters.getRawParameterValue ("phaseron");

    // Lus à travers la modulation, comme les réglages des effets
    branchBalance = chainTargets.get (parameters, "branchbalance");
    branchSpread  = chainTargets.get (parameters, "branchspread");
    crossFeedback = chainTargets.get (parameters, "crossfeedback");
    branchWidth   = chainTargets.get (parameters, "branchwidth");
    wetOnly       = parameters.getRawParameterValue ("wetonly");

    // Les mélangeurs de la baie : modulables comme les branches
    for (int m = 0; m < ChainPatch::numMixers; ++m)
    {
        mixerParams[(size_t) (m * 4 + 0)] = chainTargets.get (parameters, mixerParamId (m, "levela"));
        mixerParams[(size_t) (m * 4 + 1)] = chainTargets.get (parameters, mixerParamId (m, "levelb"));
        mixerParams[(size_t) (m * 4 + 2)] = chainTargets.get (parameters, mixerParamId (m, "pana"));
        mixerParams[(size_t) (m * 4 + 3)] = chainTargets.get (parameters, mixerParamId (m, "panb"));
    }

    for (int l = 0; l < 2; ++l)
    {
        lfoRate[l]  = parameters.getRawParameterValue ("lfo" + juce::String (l + 1) + "rate");
        lfoShape[l] = parameters.getRawParameterValue ("lfo" + juce::String (l + 1) + "shape");
        lfoSync[l]     = parameters.getRawParameterValue ("lfo" + juce::String (l + 1) + "sync");
        lfoDivision[l] = parameters.getRawParameterValue ("lfo" + juce::String (l + 1) + "division");
    }
    randomRate = parameters.getRawParameterValue ("randomrate");
    envAttack  = parameters.getRawParameterValue ("envattack");
    envRelease = parameters.getRawParameterValue ("envrelease");

    for (int slot = 0; slot < numSlots; ++slot)
    {
        slotSource[(size_t) slot] = parameters.getRawParameterValue (slotParamId (slot, "source"));
        slotAmount[(size_t) slot] = parameters.getRawParameterValue (slotParamId (slot, "amount"));
        slotDestination[(size_t) slot] = -1;
        slotOutput[(size_t) slot] = 0.0f;
    }
    for (auto& v : sourceValues)
        v = 0.0f;

    // Même ordre que ChainRouting::Effect
    effects  = { &filter, &amp, &harmonizer, &reverse, &layered, &chorus, &delay, &bandDelay, &ultraTap, &reverb, &compressor, &tremolo, &phaser };
    onParams = { filterOn, ampOn, harmonizerOn, reverseOn, layeredOn, chorusOn, delayOn, bandDelayOn, ultraTapOn, reverbOn, compressorOn, tremoloOn, phaserOn };

    // Tout ce que la matrice peut moduler : les réglages continus de chaque effet, puis les branches
    static const char* effectNames[] = { "Filter", "Amp", "Harmonizer", "Reverse", "Layered", "Chorus",
                                         "Delay", "Band Delay", "Ultra-Tap", "Reverb", "Compressor", "Tremolo", "Phaser", "Branches & Mixers" };
    std::array<ModTargets*, ChainRouting::numEffects + 1> targetLists {
        &filter.modulation, &amp.modulation, &harmonizer.modulation, &reverse.modulation, &layered.modulation,
        &chorus.modulation, &delay.modulation, &bandDelay.modulation, &ultraTap.modulation, &reverb.modulation, &compressor.modulation, &tremolo.modulation, &phaser.modulation, &chainTargets
    };

    for (int e = 0; e <= ChainRouting::numEffects; ++e)
    {
        auto& list = *targetLists[(size_t) e];
        const juce::String effectKey = e < ChainRouting::numEffects ? ChainRouting::keys()[(size_t) e] : "branches";

        for (int i = 0; i < list.size(); ++i)
        {
            Destination d;
            d.key        = effectKey + "/" + list.idOf (i);
            d.effectName = effectNames[e];
            d.paramName  = list[i].parameter->getName (40);
            d.effect     = e;
            d.target     = &list[i];
            d.numSteps   = list[i].numSteps;
            d.stepUnit   = list[i].parameter->getLabel();
            destinations.push_back (d);
        }
    }

    // Le Mix de chaque effet, que Wet Only pousse à 100 % dans la branche B
    for (int e = 0; e < ChainRouting::numEffects; ++e)
        mixDestination[(size_t) e] = findDestination (juce::String (ChainRouting::keys()[(size_t) e]) + "/mix");

    modSum.assign (destinations.size(), 0.0f);
    modApplied.assign (destinations.size(), 0.0f);

    // C'est la chaîne qui choisit l'entrée ; les effets reçoivent déjà de la stéréo
    input.attach (parameters);
    harmonizer.input.setInsideChain();
    amp.input.setInsideChain();
    filter.input.setInsideChain();
    reverse.input.setInsideChain();
    layered.input.setInsideChain();
    chorus.input.setInsideChain();
    delay.input.setInsideChain();
    bandDelay.input.setInsideChain();
    ultraTap.input.setInsideChain();
    reverb.input.setInsideChain();
    compressor.input.setInsideChain();
    tremolo.input.setInsideChain();
    phaser.input.setInsideChain();
}

const char* ChainProcessor::onParamId (int id)
{
    static const char* ids[] = { "filteron", "ampon", "harmonizeron", "reverseon", "layeredon",
                                 "choruson", "delayon", "banddelayon", "ultratapon", "reverbon", "compressoron", "tremoloon", "phaseron" };
    return ids[juce::jlimit (0, ChainRouting::numEffects - 1, id)];
}

void ChainProcessor::setRouting (const ChainRouting::Routing& r)
{
    auto copy = r;
    copy.normalise();
    if (copy.pack() == routing.exchange (copy.pack()))
        return;

    // La session a changé : Logic propose de l'enregistrer
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

bool ChainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono (guitare) ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void ChainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chainTargets.update();   // Balance, Spread et Cross Feedback à jour avant de régler le lissage

    // Les effets internes travaillent toujours en stéréo
    for (auto* fx : effects)
    {
        // Pas de tête de lecture hors de processBlock (voir plus bas)
        fx->setPlayHead (nullptr);
        fx->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        fx->prepareToPlay (sampleRate, samplesPerBlock);
    }

    const int size = juce::jmax (1, samplesPerBlock);
    laneA.setSize (2, size);
    laneB.setSize (2, size);
    laneWet.setSize (2, size);
    laneEffect.setSize (2, size);

    // Le retour croisé a un bloc de retard : la sortie de B n'existe qu'une fois A calculé
    feedbackLine.setSize (2, size);
    feedbackLine.clear();
    feedbackPos = 0;

    const float balance = branchBalance->load() * 0.01f, spread = branchSpread->load() * 0.01f;
    const float start[] = { (1.0f - balance) * (1.0f + spread), (1.0f - balance) * (1.0f - spread),
                            balance * (1.0f - spread), balance * (1.0f + spread) };
    for (size_t i = 0; i < mixGains.size(); ++i)
    {
        mixGains[i].reset (sampleRate, 0.03);
        mixGains[i].setCurrentAndTargetValue (start[i]);
    }
    sources.prepare (sampleRate);
    monoScratch.assign ((size_t) size, 0.0f);

    for (auto& b : nodeOut)
    {
        b.setSize (2, juce::jmax (size, ChainPatch::feedbackChunk));
        b.clear();
    }
    nodeLength.fill (0);
    gatherA.setSize (2, juce::jmax (size, ChainPatch::feedbackChunk));
    gatherB.setSize (2, juce::jmax (size, ChainPatch::feedbackChunk));
    mixerGainsReady = false;
    publishPlan (getPatch());

    widthGain.reset (sampleRate, 0.03);
    widthGain.setCurrentAndTargetValue (branchWidth->load() * 0.01f);
    feedbackGain.reset (sampleRate, 0.03);
    feedbackGain.setCurrentAndTargetValue (crossFeedback->load() * 0.01f);
}

void ChainProcessor::releaseResources()
{
    for (auto* fx : effects)
        fx->releaseResources();
}

double ChainProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    for (auto* fx : effects)
        tail += fx->getTailLengthSeconds();
    return tail;
}

// Les effets allumés d'une section, dans l'ordre choisi
void ChainProcessor::processLane (int lane, const ChainRouting::Routing& r, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (auto id : r.order)
        if (r.lane[id] == lane && isEffectOn (id))
            effects[id]->processBlock (buffer, midi);
}

void ChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Choix de l'entrée (Stereo / Mono L / Mono R) : les effets reçoivent ensuite de la stéréo
    input.process (buffer, getTotalNumInputChannels());

    // Les effets internes n'ont pas d'hôte : on leur prête la tête de lecture de la chaîne
    // (indispensable pour que Sync lise le tempo de Logic).
    // Elle n'est valable que pendant ce bloc : certains hôtes la détruisent ensuite.
    // On la retire donc à la fin, sinon les effets garderaient un pointeur invalide.
    auto* playHead = getPlayHead();
    for (auto* fx : effects)
        fx->setPlayHead (playHead);

    // Tempo et position du morceau, pour les LFO synchronisés
    hostPlaying = false;
    if (playHead != nullptr)
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                tempo.store ((float) *bpm);

            if (auto signature = position->getTimeSignature(); signature.hasValue() && signature->denominator > 0)
                quartersPerBar = 4.0 * signature->numerator / signature->denominator;

            if (auto ppq = position->getPpqPosition(); ppq.hasValue() && position->getIsPlaying())
            {
                blockPpq = *ppq;
                hostPlaying = true;
            }
        }

    const auto r = getRouting();   // lu une seule fois : le même routage pour tout le bloc
    const int n = buffer.getNumSamples();

    // Avec une modulation en cours, le bloc est traité par paquets de 64 échantillons (~1,3 ms) :
    // les réglages modulés bougent sans marches audibles
    bool modulating = false;
    for (int slot = 0; slot < numSlots; ++slot)
        modulating = modulating || isSlotActive (slot);

    // Baie de brassage : le plan publié par l'interface (pris seulement s'il est libre, sans jamais attendre)
    const bool patching = patchMode.load();
    if (patching)
    {
        const juce::GenericScopedTryLock<juce::SpinLock> lock (planLock);
        if (lock.isLocked() && planPending)
        {
            activePlan = pendingPlan;
            planPending = false;
            for (auto& b : nodeOut)
                b.clear();
            nodeLength.fill (0);
        }
    }

    // Une boucle de câbles avance par paquets de 64 échantillons : c'est son retard
    const int chunk = (modulating || (patching && activePlan.hasDelayed)) ? ChainPatch::feedbackChunk : n;
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int start = 0; start < n; start += chunk)
    {
        const int length = juce::jmin (chunk, n - start);
        float* channels[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (numChannels - 1, start) };
        juce::AudioBuffer<float> part (channels, numChannels, length);

        updateModulation (part, start, r);
        if (patching && numChannels >= 2)
            processPatch (part, midi);
        else
            processRouting (r, part, midi);
    }

    for (auto* fx : effects)
        fx->setPlayHead (nullptr);
}

void ChainProcessor::processRouting (const ChainRouting::Routing& r, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();

    processLane (ChainRouting::pre, r, buffer, midi);

    if (r.hasParallel() && buffer.getNumChannels() >= 2)
    {
        // Normalement jamais : l'hôte ne dépasse pas la taille annoncée dans prepareToPlay
        if (n > laneA.getNumSamples())
        {
            laneA.setSize (2, n, false, false, true);
            laneB.setSize (2, n, false, false, true);
        }

        juce::AudioBuffer<float> a (laneA.getArrayOfWritePointers(), 2, n);
        juce::AudioBuffer<float> b (laneB.getArrayOfWritePointers(), 2, n);

        for (int ch = 0; ch < 2; ++ch)
        {
            a.copyFrom (ch, 0, buffer, ch, 0, n);
            b.copyFrom (ch, 0, buffer, ch, 0, n);
        }

        // Retour croisé : la sortie de B, un bloc plus tôt, réinjectée dans A (adoucie pour ne jamais s'emballer)
        feedbackGain.setTargetValue (crossFeedback->load() * 0.01f);
        const bool feedback = feedbackGain.isSmoothing() || feedbackGain.getTargetValue() > 0.0f;
        const int lineSize = feedbackLine.getNumSamples();

        if (feedback)
        {
            auto* left = a.getWritePointer (0);
            auto* right = a.getWritePointer (1);
            const auto* lineL = feedbackLine.getReadPointer (0);
            const auto* lineR = feedbackLine.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float g = feedbackGain.getNextValue();
                const int pos = (feedbackPos + i) % lineSize;
                left[i]  += g * std::tanh (lineL[pos]);
                right[i] += g * std::tanh (lineR[pos]);
            }
        }
        else
        {
            feedbackGain.skip (n);
        }

        processLane (ChainRouting::branchA, r, a, midi);

        const bool killDry = wetOnly->load() >= 0.5f;
        if (killDry)
        {
            // Wet Only, comme le mélangeur d'effets d'un rack : chaque effet de B reçoit l'entrée de la branche,
            // sort 100 % effet, et les sorties s'additionnent (sans son sec)
            if (n > laneWet.getNumSamples())
            {
                laneWet.setSize (2, n, false, false, true);
                laneEffect.setSize (2, n, false, false, true);
            }

            juce::AudioBuffer<float> wet (laneWet.getArrayOfWritePointers(), 2, n);
            juce::AudioBuffer<float> one (laneEffect.getArrayOfWritePointers(), 2, n);
            wet.clear();

            for (auto id : r.order)
                if (r.lane[id] == ChainRouting::branchB && isEffectOn (id))
                {
                    for (int ch = 0; ch < 2; ++ch)
                        one.copyFrom (ch, 0, b, ch, 0, n);
                    effects[id]->processBlock (one, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        wet.addFrom (ch, 0, one, ch, 0, n);
                }

            for (int ch = 0; ch < 2; ++ch)
                b.copyFrom (ch, 0, wet, ch, 0, n);
        }
        else
        {
            processLane (ChainRouting::branchB, r, b, midi);
        }

        // Largeur stéréo de B (Width) : 0 % = mono, 100 % = telle quelle, 200 % = élargie
        widthGain.setTargetValue (branchWidth->load() * 0.01f);
        if (widthGain.isSmoothing() || ! juce::exactlyEqual (widthGain.getTargetValue(), 1.0f))
        {
            auto* left  = b.getWritePointer (0);
            auto* right = b.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float w = widthGain.getNextValue();
                const float mid = 0.5f * (left[i] + right[i]), side = 0.5f * (left[i] - right[i]) * w;
                left[i]  = mid + side;
                right[i] = mid - side;
            }
        }
        else
        {
            widthGain.skip (n);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* line = feedbackLine.getWritePointer (ch);
            const auto* src = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                line[(feedbackPos + i) % lineSize] = feedback ? src[i] : 0.0f;
        }
        feedbackPos = (feedbackPos + n) % lineSize;

        // Mélange : Balance dose A et B, Spread envoie A à gauche et B à droite
        const float balance = branchBalance->load() * 0.01f;
        const float spread  = branchSpread->load() * 0.01f;
        // Wet Only : A (sec) et B (effets seuls) ne se ressemblent plus, on ne les dose pas en fondu :
        // à 50 %, le sec et les effets sont tous deux à plein volume, comme les baffles d'un Wet/Dry/Wet
        const float gainA = killDry ? juce::jmin (1.0f, 2.0f * (1.0f - balance)) : 1.0f - balance;
        const float gainB = killDry ? juce::jmin (1.0f, 2.0f * balance) : balance;

        mixGains[0].setTargetValue (gainA * (1.0f + spread));   // A à gauche
        mixGains[1].setTargetValue (gainA * (1.0f - spread));   // A à droite
        mixGains[2].setTargetValue (gainB * (1.0f - spread));   // B à gauche
        mixGains[3].setTargetValue (gainB * (1.0f + spread));   // B à droite

        auto* outL = buffer.getWritePointer (0);
        auto* outR = buffer.getWritePointer (1);
        const auto* aL = a.getReadPointer (0);
        const auto* aR = a.getReadPointer (1);
        const auto* bL = b.getReadPointer (0);
        const auto* bR = b.getReadPointer (1);

        for (int i = 0; i < n; ++i)
        {
            outL[i] = mixGains[0].getNextValue() * aL[i] + mixGains[2].getNextValue() * bL[i];
            outR[i] = mixGains[1].getNextValue() * aR[i] + mixGains[3].getNextValue() * bR[i];
        }
    }

    processLane (ChainRouting::post, r, buffer, midi);
}

//==============================================================================
// Baie de brassage

ChainPatch::Patch ChainProcessor::getPatch() const
{
    const juce::ScopedLock lock (patchLock);
    return patch;
}

void ChainProcessor::publishPlan (const ChainPatch::Patch& p)
{
    const auto plan = ChainPatch::compile (p);
    const juce::SpinLock::ScopedLockType lock (planLock);
    pendingPlan = plan;
    planPending = true;
}

void ChainProcessor::setPatch (const ChainPatch::Patch& p)
{
    {
        const juce::ScopedLock lock (patchLock);
        if (patch == p)
            return;
        patch = p;
    }
    publishPlan (p);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void ChainProcessor::convertRoutingToPatch()
{
    // Les réglages eux-mêmes (pas les valeurs modulées, qui ne sont mises à jour qu'au bloc suivant)
    std::array<bool, ChainRouting::numEffects> on {};
    for (int e = 0; e < ChainRouting::numEffects; ++e)
        on[(size_t) e] = isEffectOn (e);
    const auto c = ChainPatch::fromRouting (getRouting(), parameters.getRawParameterValue ("branchbalance")->load(),
                                            parameters.getRawParameterValue ("branchspread")->load(), wetOnly->load() >= 0.5f, on);

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (mixerParamId (0, "levela"), c.levelA);
    set (mixerParamId (0, "levelb"), c.levelB);
    set (mixerParamId (0, "pana"), c.panA);
    set (mixerParamId (0, "panb"), c.panB);

    setPatch (c.patch);
}

void ChainProcessor::setPatchMode (bool on)
{
    // Première fois : la baie part du routage à branches actuel, déjà câblé
    if (on && getPatch().cables.empty())
        convertRoutingToPatch();

    if (patchMode.exchange (on) != on)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

// Additionne dans « dest » tout ce qui arrive à une prise IN. Un câble qui revient en arrière lit le paquet précédent,
// adouci (tanh) : une boucle ne peut pas s'emballer.
void ChainProcessor::gather (int node, int port, juce::AudioBuffer<float>& dest, int n)
{
    dest.clear (0, n);
    for (int i = 0; i < activePlan.numLinks; ++i)
    {
        const auto& link = activePlan.links[(size_t) i];
        if (link.to != node || link.port != port)
            continue;

        const auto& source = nodeOut[link.from];
        if (! link.delayed)
        {
            for (int ch = 0; ch < 2; ++ch)
                dest.addFrom (ch, 0, source, ch, 0, n);
            continue;
        }

        const int length = juce::jmin (n, nodeLength[link.from]);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* out = dest.getWritePointer (ch);
            const auto* in = source.getReadPointer (ch);
            for (int k = 0; k < length; ++k)
                out[k] += std::tanh (in[k]);
        }
    }
}

void ChainProcessor::processPatch (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    using namespace ChainPatch;
    const int n = buffer.getNumSamples();

    // Normalement jamais : l'hôte ne dépasse pas la taille annoncée dans prepareToPlay
    if (n > gatherA.getNumSamples())
    {
        for (auto& b : nodeOut)
            b.setSize (2, n, true, true, true);
        gatherA.setSize (2, n, false, false, true);
        gatherB.setSize (2, n, false, false, true);
    }

    for (int ch = 0; ch < 2; ++ch)
        nodeOut[inputNode].copyFrom (ch, 0, buffer, ch, 0, n);
    nodeLength[inputNode] = n;

    for (int step = 0; step < activePlan.numSteps; ++step)
    {
        const int node = activePlan.steps[(size_t) step];
        auto& out = nodeOut[(size_t) node];

        if (isEffect (node))
        {
            // L'entrée est rassemblée avant d'écrire : une pédale bouclée sur elle-même lit encore le paquet précédent
            gather (node, 0, gatherA, n);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, 0, gatherA, ch, 0, n);

            if (isEffectOn (node))
            {
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, n);
                effects[(size_t) node]->processBlock (view, midi);
            }
        }
        else if (isMixer (node))
        {
            gather (node, 0, gatherA, n);
            gather (node, 1, gatherB, n);

            // Gains du paquet : niveau × panoramique (comme Spread : −100 % = tout à gauche, à double niveau)
            const int m = node - numEffects;
            float target[4];
            for (int in = 0; in < 2; ++in)
            {
                const float level = mixerParams[(size_t) (m * 4 + in)]->load() * 0.01f;
                const float pan   = juce::jlimit (-1.0f, 1.0f, mixerParams[(size_t) (m * 4 + 2 + in)]->load() * 0.01f);
                target[in * 2]     = level * (1.0f - pan);   // gauche
                target[in * 2 + 1] = level * (1.0f + pan);   // droite
            }

            auto* gains = &mixerGains[(size_t) (m * 4)];
            if (! mixerGainsReady)
                std::copy (target, target + 4, gains);

            auto* outL = out.getWritePointer (0);
            auto* outR = out.getWritePointer (1);
            const auto* aL = gatherA.getReadPointer (0);
            const auto* aR = gatherA.getReadPointer (1);
            const auto* bL = gatherB.getReadPointer (0);
            const auto* bR = gatherB.getReadPointer (1);
            const float step4 = 1.0f / (float) n;
            for (int k = 0; k < n; ++k)
            {
                const float t = (float) (k + 1) * step4;   // rampe : tourner un bouton ne fait pas de clic
                outL[k] = (gains[0] + t * (target[0] - gains[0])) * aL[k] + (gains[2] + t * (target[2] - gains[2])) * bL[k];
                outR[k] = (gains[1] + t * (target[1] - gains[1])) * aR[k] + (gains[3] + t * (target[3] - gains[3])) * bR[k];
            }
            std::copy (target, target + 4, gains);
        }

        nodeLength[(size_t) node] = n;
    }
    mixerGainsReady = true;

    // La sortie : tout ce qui arrive à la prise OUT de la chaîne (rien de branché = silence)
    gather (outputNode, 0, gatherA, n);
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom (ch, 0, gatherA, ch, 0, n);
}

bool ChainProcessor::isSlotActive (int slot) const noexcept
{
    return slotSource[(size_t) slot]->load() >= 0.5f && slotDestination[(size_t) slot].load() >= 0;
}

int ChainProcessor::findDestination (const juce::String& key) const
{
    for (size_t i = 0; i < destinations.size(); ++i)
        if (destinations[i].key == key)
            return (int) i;
    return -1;
}

void ChainProcessor::setSlotDestination (int slot, int destination)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    destination = juce::isPositiveAndBelow (destination, (int) destinations.size()) ? destination : -1;
    if (slotDestination[(size_t) slot].exchange (destination) != destination)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

// Avance les sources et pose les décalages sur les réglages visés
void ChainProcessor::updateModulation (const juce::AudioBuffer<float>& part, int startInBlock, const ChainRouting::Routing& r)
{
    const int n = part.getNumSamples();
    const double bpm = tempo.load();

    ModulationSources::Settings settings;
    for (int l = 0; l < 2; ++l)
    {
        settings.lfoRate[l]  = lfoRate[l]->load();
        settings.lfoShape[l] = (int) lfoShape[l]->load();

        // Synchronisé : un cycle par division, au tempo du morceau
        if (lfoSync[l]->load() >= 0.5f)
        {
            const double quarters = ModulationSources::divisionQuarters ((int) lfoDivision[l]->load(), quartersPerBar);
            settings.lfoRate[l] = (float) (bpm / 60.0 / quarters);

            // En lecture : la phase suit la position dans le morceau (même départ à chaque mesure, à chaque boucle)
            if (hostPlaying)
                sources.setLfoPhase (l, (blockPpq + startInBlock / getSampleRate() * bpm / 60.0) / quarters);
        }
    }
    settings.randomRate = randomRate->load();
    settings.attackMs   = envAttack->load();
    settings.releaseMs  = envRelease->load();
    sources.setSettings (settings);

    // Les sources écoutent ton jeu, avant les effets (mono)
    if ((int) monoScratch.size() >= n)
    {
        const auto* left  = part.getReadPointer (0);
        const auto* right = part.getReadPointer (part.getNumChannels() - 1);
        for (int i = 0; i < n; ++i)
            monoScratch[(size_t) i] = 0.5f * (left[i] + right[i]);
        bool usesPitch = false;
        for (int slot = 0; slot < numSlots; ++slot)
            usesPitch = usesPitch || (isSlotActive (slot) && (int) slotSource[(size_t) slot]->load() == ModulationSources::pitch);

        sources.process (monoScratch.data(), n, usesPitch);
    }

    for (int s = 0; s < ModulationSources::numSources; ++s)
        sourceValues[(size_t) s].store (sources.get (s), std::memory_order_relaxed);

    std::fill (modSum.begin(), modSum.end(), 0.0f);
    for (int slot = 0; slot < numSlots; ++slot)
    {
        const int source = (int) slotSource[(size_t) slot]->load();
        const int destination = slotDestination[(size_t) slot].load();
        float out = 0.0f;

        if (source > 0 && juce::isPositiveAndBelow (destination, (int) modSum.size()))
        {
            out = slotAmount[(size_t) slot]->load() * 0.01f * sources.get (source);
            modSum[(size_t) destination] += out;
        }
        slotOutput[(size_t) slot].store (out, std::memory_order_relaxed);
    }

    // Wet Only : les effets de la branche B ne gardent que leur effet (Mix poussé au maximum)
    if (wetOnly->load() >= 0.5f && ! patchMode.load())
        for (int e = 0; e < ChainRouting::numEffects; ++e)
            if (r.lane[(size_t) e] == ChainRouting::branchB && mixDestination[(size_t) e] >= 0)
                modSum[(size_t) mixDestination[(size_t) e]] += 2.0f;

    for (size_t d = 0; d < modSum.size(); ++d)
        if (! juce::exactlyEqual (modSum[d], modApplied[d]))
        {
            destinations[d].target->offset.store (modSum[d], std::memory_order_relaxed);
            modApplied[d] = modSum[d];
        }

    chainTargets.update();
}

juce::AudioProcessorEditor* ChainProcessor::createEditor()
{
    return new ChainEditor (*this);
}

// L'état de la chaîne = ses propres paramètres + l'état complet de chaque effet.
// Le même contenu sert aux sessions de Logic (en binaire) et aux fichiers de presets (en XML).
juce::ValueTree ChainProcessor::createState()
{
    juce::ValueTree state ("SIMPLE_CHAIN");
    state.appendChild (parameters.copyState(), nullptr);

    juce::MemoryBlock filterState, ampState, harmonizerState, reverseState, layeredState, chorusState, delayState, bandDelayState, ultraTapState, reverbState, compressorState, tremoloState, phaserState;
    filter.getStateInformation (filterState);
    amp.getStateInformation (ampState);
    harmonizer.getStateInformation (harmonizerState);
    reverse.getStateInformation (reverseState);
    layered.getStateInformation (layeredState);
    chorus.getStateInformation (chorusState);
    delay.getStateInformation (delayState);
    bandDelay.getStateInformation (bandDelayState);
    ultraTap.getStateInformation (ultraTapState);
    reverb.getStateInformation (reverbState);
    compressor.getStateInformation (compressorState);
    tremolo.getStateInformation (tremoloState);
    phaser.getStateInformation (phaserState);

    state.setProperty ("harmonizer", harmonizerState.toBase64Encoding(), nullptr);
    state.setProperty ("filter", filterState.toBase64Encoding(), nullptr);
    state.setProperty ("amp", ampState.toBase64Encoding(), nullptr);
    state.setProperty ("reverse", reverseState.toBase64Encoding(), nullptr);
    state.setProperty ("layered", layeredState.toBase64Encoding(), nullptr);
    state.setProperty ("chorus", chorusState.toBase64Encoding(), nullptr);
    state.setProperty ("delay", delayState.toBase64Encoding(), nullptr);
    state.setProperty ("banddelay", bandDelayState.toBase64Encoding(), nullptr);
    state.setProperty ("ultratap", ultraTapState.toBase64Encoding(), nullptr);
    state.setProperty ("reverb", reverbState.toBase64Encoding(), nullptr);
    state.setProperty ("compressor", compressorState.toBase64Encoding(), nullptr);
    state.setProperty ("tremolo", tremoloState.toBase64Encoding(), nullptr);
    state.setProperty ("phaser", phaserState.toBase64Encoding(), nullptr);

    state.setProperty ("routing", juce::String (getRouting().toString()), nullptr);
    state.setProperty ("patch", juce::String (getPatch().toString()), nullptr);
    state.setProperty ("patchmode", isPatchMode(), nullptr);

    // Destinations de la matrice, par leur nom ("delay/time") : robuste si des réglages s'ajoutent un jour
    for (int slot = 0; slot < numSlots; ++slot)
    {
        const int d = slotDestination[(size_t) slot].load();
        state.setProperty ("mod" + juce::String (slot + 1) + "dest",
                           juce::isPositiveAndBelow (d, (int) destinations.size()) ? destinations[(size_t) d].key : juce::String(), nullptr);
    }

    state.setProperty ("presetname", presetName, nullptr);
    return state;
}

void ChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName ("SIMPLE_CHAIN"))
        restoreState (juce::ValueTree::fromXml (*xml), false);
}

bool ChainProcessor::restoreState (const juce::ValueTree& state, bool keepInput)
{
    if (! state.hasType ("SIMPLE_CHAIN"))
        return false;

    // Un preset ne change pas l'entrée : elle dépend de ton branchement (guitare sur l'entrée 2…), pas du son
    auto* inputParam = parameters.getParameter ("input");
    const float currentInput = inputParam->getValue();

    // Un preset sans Amp (les presets d'usine) garde ton Amp tel quel : modèle NAM, IR, réglages, et allumé ou non
    auto* ampOnParam = parameters.getParameter ("ampon");
    const float currentAmpOn = ampOnParam->getValue();
    const bool keepAmp = ! state.hasProperty ("amp");

    const auto own = state.getChildWithName (parameters.state.getType());
    if (own.isValid())
        parameters.replaceState (own);

    if (keepInput)
        inputParam->setValueNotifyingHost (currentInput);
    if (keepAmp)
        ampOnParam->setValueNotifyingHost (currentAmpOn);

    auto restore = [&state] (const juce::Identifier& key, juce::AudioProcessor& fx)
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (state.getProperty (key).toString()) && block.getSize() > 0)
            fx.setStateInformation (block.getData(), (int) block.getSize());
    };

    restore ("harmonizer", harmonizer);
    restore ("filter", filter);   // absent des anciennes sessions : réglages d'usine
    restore ("amp", amp);
    restore ("layered", layered);   // absent des anciennes sessions : réglages d'usine
    restore ("reverse", reverse);   // absent des anciennes sessions : réglages d'usine
    restore ("chorus", chorus);   // absent des anciennes sessions : le chorus garde ses réglages d'usine
    restore ("delay", delay);
    restore ("banddelay", bandDelay);   // absent des anciennes sessions : réglages d'usine
    restore ("reverb", reverb);   // absent des anciennes sessions : réglages d'usine
    restore ("ultratap", ultraTap);   // absent des anciennes sessions : réglages d'usine
    restore ("compressor", compressor);   // absent des anciennes sessions : réglages d'usine (et éteint)
    restore ("tremolo", tremolo);         // absent des anciennes sessions : réglages d'usine (et éteint)
    restore ("phaser", phaser);           // absent des anciennes sessions : réglages d'usine (et éteint)

    // Absent des anciennes sessions : tout en série, dans l'ordre d'origine
    // (sans prévenir l'hôte : charger une session ne la modifie pas)
    routing.store (ChainRouting::Routing::fromString (state.getProperty ("routing").toString().toStdString()).pack());

    // Absents des anciennes sessions : pas de câbles, routage à branches
    {
        const auto restored = ChainPatch::Patch::fromString (state.getProperty ("patch").toString().toStdString());
        {
            const juce::ScopedLock lock (patchLock);
            patch = restored;
        }
        publishPlan (restored);
        patchMode.store ((bool) state.getProperty ("patchmode", false));
    }

    // Absentes des anciennes sessions : aucune modulation
    for (int slot = 0; slot < numSlots; ++slot)
        slotDestination[(size_t) slot] = findDestination (state.getProperty ("mod" + juce::String (slot + 1) + "dest").toString());

    presetName = state.getProperty ("presetname").toString();
    return true;
}

// Point d'entrée appelé par Logic pour créer le plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChainProcessor();
}
