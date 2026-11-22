#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainProcessor.h"

// Les réglages des 4 mélangeurs de la baie de brassage : pour chaque entrée (A et B), un niveau et un panoramique.
class MixerPanel : public juce::Component
{
public:
    explicit MixerPanel (ChainProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Colour accent() { return juce::Colour (0xffd8dce6); }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ChainProcessor& chain;
    std::array<std::array<Knob, 4>, ChainPatch::numMixers> knobs;   // niveau A, pan A, niveau B, pan B
    std::array<juce::Rectangle<int>, ChainPatch::numMixers> columns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
