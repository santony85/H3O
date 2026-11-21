#include "CompressorProcessor.h"
#include "CompressorEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        int style;
        float threshold, ratio, knee, attack, release;
        bool autoRelease;
        float sidechain, makeup;
        bool autoMakeup;
        float mix;
    };

    constexpr int clean = CompressorEngine::clean, opto = CompressorEngine::opto, punch = CompressorEngine::punch;

    constexpr Preset presets[] = {
        //  nom                         style   seuil  ratio genou  att.   rel.  auto   SC Hz  gain  auto   mix
        { "Clean Sustain",              clean,  -28,   4,    6,     15,    200,  true,   80,   0,    true,  100 },
        { "Country Squash",             punch,  -32,   8,    3,     1,     120,  false,  60,   0,    true,  100 },
        { "Funk Snap",                  punch,  -24,   4,    3,     8,     80,   false,  100,  0,    true,  100 },
        { "Smooth Opto",                opto,   -26,   3,    6,     10,    300,  true,   60,   0,    true,  100 },
        { "Parallel Punch",             punch,  -36,   10,   0,     3,     100,  false,  80,   0,    true,  45 },
        { "Lead Sustainer",             clean,  -34,   6,    9,     20,    400,  true,   100,  0,    true,  100 },
        { "Gentle Leveler",             opto,   -20,   2,    12,    30,    500,  true,   40,   0,    true,  100 },
        { "Glassy Clean (Parallel)",    clean,  -40,   4,    6,     5,     150,  true,   80,   0,    true,  50 },
        { "Brickwall Limiter",          clean,  -6,    20,   0,     0.5f,  60,   false,  20,   0,    false, 100 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

CompressorProcessor::CompressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    styleParam       = modulation.get (parameters, "style");
    thresholdParam   = modulation.get (parameters, "threshold");
    ratioParam       = modulation.get (parameters, "ratio");
    kneeParam        = modulation.get (parameters, "knee");
    attackParam      = modulation.get (parameters, "attack");
    releaseParam     = modulation.get (parameters, "release");
    autoReleaseParam = modulation.get (parameters, "autorelease");
    sidechainParam   = modulation.get (parameters, "sidechain");
    makeupParam      = modulation.get (parameters, "makeup");
    autoMakeupParam  = modulation.get (parameters, "automakeup");
    mixParam         = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout CompressorProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    auto skewed = [] (float lo, float hi, float step, float centre)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    };

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "style", 1 }, "Style", StringArray { "Clean", "Opto", "Punch" }, init.style));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), init.threshold,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ratio", 1 }, "Ratio", skewed (1.0f, 20.0f, 0.01f, 4.0f), init.ratio,
        AudioParameterFloatAttributes().withLabel (":1")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "knee", 1 }, "Knee", NormalisableRange<float> (0.0f, 24.0f, 0.1f), init.knee,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Attack", skewed (0.1f, 100.0f, 0.01f, 10.0f), init.attack,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release", skewed (10.0f, 2000.0f, 1.0f, 200.0f), init.release,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "autorelease", 1 }, "Auto Release", init.autoRelease));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sidechain", 1 }, "Sidechain HPF", skewed (20.0f, 500.0f, 1.0f, 100.0f), init.sidechain,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "makeup", 1 }, "Makeup", NormalisableRange<float> (-12.0f, 30.0f, 0.1f), init.makeup,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "automakeup", 1 }, "Auto Makeup", init.autoMakeup));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int CompressorProcessor::getNumPrograms() { return numPresets; }

const juce::String CompressorProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void CompressorProcessor::setCurrentProgram (int index)
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

    set ("style", (float) p.style);
    set ("threshold", p.threshold);
    set ("ratio", p.ratio);
    set ("knee", p.knee);
    set ("attack", p.attack);
    set ("release", p.release);
    set ("autorelease", p.autoRelease ? 1.0f : 0.0f);
    set ("sidechain", p.sidechain);
    set ("makeup", p.makeup);
    set ("automakeup", p.autoMakeup ? 1.0f : 0.0f);
    set ("mix", p.mix);
}

//==============================================================================
bool CompressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

CompressorEngine::Settings CompressorProcessor::currentSettings() const
{
    CompressorEngine::Settings s;
    s.style       = juce::jlimit (0, CompressorEngine::numStyles - 1, (int) styleParam->load());
    s.thresholdDb = thresholdParam->load();
    s.ratio       = ratioParam->load();
    s.kneeDb      = kneeParam->load();
    s.attackMs    = attackParam->load();
    s.releaseMs   = releaseParam->load();
    s.autoRelease = autoReleaseParam->load() >= 0.5f;
    s.sidechainHz = sidechainParam->load();
    s.makeupDb    = makeupParam->load();
    s.autoMakeup  = autoMakeupParam->load() >= 0.5f;
    s.mix         = mixParam->load() * 0.01f;
    return s;
}

void CompressorProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de Simple Chain
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());
    engine.reset();
    gainReduction = 0.0f;
}

void CompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de Simple Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    engine.setSettings (currentSettings());
    engine.process (buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples());

    gainReduction = engine.getGainReduction();
    inputLevel = engine.getInputLevel();
}

juce::AudioProcessorEditor* CompressorProcessor::createEditor()
{
    return new CompressorEditor (*this);
}

void CompressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CompressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
