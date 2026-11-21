// Tests du moteur de compresseur (sans JUCE). Cible Xcode : CompressorTest
#include "CompressorEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr float pi = 3.14159265f;
    int failures = 0;

    void check (bool ok, const char* name, const char* detail)
    {
        std::printf ("%-52s %s  %s\n", name, ok ? "OK    " : "ÉCHEC ", detail);
        if (! ok)
            ++failures;
    }

    // Onde carrée à 1 kHz : niveau crête constant, que le coupe-bas du détecteur laisse passer
    std::vector<float> square (float peakDb, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = ((i / 24) % 2 == 0 ? 1.0f : -1.0f) * std::pow (10.0f, peakDb / 20.0f);
        return v;
    }

    float db (float gain) { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }
    float fromDb (float d) { return std::pow (10.0f, d / 20.0f); }

    std::vector<float> sine (float freq, float peakDb, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = fromDb (peakDb) * std::sin (2.0f * pi * freq * (float) i / (float) sampleRate);
        return v;
    }

    float peakOf (const std::vector<float>& v, int from, int to)
    {
        float p = 0.0f;
        for (int i = from; i < to; ++i)
            p = std::max (p, std::abs (v[(size_t) i]));
        return p;
    }

    CompressorEngine make (CompressorEngine::Settings s)
    {
        CompressorEngine e;
        e.prepare (sampleRate);
        e.setSettings (s);
        e.reset();
        return e;
    }

    void run (CompressorEngine& e, std::vector<float>& l, std::vector<float>& r)
    {
        for (size_t start = 0; start < l.size(); start += 512)
            e.process (l.data() + start, r.data() + start, (int) std::min<size_t> (512, l.size() - start));
    }

    // Réduction de gain mesurée sur un signal constant (la sortie divisée par l'entrée)
    std::vector<float> gainTrace (CompressorEngine& e, const std::vector<float>& input)
    {
        std::vector<float> l (input), r (input), trace (input.size());
        for (size_t i = 0; i < input.size(); ++i)
        {
            e.process (&l[i], &r[i], 1);
            trace[i] = e.getGainReduction();
        }
        return trace;
    }
}

int main()
{
    char detail[160];
    using Engine = CompressorEngine;

    // 1. Ratio 1:1, sans gain : la sortie est l'entrée, à l'échantillon près
    {
        Engine::Settings s;
        s.ratio = 1.0f;
        auto e = make (s);
        auto in = sine (440.0f, -6.0f, 48000);
        auto l = in, r = in;
        run (e, l, r);
        float err = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            err = std::max (err, std::abs (l[i] - in[i]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", err);
        check (err < 1.0e-6f, "ratio 1:1 : transparent", detail);
    }

    // 2. Sous le seuil : rien ne change
    {
        Engine::Settings s;
        s.thresholdDb = -20.0f;
        s.kneeDb = 0.0f;
        auto e = make (s);
        auto in = sine (1000.0f, -30.0f, 48000);
        auto l = in, r = in;
        run (e, l, r);
        const float change = db (peakOf (l, 24000, 48000) / peakOf (in, 24000, 48000));
        std::snprintf (detail, sizeof detail, "%+.3f dB", change);
        check (std::abs (change) < 0.01f, "sous le seuil : intact", detail);
    }

    // 3. Courbe statique : −10 dBFS, seuil −20, ratio 4 → 10 dB au-dessus deviennent 2,5 : sortie −17,5 dBFS
    for (int style : { (int) Engine::clean, (int) Engine::punch })
    {
        Engine::Settings s;
        s.style = style;
        s.thresholdDb = -20.0f;
        s.ratio = 4.0f;
        s.kneeDb = 0.0f;
        s.attackMs = 1.0f;
        s.releaseMs = 200.0f;
        auto e = make (s);
        auto in = sine (1000.0f, -10.0f, 96000);
        auto l = in, r = in;
        run (e, l, r);
        const float out = db (peakOf (l, 72000, 96000));
        std::snprintf (detail, sizeof detail, "%.2f dBFS (attendu −17,5)", out);
        check (std::abs (out + 17.5f) < 0.6f, style == Engine::clean ? "Clean : seuil et ratio exacts" : "Punch (feedback) : même courbe", detail);
    }

    // 4. Genou doux : la courbe est continue et passe sous la droite dure, sans marche
    {
        float worstJump = 0.0f;
        float previous = Engine::curve (-40.0f, -20.0f, 4.0f, 12.0f);
        bool belowInput = true;
        for (float x = -39.9f; x <= 0.0f; x += 0.1f)
        {
            const float y = Engine::curve (x, -20.0f, 4.0f, 12.0f);
            worstJump = std::max (worstJump, std::abs (y - previous));
            belowInput = belowInput && y <= x + 1.0e-4f;
            previous = y;
        }
        std::snprintf (detail, sizeof detail, "plus grand pas %.3f dB pour 0,1 dB", worstJump);
        check (worstJump <= 0.1001f && belowInput, "genou doux : continu, jamais d'amplification", detail);
    }

    // 5. Attaque : un saut de −40 à −10 dBFS atteint 63 % de la réduction finale en ~10 ms
    {
        Engine::Settings s;
        s.thresholdDb = -20.0f;
        s.ratio = 10.0f;
        s.kneeDb = 0.0f;
        s.attackMs = 10.0f;
        s.releaseMs = 500.0f;
        auto e = make (s);
        const auto in = square (-10.0f, 48000);
        const auto trace = gainTrace (e, in);
        const float final = trace.back();
        int reached = -1;
        for (size_t i = 0; i < trace.size(); ++i)
            if (trace[i] >= 0.632f * final)
            {
                reached = (int) i;
                break;
            }
        const float ms = reached * 1000.0f / (float) sampleRate;
        std::snprintf (detail, sizeof detail, "63 %% en %.1f ms, réduction finale %.1f dB", ms, final);
        check (ms > 8.0f && ms < 12.0f && std::abs (final - 9.0f) < 0.5f, "attaque : temps exact", detail);
    }

    // 6. Relâche automatique : après une note courte, ça revient plus vite qu'après une note tenue
    {
        Engine::Settings s;
        s.thresholdDb = -30.0f;
        s.ratio = 6.0f;
        s.attackMs = 2.0f;
        s.releaseMs = 400.0f;
        s.autoRelease = true;

        auto recovery = [&] (int burstSamples)
        {
            auto e = make (s);
            auto in = square (-6.0f, burstSamples + 48000);
            for (size_t i = (size_t) burstSamples; i < in.size(); ++i)
                in[i] = 0.0f;
            const auto trace = gainTrace (e, in);
            const float peak = trace[(size_t) burstSamples - 1];
            for (size_t i = (size_t) burstSamples; i < trace.size(); ++i)
                if (trace[i] < 0.1f * peak)
                    return (float) (i - (size_t) burstSamples) * 1000.0f / (float) sampleRate;
            return 1000.0f;
        };

        const float shortNote = recovery (2400), longNote = recovery (96000);
        std::snprintf (detail, sizeof detail, "note de 50 ms : %.0f ms, note de 2 s : %.0f ms", shortNote, longNote);
        check (shortNote * 2.0f < longNote, "relâche auto : dépend du jeu", detail);
    }

    // 7. Mix 0 % : le son sec, exactement
    {
        Engine::Settings s;
        s.thresholdDb = -40.0f;
        s.ratio = 20.0f;
        s.mix = 0.0f;
        auto e = make (s);
        auto in = sine (220.0f, -3.0f, 24000);
        auto l = in, r = in;
        run (e, l, r);
        float err = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            err = std::max (err, std::abs (l[i] - in[i]));
        std::snprintf (detail, sizeof detail, "écart max %.2e", err);
        check (err < 1.0e-6f, "mix 0 % : son sec intact", detail);
    }

    // 8. Coupe-bas du détecteur : une basse à 50 Hz déclenche beaucoup moins la compression
    {
        auto reductionFor = [] (float hz)
        {
            Engine::Settings s;
            s.thresholdDb = -30.0f;
            s.ratio = 8.0f;
            s.sidechainHz = hz;
            auto e = make (s);
            auto in = sine (50.0f, -6.0f, 48000);
            auto l = in, r = in;
            run (e, l, r);
            return e.getGainReduction();
        };
        const float full = reductionFor (10.0f), filtered = reductionFor (250.0f);
        std::snprintf (detail, sizeof detail, "sans filtre %.1f dB, coupe-bas 250 Hz %.1f dB", full, filtered);
        check (full - filtered > 6.0f, "coupe-bas du détecteur : les basses ne pompent plus", detail);
    }

    // 9. Gain automatique : un jeu fort ressort à un niveau proche de l'entrée
    {
        Engine::Settings s;
        s.thresholdDb = -30.0f;
        s.ratio = 4.0f;
        s.autoMakeup = true;
        auto e = make (s);
        auto in = sine (440.0f, -12.0f, 96000);
        auto l = in, r = in;
        run (e, l, r);
        const float change = db (peakOf (l, 72000, 96000) / fromDb (-12.0f));
        std::snprintf (detail, sizeof detail, "%+.1f dB par rapport à l'entrée (gain auto %.1f dB)", change, e.getMakeup());
        check (std::abs (change) < 6.0f && e.getMakeup() > 5.0f, "gain automatique : volume rattrapé", detail);
    }

    // 10. Cas extrêmes : ratio 20, attaque minimale, les trois caractères, en mono et en stéréo : jamais de NaN, jamais d'emballement
    {
        bool ok = true;
        float worst = 0.0f;
        for (int style = 0; style < Engine::numStyles; ++style)
            for (float ratio : { 1.5f, 4.0f, 20.0f })
            {
                Engine::Settings s;
                s.style = style;
                s.thresholdDb = -50.0f;
                s.ratio = ratio;
                s.attackMs = 0.1f;
                s.releaseMs = 10.0f;
                s.makeupDb = 0.0f;
                auto e = make (s);
                auto l = sine (3000.0f, 0.0f, 48000), r = sine (60.0f, -3.0f, 48000);
                run (e, l, r);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
                    worst = std::max ({ worst, std::abs (l[i]), std::abs (r[i]) });
                }
                std::vector<float> mono = sine (100.0f, 0.0f, 4800);
                e.process (mono.data(), nullptr, (int) mono.size());
                for (auto v : mono)
                    ok = ok && std::isfinite (v);
            }
        std::snprintf (detail, sizeof detail, "crête max %.2f", worst);
        check (ok && worst <= 1.0f, "stable dans tous les cas", detail);
    }

    // 11. Opto : comprime en douceur (genou large), sans dépasser la courbe dure
    {
        Engine::Settings s;
        s.style = Engine::opto;
        s.thresholdDb = -24.0f;
        s.ratio = 4.0f;
        auto e = make (s);
        auto in = sine (440.0f, -10.0f, 96000);
        auto l = in, r = in;
        run (e, l, r);
        const float out = db (peakOf (l, 72000, 96000));
        std::snprintf (detail, sizeof detail, "−10 dBFS → %.1f dBFS (courbe dure : −20,5)", out);
        check (out < -14.0f && out > -22.0f, "Opto : compression douce et régulière", detail);
    }

    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS OK" : "DES TESTS ONT ÉCHOUÉ");
    return failures == 0 ? 0 : 1;
}
