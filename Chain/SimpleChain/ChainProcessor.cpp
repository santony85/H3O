#include "ChainProcessor.h"
#include "ChainEditor.h"

ChainProcessor::ChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "CHAIN_PARAMETERS",
                  {
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
                      // Branches parallèles A et B
                      std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "branchbalance", 1 }, "Branch Balance",
                                                                   juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                                   juce::AudioParameterFloatAttributes().withLabel ("%")),
                      std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "branchspread", 1 }, "Branch Spread",
                                                                   juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                                   juce::AudioParameterFloatAttributes().withLabel ("%")),
                      std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "crossfeedback", 1 }, "Cross Feedback",
                                                                   juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 0.0f,
                                                                   juce::AudioParameterFloatAttributes().withLabel ("%")),
                  })
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

    branchBalance = parameters.getRawParameterValue ("branchbalance");
    branchSpread  = parameters.getRawParameterValue ("branchspread");
    crossFeedback = parameters.getRawParameterValue ("crossfeedback");

    // Même ordre que ChainRouting::Effect
    effects  = { &filter, &amp, &harmonizer, &reverse, &layered, &chorus, &delay, &bandDelay, &ultraTap, &reverb };
    onParams = { filterOn, ampOn, harmonizerOn, reverseOn, layeredOn, chorusOn, delayOn, bandDelayOn, ultraTapOn, reverbOn };

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
}

const char* ChainProcessor::onParamId (int id)
{
    static const char* ids[] = { "filteron", "ampon", "harmonizeron", "reverseon", "layeredon",
                                 "choruson", "delayon", "banddelayon", "ultratapon", "reverbon" };
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

    const auto r = getRouting();   // lu une seule fois : le même routage pour tout le bloc
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
        processLane (ChainRouting::branchB, r, b, midi);

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
        mixGains[0].setTargetValue ((1.0f - balance) * (1.0f + spread));   // A à gauche
        mixGains[1].setTargetValue ((1.0f - balance) * (1.0f - spread));   // A à droite
        mixGains[2].setTargetValue (balance * (1.0f - spread));            // B à gauche
        mixGains[3].setTargetValue (balance * (1.0f + spread));            // B à droite

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

    for (auto* fx : effects)
        fx->setPlayHead (nullptr);
}

juce::AudioProcessorEditor* ChainProcessor::createEditor()
{
    return new ChainEditor (*this);
}

// L'état de la chaîne = ses propres paramètres + l'état complet de chaque effet
void ChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("SIMPLE_CHAIN");
    state.appendChild (parameters.copyState(), nullptr);

    juce::MemoryBlock filterState, ampState, harmonizerState, reverseState, layeredState, chorusState, delayState, bandDelayState, ultraTapState, reverbState;
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

    state.setProperty ("routing", juce::String (getRouting().toString()), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("SIMPLE_CHAIN"))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    const auto own = state.getChildWithName (parameters.state.getType());
    if (own.isValid())
        parameters.replaceState (own);

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

    // Absent des anciennes sessions : tout en série, dans l'ordre d'origine
    // (sans prévenir l'hôte : charger une session ne la modifie pas)
    routing.store (ChainRouting::Routing::fromString (state.getProperty ("routing").toString().toStdString()).pack());
}

// Point d'entrée appelé par Logic pour créer le plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChainProcessor();
}
