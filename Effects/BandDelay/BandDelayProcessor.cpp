#include "BandDelayProcessor.h"
#include "BandDelayEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float bands, time, low, high, resonance, pattern, feedback, spread, mix;
    };

    // pattern : 0 = Rising, 1 = Falling, 2 = Alternating
    constexpr Preset presets[] = {
        //  nom                      bandes  ms     bas Hz  haut Hz  réso %  ordre  fb %  spread %  mix %
        { "Band Delay Rising",          4,    800,   300,    3000,     60,     0,    20,    80,      35 },
        { "Falling Colors",             6,   1200,   250,    4000,     70,     1,    25,   100,      35 },
        { "Wah Echoes",                 3,    600,   500,    1500,     90,     2,    45,    60,      40 },
        { "Telephone Taps",             2,    400,  1200,    2500,     40,     0,    30,   100,      35 },
        { "Resonant Ping-Pong",         8,   1600,   200,    6000,     85,     2,    35,   100,      35 },
        { "Dark Dub Bands",             4,   1500,   120,     900,     50,     1,    55,    80,      40 },
        { "Sparkle Taps",               5,    700,  2000,    8000,     60,     0,    20,   100,      30 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

BandDelayProcessor::BandDelayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    bandsParam     = modulation.get (parameters, "bands");
    timeParam      = modulation.get (parameters, "time");
    lowParam       = modulation.get (parameters, "low");
    highParam      = modulation.get (parameters, "high");
    resonanceParam = modulation.get (parameters, "resonance");
    patternParam   = modulation.get (parameters, "pattern");
    spreadParam    = modulation.get (parameters, "spread");
    feedbackParam  = modulation.get (parameters, "feedback");
    mixParam       = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout BandDelayProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    auto percent = [&layout] (const char* id, const char* name, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, hi, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%")));
    };

    auto hertz = [&layout] (const char* id, const char* name, float lo, float hi, float def)
    {
        NormalisableRange<float> range (lo, hi, 1.0f);
        range.setSkewForCentre (std::sqrt (lo * hi));   // échelle musicale : le milieu du bouton = milieu en octaves
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def, AudioParameterFloatAttributes().withLabel ("Hz")));
    };

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "bands", 1 }, "Bands", 1, BandDelayEngine::maxBands, (int) init.bands));

    NormalisableRange<float> timeRange (20.0f, maxTimeMs, 1.0f);
    timeRange.setSkewForCentre (500.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "time", 1 }, "Time", timeRange, init.time,
        AudioParameterFloatAttributes().withLabel ("ms")));

    hertz ("low",  "Low",  80.0f,  2000.0f, init.low);
    hertz ("high", "High", 500.0f, 10000.0f, init.high);
    percent ("resonance", "Resonance", 100.0f, init.resonance);

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "pattern", 1 }, "Pattern", StringArray { "Rising", "Falling", "Alternating" }, (int) init.pattern));

    percent ("feedback", "Feedback", 90.0f, init.feedback);
    percent ("spread", "Spread", 100.0f, init.spread);
    percent ("mix", "Mix", 100.0f, init.mix);

    return layout;
}

BandDelayEngine::Settings BandDelayProcessor::currentSettings() const noexcept
{
    BandDelayEngine::Settings s;
    s.bands     = (int) bandsParam->load();
    s.timeMs    = timeParam->load();
    s.lowHz     = lowParam->load();
    s.highHz    = highParam->load();
    s.resonance = resonanceParam->load() * 0.01f;
    s.pattern   = (BandDelayEngine::Pattern) juce::jlimit (0, 2, (int) patternParam->load());
    s.spread    = spreadParam->load() * 0.01f;
    return s;
}

//==============================================================================
int BandDelayProcessor::getNumPrograms() { return numPresets; }

const juce::String BandDelayProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void BandDelayProcessor::setCurrentProgram (int index)
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

    set ("bands", p.bands);
    set ("time", p.time);
    set ("low", p.low);
    set ("high", p.high);
    set ("resonance", p.resonance);
    set ("pattern", p.pattern);
    set ("feedback", p.feedback);
    set ("spread", p.spread);
    set ("mix", p.mix);
}

//==============================================================================
bool BandDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void BandDelayProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    currentSampleRate = sampleRate;

    engine.prepare (sampleRate, maxTimeMs * 0.001 + 0.05);
    engine.setSettings (currentSettings());

    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void BandDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc ; le moteur fait glisser les échos en douceur
    engine.setSettings (currentSettings());
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        float wetL, wetR;
        engine.process (0.5f * (dryL + dryR), smoothedFeedback.getNextValue(), wetL, wetR);

        const float mix = smoothedMix.getNextValue();

        if (right != nullptr)
        {
            left[i]  = dryL * (1.0f - mix) + wetL * mix;
            right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
        else
        {
            left[i] = dryL * (1.0f - mix) + 0.5f * (wetL + wetR) * mix;
        }
    }
}

juce::AudioProcessorEditor* BandDelayProcessor::createEditor()
{
    return new BandDelayEditor (*this);
}

void BandDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BandDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
