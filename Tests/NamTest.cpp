// Charge un fichier .nam, fait passer 10 s de signal de guitare simulé,
// et mesure si le modèle tient le temps réel. Cible Xcode : NamTest
//   ./build/Release/NamTest "/chemin/vers/modele.nam"
#include "NAM/get_dsp.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("Usage : NamTest fichier.nam\n");
        return 2;
    }

    nam::activations::Activation::enable_fast_tanh();

    auto model = nam::get_dsp (std::filesystem::path (argv[1]));
    if (model == nullptr)
    {
        std::printf ("ÉCHEC : modèle illisible\n");
        return 1;
    }

    const double sr = model->GetExpectedSampleRate() > 0 ? model->GetExpectedSampleRate() : 48000.0;
    const int block = 512;
    model->Reset (sr, block);

    std::printf ("Fréquence du modèle : %.0f Hz\n", model->GetExpectedSampleRate());
    if (model->HasLoudness())
        std::printf ("Volume du modèle    : %.1f dB\n", model->GetLoudness());

    std::vector<float> in ((size_t) block), out ((size_t) block);
    float* inPtr = in.data();
    float* outPtr = out.data();

    const int totalSeconds = 10;
    const int numBlocks = (int) (totalSeconds * sr) / block;
    double sumSq = 0.0;
    bool finite = true;
    long n = 0;

    const auto start = std::chrono::steady_clock::now();

    for (int b = 0; b < numBlocks; ++b)
    {
        // Note de guitare simulée : 110 Hz avec harmoniques, qui décroît toutes les secondes
        for (int i = 0; i < block; ++i, ++n)
        {
            const double t = (double) n / sr;
            const double env = std::exp (-3.0 * std::fmod (t, 1.0));
            in[(size_t) i] = (float) (0.25 * env * (std::sin (2 * M_PI * 110 * t)
                                                    + 0.5 * std::sin (2 * M_PI * 220 * t)
                                                    + 0.25 * std::sin (2 * M_PI * 330 * t)));
        }

        model->process (&inPtr, &outPtr, block);

        for (float y : out)
        {
            finite = finite && std::isfinite (y);
            sumSq += (double) y * y;
        }
    }

    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double rms = std::sqrt (sumSq / (double) n);

    std::printf ("Niveau de sortie    : %.1f dBFS RMS\n", 20.0 * std::log10 (rms + 1e-12));
    std::printf ("Temps de calcul     : %.2f s pour %d s d'audio (%.1f %% d'un cœur)\n",
                 seconds, totalSeconds, 100.0 * seconds / totalSeconds);

    const bool ok = finite && rms > 1e-4 && seconds < totalSeconds;
    std::printf (ok ? "OK\n" : "ÉCHEC\n");
    return ok ? 0 : 1;
}
