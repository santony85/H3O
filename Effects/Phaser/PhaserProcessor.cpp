#include "PhaserProcessor.h"
#include "PhaserEditor.h"

namespace
{
    // Durée de chaque division, en noires (comme H3O Delay). "." = pointée, "T" = triolet.
    constexpr double divisionBeats[] = {
        4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125
    };

    struct Preset
    {
        const char* name;
        int stages;            // index dans stageNames : 0 = 2 étages … 5 = 12 étages
        int shape;
        float rate;
        bool sync;
        int division;
        float depth, centre, feedback, stereoPhase, mix;
    };

    constexpr int s2 = 0, s4 = 1, s6 = 2, s8 = 3, s12 = 5;
    constexpr int sine = PhaserEngine::sine, tri = PhaserEngine::triangle;

    constexpr Preset presets[] = {
        //  nom                         étages forme Hz     sync   div  prof. centre  fb    phase  mix
        { "Classic Four-Stage",         s4,    tri,  0.5f,  false, 6,   70,   800,    0,    0,     50 },
        { "Vintage Swoosh",             s4,    sine, 0.35f, false, 6,   60,   650,    20,   0,     50 },
        { "Funk Phase",                 s4,    tri,  1.2f,  false, 6,   55,   900,    45,   0,     50 },
        { "Jet Sweep",                  s8,    tri,  0.12f, false, 6,   90,   1000,   70,   0,     50 },
        { "Vibe Swirl",                 s6,    sine, 4.5f,  false, 6,   35,   900,    15,   0,     50 },
        { "Twelve-Stage Liquid",        s12,   sine, 0.25f, false, 6,   80,   1000,   50,   0,     50 },
        { "Stereo Spin",                s6,    sine, 0.6f,  false, 6,   75,   900,    30,   180,   50 },
        { "Nasal Throat",               s8,    tri,  0.8f,  false, 6,   60,   1200,   -70,  0,     50 },
        { "Half-Note Phase (Sync)",     s4,    tri,  1.0f,  true,  1,   70,   800,    35,   0,     50 },
        { "Subtle Shimmer",             s2,    sine, 0.3f,  false, 6,   45,   1500,   0,    90,    35 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray PhaserProcessor::stageNames { "2", "4", "6", "8", "10", "12" };
const juce::StringArray PhaserProcessor::shapeNames { "Sine", "Triangle" };
const juce::StringArray PhaserProcessor::divisionNames {
    "1/1", "1/2", "1/2.", "1/4", "1/4.", "1/4T",
    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"
};

PhaserProcessor::PhaserProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    jassert (divisionNames.size() == (int) std::size (divisionBeats));
    input.attach (parameters);

    stagesParam      = modulation.get (parameters, "stages");
    shapeParam       = modulation.get (parameters, "shape");
    rateParam        = modulation.get (parameters, "rate");
    syncParam        = modulation.get (parameters, "sync");
    divisionParam    = modulation.get (parameters, "division");
    depthParam       = modulation.get (parameters, "depth");
    centreParam      = modulation.get (parameters, "centre");
    feedbackParam    = modulation.get (parameters, "feedback");
    stereoPhaseParam = modulation.get (parameters, "stereophase");
    mixParam         = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout PhaserProcessor::createParameterLayout()
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

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stages", 1 }, "Stages", stageNames, init.stages));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 }, "Shape", shapeNames, init.shape));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rate", 1 }, "Rate", skewed (0.02f, 10.0f, 0.01f, 1.0f), init.rate,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Tempo Sync", init.sync));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", divisionNames, init.division));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.depth,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "centre", 1 }, "Centre", skewed (100.0f, 4000.0f, 1.0f, 800.0f), init.centre,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (-90.0f, 90.0f, 0.1f), init.feedback,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "stereophase", 1 }, "Stereo Phase", NormalisableRange<float> (0.0f, 180.0f, 1.0f), init.stereoPhase,
        AudioParameterFloatAttributes().withLabel (juce::String::fromUTF8 ("°"))));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int PhaserProcessor::getNumPrograms() { return numPresets; }

const juce::String PhaserProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void PhaserProcessor::setCurrentProgram (int index)
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

    set ("stages", (float) p.stages);
    set ("shape", (float) p.shape);
    set ("rate", p.rate);
    set ("sync", p.sync ? 1.0f : 0.0f);
    set ("division", (float) p.division);
    set ("depth", p.depth);
    set ("centre", p.centre);
    set ("feedback", p.feedback);
    set ("stereophase", p.stereoPhase);
    set ("mix", p.mix);
}

//==============================================================================
bool PhaserProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

PhaserEngine::Settings PhaserProcessor::currentSettings() const
{
    PhaserEngine::Settings s;
    s.stages      = 2 * (1 + juce::jlimit (0, stageNames.size() - 1, (int) stagesParam->load()));
    s.shape       = juce::jlimit (0, PhaserEngine::numShapes - 1, (int) shapeParam->load());
    s.depth       = depthParam->load() * 0.01f;
    s.centreHz    = centreParam->load();
    s.feedback    = feedbackParam->load() * 0.01f;
    s.stereoPhase = stereoPhaseParam->load();
    s.mix         = mixParam->load() * 0.01f;
    return s;
}

float PhaserProcessor::computeRate()
{
    if (syncParam->load() < 0.5f)
        return rateParam->load();

    const int index = juce::jlimit (0, (int) std::size (divisionBeats) - 1, (int) divisionParam->load());
    beatsPerCycle = divisionBeats[index];
    return (float) (currentBpm.load() / 60.0 / beatsPerCycle);
}

void PhaserProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());
    engine.setRate (computeRate());
}

void PhaserProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Tempo de Logic, et position dans le morceau pour caler le balayage sur les temps
    std::optional<double> ppq;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    currentBpm.store ((float) *bpm);
            if (position->getIsPlaying())
                if (auto p = position->getPpqPosition())
                    ppq = *p;
        }

    const float rate = computeRate();
    currentRate.store (rate);
    engine.setSettings (currentSettings());
    engine.setRate (rate);

    if (syncParam->load() >= 0.5f && ppq.has_value())
        engine.setPhase (*ppq / beatsPerCycle);

    engine.process (buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples());
    shownPhase.store (engine.getPhase());
}

juce::AudioProcessorEditor* PhaserProcessor::createEditor()
{
    return new PhaserEditor (*this);
}

void PhaserProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaserProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
