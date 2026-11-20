#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Choix de l'entrée, commun à tous les effets. La sortie est toujours stéréo.
//
//  - Stereo   : gauche et droite traités tels quels
//  - Mono (L) : seule l'entrée 1 est utilisée, envoyée des deux côtés (micro du Mac, guitare sur l'entrée 1)
//  - Mono (R) : seule l'entrée 2 est utilisée (instrument branché sur l'entrée 2 d'une carte son)
//
// Sur une piste mono de Logic (bus d'entrée à 1 canal), l'entrée est toujours dupliquée des deux côtés.
namespace InputMode
{
    enum Mode { stereo = 0, monoLeft, monoRight };

    inline const juce::StringArray& names()
    {
        static const juce::StringArray n { "Stereo", "Mono (L)", "Mono (R)" };
        return n;
    }

    // En Standalone, l'entrée est souvent un seul micro : Mono (L) par défaut.
    // Dans Logic, Stereo par défaut (une piste mono est de toute façon gérée à part).
    inline std::unique_ptr<juce::AudioParameterChoice> createParameter()
    {
        const bool standalone = juce::PluginHostType::getPluginLoadedAs()
                             == juce::AudioProcessor::wrapperType_Standalone;

        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "input", 1 }, "Input", names(), standalone ? monoLeft : stereo);
    }

    // Entrée mono ou stéréo, sortie toujours stéréo
    inline bool isLayoutSupported (const juce::AudioProcessor::BusesLayout& layouts)
    {
        const auto& in = layouts.getMainInputChannelSet();
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    // À placer en tête de processBlock : après l'appel, les canaux 0 et 1 contiennent le signal à traiter
    class Selector
    {
    public:
        void attach (juce::AudioProcessorValueTreeState& state) { param = state.getRawParameterValue ("input"); }

        // Dans Simple Chain, c'est la chaîne qui choisit l'entrée : l'effet ne touche à rien
        // et son menu Input est masqué
        void setInsideChain() noexcept      { insideChain = true; }
        bool isInsideChain() const noexcept { return insideChain; }

        void process (juce::AudioBuffer<float>& buffer, int numInputChannels) const noexcept
        {
            if (buffer.getNumChannels() < 2)
                return;

            const int n    = buffer.getNumSamples();
            const int mode = (insideChain || param == nullptr) ? stereo : (int) param->load();

            if (numInputChannels < 2 || mode == monoLeft)
                buffer.copyFrom (1, 0, buffer, 0, 0, n);
            else if (mode == monoRight)
                buffer.copyFrom (0, 0, buffer, 1, 0, n);
        }

    private:
        std::atomic<float>* param = nullptr;
        bool insideChain = false;
    };

    // Menu « Input » prêt à placer dans une interface
    class Box : public juce::Component
    {
    public:
        Box (juce::AudioProcessorValueTreeState& state, juce::Colour accent)
        {
            label.setText ("Input", juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, accent);
            label.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (label);

            combo.addItemList (names(), 1);
            addAndMakeVisible (combo);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "input", combo);
        }

        static constexpr int preferredWidth = 150;

        void resized() override
        {
            auto area = getLocalBounds();
            label.setBounds (area.removeFromLeft (45));
            combo.setBounds (area.reduced (0, 2));
        }

    private:
        juce::Label label;
        juce::ComboBox combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
}
