#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Paramètres modulables, pour la matrice de modulation de Simple Chain.
//
// Un effet ne lit plus ses paramètres directement : il lit une « valeur effective »,
// égale au réglage du bouton, plus un décalage posé par la matrice (0 hors de la chaîne).
// Le réglage lui-même n'est jamais touché : la session garde ta valeur, le bouton ne bouge pas.
//
// Le décalage est exprimé dans l'échelle du bouton (0 à 1, avec sa courbe) :
// +0,25 = un quart de la course du bouton vers la droite.
class ModTargets
{
public:
    struct Target
    {
        juce::RangedAudioParameter* parameter = nullptr;
        std::atomic<float>* base = nullptr;     // valeur du réglage (APVTS)
        std::atomic<float> value { 0.0f };      // valeur effective, lue par l'effet
        std::atomic<float> offset { 0.0f };     // décalage posé par la matrice
    };

    // À la place de parameters.getRawParameterValue (id) : même usage, valeur modulée
    std::atomic<float>* get (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        // Menus et interrupteurs : pas modulables, l'effet lit directement le réglage
        if (dynamic_cast<juce::AudioParameterFloat*> (state.getParameter (id)) == nullptr)
            return state.getRawParameterValue (id);

        auto target = std::make_unique<Target>();
        target->parameter = state.getParameter (id);
        target->base      = state.getRawParameterValue (id);
        jassert (target->parameter != nullptr && target->base != nullptr);
        target->value     = target->base->load();
        ids.add (id);
        targets.push_back (std::move (target));
        return &targets.back()->value;
    }

    // En tête de prepareToPlay et de processBlock : calcule les valeurs effectives
    void update() noexcept
    {
        for (auto& t : targets)
        {
            const float base = t->base->load();
            const float offset = t->offset.load (std::memory_order_relaxed);

            if (offset == 0.0f)
            {
                t->value.store (base, std::memory_order_relaxed);
                continue;
            }

            const float normalised = juce::jlimit (0.0f, 1.0f, t->parameter->convertTo0to1 (base) + offset);
            t->value.store (t->parameter->convertFrom0to1 (normalised), std::memory_order_relaxed);
        }
    }

    int size() const noexcept                         { return (int) targets.size(); }
    Target& operator[] (int i) noexcept               { return *targets[(size_t) i]; }
    const juce::String& idOf (int i) const noexcept   { return ids.getReference (i); }

    Target* find (const juce::String& id) noexcept
    {
        const int i = ids.indexOf (id);
        return i >= 0 ? targets[(size_t) i].get() : nullptr;
    }

private:
    std::vector<std::unique_ptr<Target>> targets;
    juce::StringArray ids;
};
