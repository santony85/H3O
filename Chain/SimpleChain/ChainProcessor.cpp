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
                      std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "reverseon", 1 }, "Reverse On", false),
                      std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "choruson", 1 }, "Chorus On", false),
                      std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delayon", 1 }, "Delay On", true),
                  })
{
    harmonizerOn = parameters.getRawParameterValue ("harmonizeron");
    ampOn        = parameters.getRawParameterValue ("ampon");
    reverseOn    = parameters.getRawParameterValue ("reverseon");
    chorusOn     = parameters.getRawParameterValue ("choruson");
    delayOn      = parameters.getRawParameterValue ("delayon");

    // C'est la chaîne qui choisit l'entrée ; les effets reçoivent déjà de la stéréo
    input.attach (parameters);
    harmonizer.input.setInsideChain();
    amp.input.setInsideChain();
    reverse.input.setInsideChain();
    chorus.input.setInsideChain();
    delay.input.setInsideChain();
}

bool ChainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono (guitare) ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void ChainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Les effets internes travaillent toujours en stéréo
    for (juce::AudioProcessor* fx : { (juce::AudioProcessor*) &amp, (juce::AudioProcessor*) &harmonizer, (juce::AudioProcessor*) &reverse, (juce::AudioProcessor*) &chorus, (juce::AudioProcessor*) &delay })
    {
        // Pas de tête de lecture hors de processBlock (voir plus bas)
        fx->setPlayHead (nullptr);
        fx->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        fx->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void ChainProcessor::releaseResources()
{
    harmonizer.releaseResources();
    amp.releaseResources();
    reverse.releaseResources();
    chorus.releaseResources();
    delay.releaseResources();
}

double ChainProcessor::getTailLengthSeconds() const
{
    return amp.getTailLengthSeconds() + harmonizer.getTailLengthSeconds() + reverse.getTailLengthSeconds() + chorus.getTailLengthSeconds() + delay.getTailLengthSeconds();
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
    harmonizer.setPlayHead (playHead);
    amp.setPlayHead (playHead);
    reverse.setPlayHead (playHead);
    chorus.setPlayHead (playHead);
    delay.setPlayHead (playHead);

    if (isAmpOn())
        amp.processBlock (buffer, midi);

    if (isHarmonizerOn())
        harmonizer.processBlock (buffer, midi);

    if (isReverseOn())
        reverse.processBlock (buffer, midi);

    if (isChorusOn())
        chorus.processBlock (buffer, midi);

    if (isDelayOn())
        delay.processBlock (buffer, midi);

    harmonizer.setPlayHead (nullptr);
    amp.setPlayHead (nullptr);
    reverse.setPlayHead (nullptr);
    chorus.setPlayHead (nullptr);
    delay.setPlayHead (nullptr);
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

    juce::MemoryBlock ampState, harmonizerState, reverseState, chorusState, delayState;
    amp.getStateInformation (ampState);
    harmonizer.getStateInformation (harmonizerState);
    reverse.getStateInformation (reverseState);
    chorus.getStateInformation (chorusState);
    delay.getStateInformation (delayState);

    state.setProperty ("harmonizer", harmonizerState.toBase64Encoding(), nullptr);
    state.setProperty ("amp", ampState.toBase64Encoding(), nullptr);
    state.setProperty ("reverse", reverseState.toBase64Encoding(), nullptr);
    state.setProperty ("chorus", chorusState.toBase64Encoding(), nullptr);
    state.setProperty ("delay", delayState.toBase64Encoding(), nullptr);

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
    restore ("amp", amp);
    restore ("reverse", reverse);   // absent des anciennes sessions : réglages d'usine
    restore ("chorus", chorus);   // absent des anciennes sessions : le chorus garde ses réglages d'usine
    restore ("delay", delay);
}

// Point d'entrée appelé par Logic pour créer le plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChainProcessor();
}
