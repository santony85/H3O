# H3O

Suite d'effets audio pour guitare (AU, VST3 et applications autonomes pour macOS), écrite en C++ avec JUCE.
Le cœur du projet est **H3O Chain**, un multi-effets avec routage libre en deux branches parallèles (dans l'esprit de l'Eventide H3000),
une matrice de modulation et 200 presets d'usine.

## Effets

Compressor · Swept Filter · Amp (modèles NAM et IR) · Harmonizer (diatonique) · Reverse · Layered Shift · Chorus ·
Tremolo (trémolo, harmonique, auto-pan) · Delay · Band Delay · Ultra-Tap · Reverb

Chaque effet existe aussi en plugin séparé.

## Compiler

Prérequis : macOS, Xcode, CMake et [JUCE](https://github.com/juce-framework/JUCE).

```bash
git clone --recursive https://github.com/santony85/H3O.git
cd H3O
cmake -S . -B build -G Xcode -DJUCE_DIR=$HOME/JUCE
cmake --build build --config Release
```

## Licence

GNU AGPLv3 (voir [LICENSE.md](LICENSE.md)). H3O utilise JUCE et NeuralAmpModelerCore (MIT).
Eventide et H3000 sont des marques d'Eventide Inc. ; H3O n'y est pas affilié.
