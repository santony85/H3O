#include "AmpProcessor.h"
#include "AmpEditor.h"

#include "NAM/activations.h"
#include "NAM/get_dsp.h"

namespace
{
    // Volume de référence quand « Normalize » est activé (même valeur que le plugin NAM officiel)
    constexpr double targetLoudnessDb = -18.0;

    juce::String dbText (float v, int) { return juce::String (v, 1) + " dB"; }
}

AmpProcessor::AmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    inputGainParam  = modulation.get (parameters, "inputgain");
    gateParam       = modulation.get (parameters, "gate");
    bassParam       = modulation.get (parameters, "bass");
    midParam        = modulation.get (parameters, "mid");
    trebleParam     = modulation.get (parameters, "treble");
    outputGainParam = modulation.get (parameters, "outputgain");
    normaliseParam  = modulation.get (parameters, "normalize");
    cabParam        = modulation.get (parameters, "cab");

    // tanh approchée : nettement plus rapide, inaudible
    nam::activations::Activation::enable_fast_tanh();
}

// Défini ici car nam::DSP n'est complet que dans ce fichier
AmpProcessor::~AmpProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AmpProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "inputgain", 1 }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gate", 1 }, "Gate", NormalisableRange<float> (-100.0f, -20.0f, 0.1f), -100.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return v <= -99.5f ? String ("Off") : String (v, 1) + " dB"; })));

    for (auto [id, name] : { std::pair { "bass", "Bass" }, { "mid", "Mid" }, { "treble", "Treble" } })
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outputgain", 1 }, "Output", NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "normalize", 1 }, "Normalize", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "cab", 1 }, "Cab", true));

    return layout;
}

//==============================================================================
bool AmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void AmpProcessor::configure (LoadedModel& m) const
{
    if (m.dsp == nullptr)
        return;

    m.modelRate = m.dsp->GetExpectedSampleRate() > 0.0 ? m.dsp->GetExpectedSampleRate() : 48000.0;
    m.resampled = ! m.dsp->SupportsArbitrarySampleRate()
               && std::abs (m.modelRate - currentSampleRate) > 0.5;

    if (m.resampled)
    {
        m.converter.prepare (currentSampleRate, m.modelRate, maxBlockSize);
        m.dsp->Reset (m.modelRate, m.converter.maxModelBlockFor (maxBlockSize));
    }
    else
    {
        m.dsp->Reset (currentSampleRate, maxBlockSize);
    }
}

void AmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    modulation.update();   // réglages + modulation de Simple Chain
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (samplesPerBlock, 32);

    mono.assign ((size_t) maxBlockSize, 0.0f);
    scratch.assign ((size_t) maxBlockSize, 0.0f);

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        if (model != nullptr)
            configure (*model);
    }

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    gate.prepare (spec);
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gate.setRelease (100.0f);

    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // Coefficients biquad dès le départ : les mises à jour suivantes n'allouent plus rien
    for (auto* f : { &bassFilter, &midFilter, &trebleFilter })
    {
        f->coefficients = Coeffs::makePeakFilter (sampleRate, 1000.0f, 0.7f, 1.0f);
        f->prepare (spec);
        f->reset();
    }
    lastBass = lastMid = lastTreble = -100.0f;
    updateFilters();

    // Baffle simulé quand aucune IR n'est chargée : coupe les graves en dessous de 90 Hz
    // et les aigus au-dessus de 5 kHz (24 dB/octave), comme un haut-parleur de guitare
    cabHighPass.coefficients = Coeffs::makeHighPass (sampleRate, 90.0f, 0.707f);
    cabLowPass1.coefficients = Coeffs::makeLowPass (sampleRate, 5000.0f, 0.54f);
    cabLowPass2.coefficients = Coeffs::makeLowPass (sampleRate, 5000.0f, 1.31f);
    for (auto* f : { &cabHighPass, &cabLowPass1, &cabLowPass2 })
    {
        f->prepare (spec);
        f->reset();
    }

    cabinet.prepare (spec);
    cabinet.reset();

    smoothedInput.reset (sampleRate, 0.02);
    smoothedOutput.reset (sampleRate, 0.02);
    smoothedInput.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    smoothedOutput.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    prepared = true;
}

void AmpProcessor::updateFilters()
{
    using Array = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto sr = currentSampleRate;

    const float bass = bassParam->load(), mid = midParam->load(), treble = trebleParam->load();

    if (! juce::exactlyEqual (bass, lastBass))
        *bassFilter.coefficients = Array::makeLowShelf (sr, 120.0f, 0.707f, juce::Decibels::decibelsToGain (bass));
    if (! juce::exactlyEqual (mid, lastMid))
        *midFilter.coefficients = Array::makePeakFilter (sr, 750.0f, 0.8f, juce::Decibels::decibelsToGain (mid));
    if (! juce::exactlyEqual (treble, lastTreble))
        *trebleFilter.coefficients = Array::makeHighShelf (sr, 3000.0f, 0.707f, juce::Decibels::decibelsToGain (treble));

    lastBass = bass;
    lastMid = mid;
    lastTreble = treble;
}

void AmpProcessor::processModel (float* data, int n)
{
    // Si l'interface est en train d'échanger le modèle, ce bloc passe sans ampli (quelques µs, très rare)
    const juce::SpinLock::ScopedTryLockType lock (modelLock);
    if (! lock.isLocked() || model == nullptr || model->dsp == nullptr)
        return;

    auto& m = *model;

    if (m.resampled)
    {
        m.converter.process (data, n, [&m] (const float* in, float* out, int count)
        {
            auto* inPtr = const_cast<float*> (in);
            m.dsp->process (&inPtr, &out, count);
        });
    }
    else
    {
        float* out = scratch.data();
        m.dsp->process (&data, &out, n);
        std::copy (out, out + n, data);
    }

    if (normaliseParam->load() >= 0.5f && m.hasLoudness)
        juce::FloatVectorOperations::multiply (data, m.normaliseGain, n);
}

void AmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de Simple Chain
    juce::ScopedNoDenormals noDenormals;

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, getTotalNumInputChannels());

    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    updateFilters();

    const float gateDb  = gateParam->load();
    const bool gateOn   = gateDb > -99.5f;
    const bool cabOn    = cabParam->load() >= 0.5f;
    const bool useIR    = cabOn && irLoaded.load();
    gate.setThreshold (gateDb);

    smoothedInput.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    smoothedOutput.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    // Un ampli est mono : on traite par tranches de la taille préparée
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - start);
        float* x = mono.data();

        for (int i = 0; i < n; ++i)
        {
            const float l = left[start + i];
            const float r = right != nullptr ? right[start + i] : l;
            x[i] = 0.5f * (l + r) * smoothedInput.getNextValue();
        }

        if (gateOn)
            for (int i = 0; i < n; ++i)
                x[i] = gate.processSample (0, x[i]);

        processModel (x, n);

        for (int i = 0; i < n; ++i)
            x[i] = trebleFilter.processSample (midFilter.processSample (bassFilter.processSample (x[i])));

        if (useIR)
        {
            juce::dsp::AudioBlock<float> block (&x, 1, (size_t) n);
            cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else if (cabOn)
        {
            for (int i = 0; i < n; ++i)
                x[i] = cabLowPass2.processSample (cabLowPass1.processSample (cabHighPass.processSample (x[i])));
        }

        for (int i = 0; i < n; ++i)
        {
            const float y = x[i] * smoothedOutput.getNextValue();
            left[start + i] = y;
            if (right != nullptr)
                right[start + i] = y;
        }
    }
}

//==============================================================================
juce::String AmpProcessor::loadModel (const juce::File& file)
{
    lastModelError = tryLoadModel (file);

    if (lastModelError.isNotEmpty())
        std::fprintf (stderr, "Simple Amp: %s\n", lastModelError.toRawUTF8());

    return lastModelError;
}

juce::String AmpProcessor::tryLoadModel (const juce::File& file)
{
    if (! file.existsAsFile())
        return "File not found: " + file.getFullPathName();

    auto loaded = std::make_unique<LoadedModel>();

    try
    {
        loaded->dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
    }
    catch (const std::exception& e)
    {
        return "Cannot read this model: " + juce::String (e.what());
    }

    if (loaded->dsp == nullptr)
        return "Cannot read this model.";

    loaded->name = file.getFileNameWithoutExtension();
    loaded->hasLoudness = loaded->dsp->HasLoudness();
    if (loaded->hasLoudness)
        loaded->normaliseGain = (float) juce::Decibels::decibelsToGain (targetLoudnessDb - loaded->dsp->GetLoudness());

    configure (*loaded);   // le calcul lourd (allocation, préchauffage) se fait ici, hors du fil audio

    // L'ancien modèle est détruit après avoir rendu le verrou
    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, loaded);
    }

    modelFile = file;
    return {};
}

void AmpProcessor::clearModel()
{
    std::unique_ptr<LoadedModel> old;
    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (model, old);
    }
    modelFile = juce::File();
    lastModelError.clear();
}

juce::String AmpProcessor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return "File not found: " + file.getFullPathName();

    // Chargé en arrière-plan par JUCE, puis échangé sans coupure
    cabinet.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::yes, 0,
                                 juce::dsp::Convolution::Normalise::yes);
    irFile = file;
    irLoaded = true;
    return {};
}

void AmpProcessor::clearImpulseResponse()
{
    irLoaded = false;
    irFile = juce::File();
}

AmpProcessor::ModelInfo AmpProcessor::getModelInfo() const
{
    ModelInfo info;
    const juce::SpinLock::ScopedLockType lock (modelLock);

    if (model != nullptr)
    {
        info.name        = model->name;
        info.sampleRate  = model->modelRate;
        info.resampled   = model->resampled;
        info.hasLoudness = model->hasLoudness;
    }

    return info;
}

//==============================================================================
juce::AudioProcessorEditor* AmpProcessor::createEditor()
{
    return new AmpEditor (*this);
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("modelPath", modelFile.getFullPathName(), nullptr);
    state.setProperty ("irPath", irFile.getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const juce::String modelPath = state.getProperty ("modelPath").toString();
    const juce::String irPath    = state.getProperty ("irPath").toString();
    parameters.replaceState (state);

    // Recharge les fichiers de la session (ignorés s'ils ont été déplacés)
    if (modelPath.isEmpty())
        clearModel();
    else if (juce::File (modelPath) != modelFile)
        loadModel (juce::File (modelPath));

    if (irPath.isEmpty())
        clearImpulseResponse();
    else if (juce::File (irPath) != irFile)
        loadImpulseResponse (juce::File (irPath));
}
