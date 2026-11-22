#include "FlangerProcessor.h"
#include "FlangerEditor.h"

namespace
{
    // Durée de chaque division, en noires (comme H3O Delay). "." = pointée, "T" = triolet.
    constexpr double divisionBeats[] = {
        4.0, 2.0, 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125
    };

    struct Preset
    {
        const char* name;
        int mode;              // 0 = Classic, 1 = Through-Zero
        int shape;
        float rate;
        bool sync;
        int division;
        float depth, manual, feedback, stereoPhase, mix;
    };

    constexpr int cl = FlangerEngine::classic, tz = FlangerEngine::throughZero;
    constexpr int sine = FlangerEngine::sine, tri = FlangerEngine::triangle;

    constexpr Preset presets[] = {
        //  nom                               mode forme Hz     sync   div  prof. manual fb    phase  mix
        { "Classic Jet",                      cl,  tri,  0.15f, false, 6,   85,   2.0f,  70,   0,     50 },
        { "Through-Zero Tape",                tz,  sine, 0.12f, false, 6,   100,  3.0f,  0,    0,     50 },
        { "Metallic Comb",                    cl,  tri,  0.4f,  false, 6,   60,   1.0f,  85,   0,     50 },
        { "Negative Hollow",                  cl,  tri,  0.25f, false, 6,   70,   2.5f,  -75,  0,     50 },
        { "Subtle Sweep",                     cl,  sine, 0.3f,  false, 6,   50,   3.0f,  30,   90,    35 },
        { "Stereo Wide Flange",               cl,  tri,  0.2f,  false, 6,   75,   2.0f,  55,   180,   50 },
        { "Fast Warble",                      cl,  sine, 3.5f,  false, 6,   25,   1.5f,  20,   0,     50 },
        { "Quarter-Note Jet (Sync)",          cl,  tri,  1.0f,  true,  3,   85,   2.0f,  70,   0,     50 },
        { "Through-Zero Swoosh (Sync 1/1)",   tz,  tri,  1.0f,  true,  0,   100,  2.0f,  30,   0,     50 },
        { "Static Comb",                      cl,  tri,  0.02f, false, 6,   0,    4.0f,  60,   0,     50 },
        { "Deep Space Flange",                tz,  sine, 0.06f, false, 6,   100,  5.0f,  50,   120,   50 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray FlangerProcessor::modeNames { "Classic", "Through-Zero" };
const juce::StringArray FlangerProcessor::shapeNames { "Sine", "Triangle" };
const juce::StringArray FlangerProcessor::divisionNames {
    "1/1", "1/2", "1/2.", "1/4", "1/4.", "1/4T",
    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"
};

FlangerProcessor::FlangerProcessor()
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
    manualParam      = modulation.get (parameters, "manual");
    feedbackParam    = modulation.get (parameters, "feedback");
    stereoPhaseParam = modulation.get (parameters, "stereophase");
    mixParam         = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout FlangerProcessor::createParameterLayout()
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
        ParameterID { "rate", 1 }, "Rate", skewed (0.02f, 10.0f, 0.01f, 1.0f), init.rate,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Tempo Sync", init.sync));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", divisionNames, init.division));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.depth,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "manual", 1 }, "Manual", skewed (0.1f, 10.0f, 0.01f, 2.0f), init.manual,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (-95.0f, 95.0f, 0.1f), init.feedback,
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
int FlangerProcessor::getNumPrograms() { return numPresets; }

const juce::String FlangerProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void FlangerProcessor::setCurrentProgram (int index)
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
    set ("manual", p.manual);
    set ("feedback", p.feedback);
    set ("stereophase", p.stereoPhase);
    set ("mix", p.mix);
}

//==============================================================================
bool FlangerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

FlangerEngine::Settings FlangerProcessor::currentSettings() const
{
    FlangerEngine::Settings s;
    s.mode        = juce::jlimit (0, FlangerEngine::numModes - 1, (int) modeParam->load());
    s.shape       = juce::jlimit (0, FlangerEngine::numShapes - 1, (int) shapeParam->load());
    s.depth       = depthParam->load() * 0.01f;
    s.manualMs    = manualParam->load();
    s.feedback    = feedbackParam->load() * 0.01f;
    s.stereoPhase = stereoPhaseParam->load();
    s.mix         = mixParam->load() * 0.01f;
    return s;
}

float FlangerProcessor::computeRate()
{
    if (syncParam->load() < 0.5f)
        return rateParam->load();

    const int index = juce::jlimit (0, (int) std::size (divisionBeats) - 1, (int) divisionParam->load());
    beatsPerCycle = divisionBeats[index];
    return (float) (currentBpm.load() / 60.0 / beatsPerCycle);
}

void FlangerProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());
    engine.setRate (computeRate());
}

void FlangerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

juce::AudioProcessorEditor* FlangerProcessor::createEditor()
{
    return new FlangerEditor (*this);
}

void FlangerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FlangerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
