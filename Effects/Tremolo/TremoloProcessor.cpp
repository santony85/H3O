#include "TremoloProcessor.h"
#include <optional>
#include "TremoloEditor.h"

namespace
{
    // Durée de chaque division, en noires (comme H3O Delay). "." = pointée, "T" = triolet.
    constexpr double divisionBeats[] = {
        4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125
    };

    struct Preset
    {
        const char* name;
        int mode, shape;
        float rate;
        bool sync;
        int division;
        float depth, smooth, stereoPhase, crossover;
    };

    constexpr int trem = TremoloEngine::tremolo, harm = TremoloEngine::harmonic, pan = TremoloEngine::autoPan;
    constexpr int sine = TremoloEngine::sine, tri = TremoloEngine::triangle, square = TremoloEngine::square,
                  sawDown = TremoloEngine::sawDown;

    constexpr Preset presets[] = {
        //  nom                           mode  forme    Hz     sync   div  prof. lisse phase  sép. Hz
        { "Surf Trem",                    trem, sine,    6.5f,  false, 6,   65,   30,   0,     650 },
        { "Brownface Harmonic",           harm, sine,    4.5f,  false, 6,   80,   30,   0,     650 },
        { "Gentle Throb",                 trem, sine,    3.0f,  false, 6,   35,   30,   0,     650 },
        { "Choppy Square",                trem, square,  7.0f,  false, 6,   90,   20,   0,     650 },
        { "Helicopter (Sync 1/16)",       trem, square,  8.0f,  true,  9,   100,  10,   0,     650 },
        { "Ping-Pong Trem (Sync 1/8)",    trem, tri,     5.0f,  true,  6,   80,   30,   180,   650 },
        { "Saw Pulse (Sync 1/8)",         trem, sawDown, 5.0f,  true,  6,   75,   15,   0,     650 },
        { "Wide Harmonic",                harm, sine,    3.5f,  false, 6,   70,   30,   90,    800 },
        { "Slow Auto-Pan",                pan,  sine,    0.25f, false, 6,   80,   30,   0,     650 },
        { "Auto-Pan (Sync 1/4)",          pan,  sine,    2.0f,  true,  3,   70,   30,   0,     650 },
        { "Fast Swirl Pan",               pan,  tri,     5.0f,  false, 6,   60,   30,   0,     650 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray TremoloProcessor::modeNames  { "Tremolo", "Harmonic", "Auto-Pan" };
const juce::StringArray TremoloProcessor::shapeNames { "Sine", "Triangle", "Square", "Saw Up", "Saw Down" };
const juce::StringArray TremoloProcessor::divisionNames {
    "1/1", "1/2", "1/2.", "1/4", "1/4.", "1/4T",
    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"
};

TremoloProcessor::TremoloProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    jassert (divisionNames.size() == (int) std::size (divisionBeats));
    input.attach (parameters);

    modeParam        = modulation.get (parameters, "mode");
    shapeParam       = modulation.get (parameters, "shape");
    rateParam        = modulation.get (parameters, "rate");
    syncParam        = modulation.get (parameters, "sync");
    divisionParam    = modulation.get (parameters, "division");
    depthParam       = modulation.get (parameters, "depth");
    smoothParam      = modulation.get (parameters, "smooth");
    stereoPhaseParam = modulation.get (parameters, "stereophase");
    crossoverParam   = modulation.get (parameters, "crossover");
}

juce::AudioProcessorValueTreeState::ParameterLayout TremoloProcessor::createParameterLayout()
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

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modeNames, init.mode));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 }, "Shape", shapeNames, init.shape));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rate", 1 }, "Rate", skewed (0.05f, 20.0f, 0.01f, 4.0f), init.rate,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Tempo Sync", init.sync));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", divisionNames, init.division));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.depth,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "smooth", 1 }, "Smooth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.smooth,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "stereophase", 1 }, "Stereo Phase", NormalisableRange<float> (0.0f, 180.0f, 1.0f), init.stereoPhase,
        AudioParameterFloatAttributes().withLabel (juce::String::fromUTF8 ("°"))));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "crossover", 1 }, "Crossover", skewed (200.0f, 2000.0f, 1.0f, 650.0f), init.crossover,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    return layout;
}

//==============================================================================
int TremoloProcessor::getNumPrograms() { return numPresets; }

const juce::String TremoloProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void TremoloProcessor::setCurrentProgram (int index)
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

    set ("mode", (float) p.mode);
    set ("shape", (float) p.shape);
    set ("rate", p.rate);
    set ("sync", p.sync ? 1.0f : 0.0f);
    set ("division", (float) p.division);
    set ("depth", p.depth);
    set ("smooth", p.smooth);
    set ("stereophase", p.stereoPhase);
    set ("crossover", p.crossover);
}

//==============================================================================
bool TremoloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

TremoloEngine::Settings TremoloProcessor::currentSettings() const
{
    TremoloEngine::Settings s;
    s.mode        = juce::jlimit (0, TremoloEngine::numModes - 1, (int) modeParam->load());
    s.shape       = juce::jlimit (0, TremoloEngine::numShapes - 1, (int) shapeParam->load());
    s.depth       = depthParam->load() * 0.01f;
    s.smooth      = smoothParam->load() * 0.01f;
    s.stereoPhase = stereoPhaseParam->load();
    s.crossoverHz = crossoverParam->load();
    return s;
}

float TremoloProcessor::computeRate()
{
    if (syncParam->load() < 0.5f)
        return rateParam->load();

    const int index = juce::jlimit (0, (int) std::size (divisionBeats) - 1, (int) divisionParam->load());
    beatsPerCycle = divisionBeats[index];
    return (float) (currentBpm.load() / 60.0 / beatsPerCycle);
}

void TremoloProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());
    engine.setRate (computeRate());
}

void TremoloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Tempo de Logic, et position dans le morceau pour caler l'onde sur les temps
    std::optional<double> ppq;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    currentBpm.store ((float) *bpm);
            if (position->getIsPlaying())
                if (auto p = position->getPpqPosition()) ppq = *p;
        }

    const float rate = computeRate();
    currentRate.store (rate);
    engine.setSettings (currentSettings());
    engine.setRate (rate);

    // Calé sur le tempo et en lecture : l'onde repart pile sur les temps (sinon elle tourne librement)
    if (syncParam->load() >= 0.5f && ppq.has_value())
        engine.setPhase (*ppq / beatsPerCycle);

    engine.process (buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples());
    shownPhase.store (engine.getPhase());
}

juce::AudioProcessorEditor* TremoloProcessor::createEditor()
{
    return new TremoloEditor (*this);
}

void TremoloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TremoloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
