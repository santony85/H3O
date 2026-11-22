#include "DelayProcessor.h"
#include "DelayEditor.h"

namespace
{
    // Durée de chaque division, en noires (1.0 = une noire). "." = pointée, "T" = triolet.
    constexpr float divisionBeats[] = {
        4.0f,                 // 1/1
        2.0f,                 // 1/2
        3.0f,                 // 1/2.
        1.0f,                 // 1/4
        1.5f,                 // 1/4.
        2.0f / 3.0f,          // 1/4T
        0.5f,                 // 1/8
        0.75f,                // 1/8.
        1.0f / 3.0f,          // 1/8T
        0.25f,                // 1/16
        0.375f,               // 1/16.
        1.0f / 6.0f,          // 1/16T
        0.125f                // 1/32
    };

    // Presets d'usine. Temps et filtres repris des presets « H3000 Legacy » installés
    // avec ton H3000 Factory (indiqués en commentaire) ; feedback et mix ajustés à
    // l'oreille pour un seul délai, car le H3000 les obtient en câblant ses blocs.
    struct Preset
    {
        const char* name;
        float timeMs, feedback, mix;
        bool sync;
        int division;          // index dans divisionNames (utilisé si sync)
        float lowCut, highCut;
        bool pingPong;
    };

    constexpr Preset presets[] = {
        //  nom                        ms   fb %  mix %  sync  div  low   high   ping
        { "Default",                  350,  40,   35,  false,  6,  100,  6000, false },
        // 535-ANALOG DELAYS : 160 ms, répétitions sombres comme un délai analogique
        { "Analog Delays",            160,  45,   35,  false,  6,  120,  3500, false },
        // 521-MICROPITCH-SLAP : slapback de 121 ms, coupé à 8 kHz
        { "Slapback",                 121,  10,   30,  false,  6,   80,  8000, false },
        // 406-LASER ECHO : écho très court et très réinjecté, sonorité métallique
        { "Laser Echo",                50,  72,   35,  false,  6,  300, 20000, false },
        // 549-SPACE ECHO : 195 ms, beaucoup de répétitions qui s'assombrissent
        { "Space Echo",               195,  60,   35,  false,  6,  200,  3000, false },
        // 284-LONG LEAD PING : 550 ms en ping-pong pour les solos
        { "Long Lead Ping",           550,  45,   30,  false,  6,  150,  5000, true  },
        // 497-SMOOTH DELAYS : 475 ms, discret, derrière le son
        { "Smooth Delays",            475,  40,   25,  false,  6,  150,  6000, false },
        // 499-KAMIKAZE DELAYS : 400 ms, feedback poussé, coupé à 5 kHz
        { "Kamikaze Delays",          400,  78,   40,  false,  6,  100,  5000, false },
        // 263-UnderwaterDelay : 320 ms, très étouffé
        { "Underwater Delay",         320,  50,   40,  false,  6,  100,  1200, false },
        // Synchronisés sur le tempo de Logic
        { "Dotted Eighth (Sync)",     350,  40,   30,  true,   7,  150,  6000, false },
        { "Quarter Ping-Pong (Sync)", 350,  45,   30,  true,   3,  150,  5000, true  },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray DelayProcessor::divisionNames {
    "1/1", "1/2", "1/2.", "1/4", "1/4.", "1/4T",
    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"
};

DelayProcessor::DelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    timeParam     = modulation.get (parameters, "time");
    feedbackParam = modulation.get (parameters, "feedback");
    mixParam      = modulation.get (parameters, "mix");
    syncParam     = modulation.get (parameters, "sync");
    divisionParam = modulation.get (parameters, "division");
    lowCutParam   = modulation.get (parameters, "lowcut");
    highCutParam  = modulation.get (parameters, "highcut");
    pingPongParam = modulation.get (parameters, "pingpong");

    jassert (divisionNames.size() == (int) std::size (divisionBeats));
}

juce::AudioProcessorValueTreeState::ParameterLayout DelayProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "time", 1 }, "Time",
        NormalisableRange<float> (1.0f, 2000.0f, 0.1f, 0.4f), 350.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback",
        NormalisableRange<float> (0.0f, 95.0f, 0.1f), 40.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "sync", 1 }, "Sync", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "division", 1 }, "Division", divisionNames, 6));  // 1/8 par défaut (= preset Default)

    // Échelle logarithmique : le milieu de course du bouton tombe sur la valeur "centre"
    NormalisableRange<float> lowCutRange (20.0f, 2000.0f, 1.0f);
    lowCutRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lowcut", 1 }, "Low Cut", lowCutRange, 100.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    NormalisableRange<float> highCutRange (1000.0f, 20000.0f, 1.0f);
    highCutRange.setSkewForCentre (5000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "highcut", 1 }, "High Cut", highCutRange, 6000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "pingpong", 1 }, "Ping-Pong", false));

    return layout;
}

void DelayProcessor::updateFilters()
{
    // Reste sous la fréquence de Nyquist (moitié de la fréquence d'échantillonnage)
    const float maxCutoff = 0.45f * (float) currentSampleRate;

    lowCutFilter.setCutoffFrequency  (juce::jlimit (20.0f, maxCutoff, lowCutParam->load()));
    highCutFilter.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, highCutParam->load()));
}

float DelayProcessor::computeTargetDelayMs()
{
    // Récupère le tempo de Logic à chaque bloc (il peut changer en cours de morceau)
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    currentBpm.store ((float) *bpm);

    float delayMs = timeParam->load();

    if (syncParam->load() >= 0.5f)
    {
        const int index = juce::jlimit (0, (int) std::size (divisionBeats) - 1,
                                        (int) divisionParam->load());
        const float msPerBeat = 60000.0f / currentBpm.load();
        delayMs = divisionBeats[index] * msPerBeat;
    }

    delayMs = juce::jlimit (1.0f, (float) (maxDelaySeconds * 1000.0), delayMs);
    currentDelayMs.store (delayMs);
    return delayMs;
}

bool DelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void DelayProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    modulation.update();   // réglages + modulation de H3O Chain
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock,
                                  (juce::uint32) getTotalNumOutputChannels() };
    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelaySeconds * sampleRate) + 1);
    delayLine.reset();

    lowCutFilter.prepare (spec);
    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.prepare (spec);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    updateFilters();
    lowCutFilter.reset();
    highCutFilter.reset();

    smoothedDelaySamples.reset (sampleRate, 0.05);  // 50 ms de lissage
    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);

    smoothedDelaySamples.setCurrentAndTargetValue (computeTargetDelayMs() * 0.001f * (float) sampleRate);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void DelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, getTotalNumInputChannels());

    smoothedDelaySamples.setTargetValue (computeTargetDelayMs() * 0.001f * (float) currentSampleRate);
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    updateFilters();

    // Optimisation 3 : les pointeurs des canaux sont récupérés une seule fois par bloc
    // (sortie stéréo garantie par isBusesLayoutSupported)
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    float* channelData[2] = {};
    for (int ch = 0; ch < numChannels; ++ch)
        channelData[ch] = buffer.getWritePointer (ch);

    const bool smoothing = smoothedDelaySamples.isSmoothing()
                        || smoothedFeedback.isSmoothing()
                        || smoothedMix.isSmoothing();

    // Ping-pong : les deux canaux de sortie sont nécessaires
    const bool pingPong = pingPongParam->load() >= 0.5f && numChannels == 2;

    if (pingPong)
    {
        // Les deux canaux doivent être traités ensemble à chaque échantillon,
        // car chacun alimente l'autre : boucle échantillon par échantillon.
        // (getNextValue() ne coûte presque rien quand aucun lissage n'est en cours)
        float* left  = channelData[0];
        float* right = channelData[1];

        for (int i = 0; i < numSamples; ++i)
        {
            const float delaySamples = smoothedDelaySamples.getNextValue();
            const float feedback     = smoothedFeedback.getNextValue();
            const float mix          = smoothedMix.getNextValue();

            const float dryL = left[i];
            const float dryR = right[i];

            float wetL = delayLine.popSample (0, delaySamples);
            float wetR = delayLine.popSample (1, delaySamples);
            wetL = highCutFilter.processSample (0, lowCutFilter.processSample (0, wetL));
            wetR = highCutFilter.processSample (1, lowCutFilter.processSample (1, wetR));

            // L'entrée (gauche + droite) n'entre que par la gauche.
            // La sortie gauche repart à droite, la sortie droite repart à gauche :
            // les échos rebondissent G → D → G → D…
            delayLine.pushSample (0, 0.5f * (dryL + dryR) + wetR * feedback);
            delayLine.pushSample (1, wetL * feedback);

            left[i]  = dryL * (1.0f - mix) + wetL * mix;
            right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
        return;
    }

    if (! smoothing)
    {
        // Optimisation 4 : cas courant, aucun bouton ne bouge.
        // Les valeurs sont constantes : on les lit une fois pour tout le bloc,
        // et on traite chaque canal d'un trait (accès mémoire contigus).
        const float feedback = smoothedFeedback.getCurrentValue();
        const float mix      = smoothedMix.getCurrentValue();
        const float dryGain  = 1.0f - mix;

        delayLine.setDelay (smoothedDelaySamples.getCurrentValue());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channelData[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const float dry = data[i];
                float wet = delayLine.popSample (ch);   // utilise le délai fixé ci-dessus
                wet = highCutFilter.processSample (ch, lowCutFilter.processSample (ch, wet));
                delayLine.pushSample (ch, dry + wet * feedback);
                data[i] = dry * dryGain + wet * mix;
            }
        }
        return;
    }

    // Lissage en cours : les valeurs changent à chaque échantillon et sont
    // partagées par les deux canaux, donc boucle échantillon par échantillon.
    for (int i = 0; i < numSamples; ++i)
    {
        const float delaySamples = smoothedDelaySamples.getNextValue();
        const float feedback     = smoothedFeedback.getNextValue();
        const float mix          = smoothedMix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channelData[ch];
            const float dry = data[i];

            float wet = delayLine.popSample (ch, delaySamples);
            wet = highCutFilter.processSample (ch, lowCutFilter.processSample (ch, wet));
            delayLine.pushSample (ch, dry + wet * feedback);

            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

juce::AudioProcessorEditor* DelayProcessor::createEditor()
{
    return new DelayEditor (*this);
}

//==============================================================================
int DelayProcessor::getNumPrograms() { return numPresets; }

const juce::String DelayProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void DelayProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets))
        return;

    currentProgram = index;
    const auto& p = presets[index];

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set ("time", p.timeMs);
    set ("feedback", p.feedback);
    set ("mix", p.mix);
    set ("sync", p.sync ? 1.0f : 0.0f);
    set ("division", (float) p.division);
    set ("lowcut", p.lowCut);
    set ("highcut", p.highCut);
    set ("pingpong", p.pingPong ? 1.0f : 0.0f);
}

void DelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
