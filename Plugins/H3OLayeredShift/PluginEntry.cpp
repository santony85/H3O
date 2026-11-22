#include "LayeredShift/LayeredShiftProcessor.h"

// Point d'entrée appelé par Logic pour créer le plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LayeredShiftProcessor();
}
