#include "HarmonizerProcessor.h"
#include "HarmonizerEditor.h"

namespace
{
    // Presets d'usine, inspirés des presets "H3000 Legacy" installés avec ton H3000 Factory
    struct Preset
    {
        const char* name;
        float aSemi, aCents, aDelay, aLevel;   // voix A (à gauche)
        float bSemi, bCents, bDelay, bLevel;   // voix B (à droite)
        float splice, feedback, spread, mix;
        // Diatonic Shift : mode (0 = Chromatic, 1 = Diatonic), tonalité (0 = C … 11 = B),
        // gamme (index dans DiatonicScale::scales()), intervalles des voix en crans de gamme
        int mode = 0, key = 4, scale = 1, aDegrees = 2, bDegrees = 4;
    };

    constexpr Preset presets[] = {
        // Désaccord léger gauche/droite, comme 231-MICROPITCHSHIFT (+9/-9 cents, 10/25 ms)
        { "Micro-Pitch Wide",        0,  9, 10, 100,    0, -9, 25, 100,   30,  0, 100, 50 },
        // Octave + quinte, comme 102-DUAL SHIFT (+1200 / +700 cents)
        { "Dual Shift: Octave + 5th", 12, 0, 10, 100,    7,  0, 10, 100,   65,  0, 100, 45 },
        // Octave au-dessus et en dessous
        { "Octave Up + Down",        12,  0, 10, 100,  -12,  0, 10, 100,   65,  0, 100, 45 },
        // Harmonie fixe : tierce majeure + quinte
        { "Major 3rd + 5th",          4,  0, 10, 100,    7,  0, 15, 100,   50,  0,  60, 45 },
        // Octaves qui montent en cascade grâce au feedback
        { "Octave Cascade",          12,  0, 80,  90,   12,  0, 100, 90,   65, 55, 100, 40 },

        // --- Diatonic Shift : les intervalles suivent la gamme choisie ---
        // Tierce + quinte « dans la gamme » en mi mineur : l'harmonie à deux voix des solos
        { "Diatonic: 3rd + 5th (E minor)", 0, 0, 10, 100,  0, 0, 15, 90,   50, 0, 60, 45,   1, 4, 1, +2, +4 },
        // Une seule voix à la tierce, au centre : la twin guitar façon rock (la mineur)
        { "Twin Lead: 3rd Above (A minor)", 0, 0, 12, 100,  0, 0, 10, 0,   50, 0,  0, 50,   1, 9, 1, +2,  0 },
        // Tierces et sixtes en sol majeur
        { "Diatonic: 3rds & 6ths (G major)", 0, 0, 10, 100,  0, 0, 18, 85,   50, 0, 70, 45,   1, 7, 0, +2, +5 },
        // Tierce en dessous + octave au-dessus, en ré dorien
        { "Diatonic: 3rd Below + Octave (D Dorian)", 0, 0, 10, 100, 0, 0, 10, 70, 60, 0, 80, 45,  1, 2, 3, -2, +7 },
    };

    constexpr int numPresets = (int) std::size (presets);

    // +2 → "+3rd", -4 → "-5th", 7 → "+Oct", 0 → "Unison" (noms pour une gamme de 7 notes)
    juce::String intervalName (int n)
    {
        if (n == 0)
            return "Unison";

        const juce::String sign = n > 0 ? "+" : "-";
        const int a = std::abs (n);

        if (a % 7 == 0)
            return sign + (a == 7 ? "Oct" : juce::String (a / 7) + " Oct");

        return sign + juce::String (a + 1) + DiatonicScale::ordinalSuffix (a + 1);
    }

    int intervalFromName (const juce::String& text)
    {
        const auto t = text.trim();
        if (t.startsWithIgnoreCase ("uni"))
            return 0;

        const int sign = t.startsWithChar ('-') ? -1 : 1;
        const auto body = t.trimCharactersAtStart ("+-").trim();

        if (body.containsIgnoreCase ("oct"))
            return sign * 7 * juce::jmax (1, body.getIntValue());

        const int n = body.getIntValue();
        return n <= 1 ? 0 : sign * (n - 1);
    }

    juce::String voiceId (int voice, const char* name)
    {
        return juce::String (voice == 0 ? "a" : "b") + name;
    }
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    for (int v = 0; v < 2; ++v)
    {
        voiceParams[v].semitones = parameters.getRawParameterValue (voiceId (v, "semi"));
        voiceParams[v].cents     = parameters.getRawParameterValue (voiceId (v, "cents"));
        voiceParams[v].delayMs   = parameters.getRawParameterValue (voiceId (v, "delay"));
        voiceParams[v].level     = parameters.getRawParameterValue (voiceId (v, "level"));
        voiceParams[v].degrees   = parameters.getRawParameterValue (voiceId (v, "degrees"));
    }

    modeParam  = parameters.getRawParameterValue ("mode");
    keyParam   = parameters.getRawParameterValue ("key");
    scaleParam = parameters.getRawParameterValue ("scale");

    spliceParam   = parameters.getRawParameterValue ("splice");
    feedbackParam = parameters.getRawParameterValue ("feedback");
    spreadParam   = parameters.getRawParameterValue ("spread");
    mixParam      = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    // Diatonic Shift
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode", StringArray { "Chromatic", "Diatonic" }, init.mode));

    StringArray keys, scaleNames;
    for (int k = 0; k < 12; ++k)
        keys.add (DiatonicScale::noteName (k));
    for (const auto& sc : DiatonicScale::scales())
        scaleNames.add (sc.name);

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 },   "Key",   keys,       init.key));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames, init.scale));

    for (int v = 0; v < 2; ++v)
    {
        const String voice = v == 0 ? "A " : "B ";

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { voiceId (v, "semi"), 1 }, voice + "Shift", -24, 24,
            (int) (v == 0 ? init.aSemi : init.bSemi),
            AudioParameterIntAttributes().withLabel ("st")));

        // Intervalle en crans de gamme : 0 = unisson, +2 = tierce au-dessus, -4 = quinte en dessous, 7 = octave
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { voiceId (v, "degrees"), 1 }, voice + "Interval", -14, 14,
            v == 0 ? init.aDegrees : init.bDegrees,
            AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int n, int) { return intervalName (n); })
                .withValueFromStringFunction (intervalFromName)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "cents"), 1 }, voice + "Fine",
            NormalisableRange<float> (-50.0f, 50.0f, 0.1f), v == 0 ? init.aCents : init.bCents,
            AudioParameterFloatAttributes().withLabel ("ct")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "delay"), 1 }, voice + "Delay",
            NormalisableRange<float> (0.0f, (float) (maxVoiceDelaySeconds * 1000.0), 0.1f),
            v == 0 ? init.aDelay : init.bDelay,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "level"), 1 }, voice + "Level",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), v == 0 ? init.aLevel : init.bLevel,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "splice", 1 }, "Splice",
        NormalisableRange<float> (20.0f, (float) (maxSpliceSeconds * 1000.0), 0.1f), init.splice,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback",
        NormalisableRange<float> (0.0f, 90.0f, 0.1f), init.feedback,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "spread", 1 }, "Spread",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.spread,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int HarmonizerProcessor::getNumPrograms() { return numPresets; }

const juce::String HarmonizerProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void HarmonizerProcessor::setCurrentProgram (int index)
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

    set ("asemi", p.aSemi);  set ("acents", p.aCents);  set ("adelay", p.aDelay);  set ("alevel", p.aLevel);
    set ("bsemi", p.bSemi);  set ("bcents", p.bCents);  set ("bdelay", p.bDelay);  set ("blevel", p.bLevel);
    set ("splice", p.splice);
    set ("feedback", p.feedback);
    set ("spread", p.spread);
    set ("mix", p.mix);
    set ("mode", (float) p.mode);
    set ("key", (float) p.key);
    set ("scale", (float) p.scale);
    set ("adegrees", (float) p.aDegrees);
    set ("bdegrees", (float) p.bDegrees);
}

//==============================================================================
bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    shifter.prepare (sampleRate, maxVoiceDelaySeconds, maxSpliceSeconds);
    for (auto& v : voiceState)
        PitchShifter::resetVoice (v);

    const auto sr = (float) sampleRate;

    for (int v = 0; v < 2; ++v)
    {
        smoothedDelay[v].reset (sampleRate, 0.05);
        smoothedLevel[v].reset (sampleRate, 0.02);
        smoothedDelay[v].setCurrentAndTargetValue (voiceParams[v].delayMs->load() * 0.001f * sr);
        smoothedLevel[v].setCurrentAndTargetValue (voiceParams[v].level->load() * 0.01f);
        smoothedRatio[v].reset (sampleRate, 0.01);   // changement de note sans clic
        smoothedRatio[v].setCurrentAndTargetValue (1.0f);
    }

    detector.prepare (sampleRate);
    tracker.reset();
    detectedNote = -1;

    smoothedSplice.reset (sampleRate, 0.05);
    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedSpread.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);

    smoothedSplice.setCurrentAndTargetValue (spliceParam->load() * 0.001f * sr);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setCurrentAndTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);

    feedbackSample = 0.0f;
}

void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    const auto sr = (float) currentSampleRate;

    const bool diatonic = isDiatonic();
    const int key = juce::jlimit (0, 11, (int) keyParam->load());
    const auto& scale = DiatonicScale::scales()[(size_t) juce::jlimit (0, (int) DiatonicScale::scales().size() - 1,
                                                                       (int) scaleParam->load())];
    if (diatonic)
        updateDiatonicShifts (key, scale);   // avec la dernière note reconnue (ou la tonique au départ)

    auto targetRatio = [this, diatonic] (int v)
    {
        // Demi-tons et cents → rapport de fréquence (1200 cents = une octave = ×2)
        const float semis = diatonic ? (float) diatonicSemitones[v] : voiceParams[v].semitones->load();
        return std::pow (2.0f, (semis * 100.0f + voiceParams[v].cents->load()) / 1200.0f);
    };

    for (int v = 0; v < 2; ++v)
    {
        smoothedRatio[v].setTargetValue (targetRatio (v));
        if (! diatonic)
            voiceShift[v] = (int) voiceParams[v].semitones->load();

        smoothedDelay[v].setTargetValue (voiceParams[v].delayMs->load() * 0.001f * sr);
        smoothedLevel[v].setTargetValue (voiceParams[v].level->load() * 0.01f);
    }

    smoothedSplice.setTargetValue (spliceParam->load() * 0.001f * sr);
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        // Les deux voix transposent la même entrée mono (somme G + D),
        // plus le retour des voix si Feedback > 0 (cascade de transpositions)
        const float monoIn = 0.5f * (dryL + dryR);
        shifter.write (monoIn + smoothedFeedback.getNextValue() * feedbackSample);

        // Diatonic : nouvelle mesure de hauteur toutes les ~5 ms → nouveaux intervalles.
        // Le décalage s'applique à la hauteur réellement jouée : bends et vibrato sont conservés.
        if (diatonic && detector.push (monoIn))
        {
            const int before = tracker.getNote();
            tracker.update (detector.isVoiced() ? PitchDetector::toMidi (detector.getFrequency()) : -1.0f);

            if (tracker.getNote() != before)
            {
                updateDiatonicShifts (key, scale);
                for (int v = 0; v < 2; ++v)
                    smoothedRatio[v].setTargetValue (targetRatio (v));
            }
        }

        const float window = smoothedSplice.getNextValue();
        const float a = shifter.process (voiceState[0], smoothedRatio[0].getNextValue(), smoothedDelay[0].getNextValue(), window)
                      * smoothedLevel[0].getNextValue();
        const float b = shifter.process (voiceState[1], smoothedRatio[1].getNextValue(), smoothedDelay[1].getNextValue(), window)
                      * smoothedLevel[1].getNextValue();

        feedbackSample = 0.5f * (a + b);

        // Spread 100 % : voix A à gauche, B à droite. 0 % : les deux au centre.
        const float spread = smoothedSpread.getNextValue();
        const float gNear  = 0.5f + 0.5f * spread;
        const float gFar   = 0.5f - 0.5f * spread;
        const float wetL   = a * gNear + b * gFar;
        const float wetR   = b * gNear + a * gFar;

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

void HarmonizerProcessor::updateDiatonicShifts (int key, const DiatonicScale::Scale& scale) noexcept
{
    const int note = tracker.getNote();
    // Avant la première note : intervalles comptés depuis la tonique
    const int reference = note >= 0 ? note : 60 + key;

    for (int v = 0; v < 2; ++v)
    {
        diatonicSemitones[v] = DiatonicScale::semitonesFor (reference, key, scale, (int) voiceParams[v].degrees->load());
        voiceShift[v] = diatonicSemitones[v];
    }

    detectedNote = note;
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor (*this);
}

void HarmonizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
