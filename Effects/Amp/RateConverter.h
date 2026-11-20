#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Fait tourner un traitement mono à une autre fréquence d'échantillonnage que l'hôte.
//
// Les modèles NAM sont entraînés à une fréquence précise (presque toujours 48 kHz).
// Si Logic tourne à 44,1 kHz, on convertit : hôte → fréquence du modèle → modèle → retour à l'hôte.
// Le nombre d'échantillons au rythme du modèle varie d'un bloc à l'autre (ex. 512 → 557 ou 558),
// d'où les petites files d'attente (FIFO) de chaque côté. Latence ajoutée : environ 0,5 ms.
//
// Tout est alloué dans prepare() : rien n'est alloué pendant le traitement.
class RateConverter
{
public:
    void prepare (double newHostRate, double newModelRate, int maxHostBlock)
    {
        hostRate  = newHostRate;
        modelRate = newModelRate;
        active    = std::abs (hostRate - modelRate) > 0.5;

        up.reset();
        down.reset();

        // Combien d'échantillons de l'hôte consommés par échantillon produit
        upRatio   = hostRate / modelRate;
        downRatio = modelRate / hostRate;

        const int maxModel = maxModelBlockFor (maxHostBlock);

        inFifo.assign  ((size_t) (maxHostBlock + 64), 0.0f);
        modelIn.assign ((size_t) maxModel, 0.0f);
        modelOut.assign ((size_t) maxModel, 0.0f);
        midFifo.assign ((size_t) (maxModel + 64), 0.0f);
        outFifo.assign ((size_t) (2 * maxHostBlock + 256), 0.0f);

        inCount = midCount = 0;

        // Un peu d'avance en sortie pour ne jamais manquer d'échantillons
        outCount = latency;
        std::fill (outFifo.begin(), outFifo.begin() + latency, 0.0f);
    }

    bool isActive() const noexcept { return active; }

    // Taille de bloc maximale que le modèle recevra
    int maxModelBlockFor (int maxHostBlock) const noexcept
    {
        return (int) std::ceil ((double) maxHostBlock * modelRate / hostRate) + 16;
    }

    // Traite data (n échantillons à la fréquence de l'hôte) sur place.
    // fn (const float* in, float* out, int count) est appelé à la fréquence du modèle.
    template <typename Fn>
    void process (float* data, int n, Fn&& fn) noexcept
    {
        // 1. Entrée de l'hôte → file d'entrée
        n = juce::jmin (n, (int) inFifo.size() - inCount);
        std::copy (data, data + n, inFifo.data() + inCount);
        inCount += n;

        // 2. Conversion vers la fréquence du modèle (en gardant 2 échantillons d'avance)
        int toModel = (int) std::floor ((double) (inCount - 2) / upRatio);
        toModel = juce::jlimit (0, (int) modelIn.size(), toModel);

        if (toModel > 0)
        {
            const int used = up.process (upRatio, inFifo.data(), modelIn.data(), toModel);
            consume (inFifo, inCount, used);

            // 3. Le modèle
            fn (modelIn.data(), modelOut.data(), toModel);

            // 4. Retour vers la fréquence de l'hôte
            const int room = (int) midFifo.size() - midCount;
            const int k = juce::jmin (toModel, room);
            std::copy (modelOut.data(), modelOut.data() + k, midFifo.data() + midCount);
            midCount += k;
        }

        int toHost = (int) std::floor ((double) (midCount - 2) / downRatio);
        toHost = juce::jlimit (0, (int) outFifo.size() - outCount, toHost);

        if (toHost > 0)
        {
            const int used = down.process (downRatio, midFifo.data(), outFifo.data() + outCount, toHost);
            consume (midFifo, midCount, used);
            outCount += toHost;
        }

        // 5. Sortie : exactement n échantillons (des zéros en cas de manque, ce qui ne doit pas arriver)
        const int available = juce::jmin (n, outCount);
        std::copy (outFifo.data(), outFifo.data() + available, data);
        std::fill (data + available, data + n, 0.0f);
        consume (outFifo, outCount, available);
    }

    static constexpr int latency = 24;   // échantillons, à la fréquence de l'hôte

private:
    static void consume (std::vector<float>& fifo, int& count, int used) noexcept
    {
        used = juce::jlimit (0, count, used);
        std::copy (fifo.data() + used, fifo.data() + count, fifo.data());
        count -= used;
    }

    double hostRate = 48000.0, modelRate = 48000.0, upRatio = 1.0, downRatio = 1.0;
    bool active = false;

    juce::LagrangeInterpolator up, down;
    std::vector<float> inFifo, modelIn, modelOut, midFifo, outFifo;
    int inCount = 0, midCount = 0, outCount = 0;
};
