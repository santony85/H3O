cmake_minimum_required(VERSION 3.22)

# À définir AVANT project() : sinon CMake a déjà choisi et ces lignes sont ignorées.
# Apple Silicon uniquement (les Mac Intel ne sont plus pris en charge par Xcode 27).
# Pour revenir à un binaire universel : "arm64;x86_64"
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version (Xcode 27 : 12.0 minimum)")

project(H3O VERSION 0.3.0)

# Range les fichiers des modules JUCE dans un dossier à part dans Xcode
set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Group JUCE module sources in the IDE" ON)

# Génère des schémas Xcode complets : les schémas Standalone savent quelle app lancer (⌘R)
set(CMAKE_XCODE_GENERATE_SCHEME ON)

# Chemin vers JUCE (modifiable : cmake -DJUCE_DIR=/autre/chemin)
set(JUCE_DIR "$ENV{HOME}/Developer/JUCE" CACHE PATH "JUCE location")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

#==============================================================================
# NeuralAmpModelerCore (licence MIT) : le moteur qui fait tourner les fichiers .nam.
# Compilé une fois, sans les avertissements stricts de JUCE.
#==============================================================================
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/NeuralAmpModelerCore)
file(GLOB NAM_CORE_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)

add_library(nam STATIC ${NAM_CORE_SOURCES})
# SYSTEM : les en-têtes de NAM ne déclenchent pas les avertissements stricts de nos plugins
target_include_directories(nam SYSTEM PUBLIC ${NAM_DIR})
target_include_directories(nam SYSTEM PUBLIC ${NAM_DIR}/Dependencies/eigen ${NAM_DIR}/Dependencies/nlohmann)
target_compile_features(nam PUBLIC cxx_std_20)
# NAM_SAMPLE_FLOAT : le modèle lit et écrit directement des float, comme JUCE
target_compile_definitions(nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_compile_options(nam PRIVATE -w $<$<CONFIG:Release>:-O3>)
set_target_properties(nam PROPERTIES FOLDER "External")

# Chaque architecture (WaveNet, LSTM, ConvNet…) s'enregistre via un objet global que rien
# ne référence directement : sans -force_load, l'éditeur de liens les supprime et aucun .nam
# ne se charge (« No config parser registered »). JUCE place le code des plugins dans une
# bibliothèque intermédiaire : le -force_load doit donc aller sur chaque format final.
function(audiofx_link_nam target)
    target_link_libraries(${target} PRIVATE nam)
    set(force_load "LINKER:-force_load,$<TARGET_FILE:nam>")

    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE ${force_load})
    endif()

    foreach(format AU VST3 Standalone)
        if(TARGET ${target}_${format})
            target_link_options(${target}_${format} PRIVATE ${force_load})
            add_dependencies(${target}_${format} nam)
        endif()
    endforeach()
endfunction()

#==============================================================================
# Code des effets : écrit une seule fois, compilé dans chaque plugin qui l'utilise
#==============================================================================
set(EFFECTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Effects)

set(COMMON_SOURCES
    ${EFFECTS_DIR}/Common/InputMode.h
    ${EFFECTS_DIR}/Common/ModTargets.h)

set(DELAY_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Delay/DelayProcessor.h
    ${EFFECTS_DIR}/Delay/DelayProcessor.cpp
    ${EFFECTS_DIR}/Delay/DelayEditor.h
    ${EFFECTS_DIR}/Delay/DelayEditor.cpp)

set(HARMONIZER_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Harmonizer/PitchShifter.h
    ${EFFECTS_DIR}/Harmonizer/PitchDetector.h
    ${EFFECTS_DIR}/Harmonizer/DiatonicScale.h
    ${EFFECTS_DIR}/Harmonizer/HarmonizerProcessor.h
    ${EFFECTS_DIR}/Harmonizer/HarmonizerProcessor.cpp
    ${EFFECTS_DIR}/Harmonizer/HarmonizerEditor.h
    ${EFFECTS_DIR}/Harmonizer/HarmonizerEditor.cpp)

set(AMP_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Amp/RateConverter.h
    ${EFFECTS_DIR}/Amp/AmpProcessor.h
    ${EFFECTS_DIR}/Amp/AmpProcessor.cpp
    ${EFFECTS_DIR}/Amp/AmpEditor.h
    ${EFFECTS_DIR}/Amp/AmpEditor.cpp)

set(CHORUS_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Chorus/ChorusEngine.h
    ${EFFECTS_DIR}/Chorus/ChorusProcessor.h
    ${EFFECTS_DIR}/Chorus/ChorusProcessor.cpp
    ${EFFECTS_DIR}/Chorus/ChorusEditor.h
    ${EFFECTS_DIR}/Chorus/ChorusEditor.cpp)

set(REVERSE_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Reverse/ReverseEngine.h
    ${EFFECTS_DIR}/Reverse/ReverseProcessor.h
    ${EFFECTS_DIR}/Reverse/ReverseProcessor.cpp
    ${EFFECTS_DIR}/Reverse/ReverseEditor.h
    ${EFFECTS_DIR}/Reverse/ReverseEditor.cpp)

set(ULTRATAP_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/UltraTap/UltraTapEngine.h
    ${EFFECTS_DIR}/UltraTap/UltraTapProcessor.h
    ${EFFECTS_DIR}/UltraTap/UltraTapProcessor.cpp
    ${EFFECTS_DIR}/UltraTap/UltraTapEditor.h
    ${EFFECTS_DIR}/UltraTap/UltraTapEditor.cpp)

set(BANDDELAY_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/BandDelay/BandDelayEngine.h
    ${EFFECTS_DIR}/BandDelay/BandDelayProcessor.h
    ${EFFECTS_DIR}/BandDelay/BandDelayProcessor.cpp
    ${EFFECTS_DIR}/BandDelay/BandDelayEditor.h
    ${EFFECTS_DIR}/BandDelay/BandDelayEditor.cpp)

set(REVERB_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Reverb/ReverbEngine.h
    ${EFFECTS_DIR}/Reverb/ReverbProcessor.h
    ${EFFECTS_DIR}/Reverb/ReverbProcessor.cpp
    ${EFFECTS_DIR}/Reverb/ReverbEditor.h
    ${EFFECTS_DIR}/Reverb/ReverbEditor.cpp)

set(LAYEREDSHIFT_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Harmonizer/PitchShifter.h
    ${EFFECTS_DIR}/LayeredShift/LayeredShiftEngine.h
    ${EFFECTS_DIR}/LayeredShift/LayeredShiftProcessor.h
    ${EFFECTS_DIR}/LayeredShift/LayeredShiftProcessor.cpp
    ${EFFECTS_DIR}/LayeredShift/LayeredShiftEditor.h
    ${EFFECTS_DIR}/LayeredShift/LayeredShiftEditor.cpp)

set(COMPRESSOR_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/Compressor/CompressorEngine.h
    ${EFFECTS_DIR}/Compressor/CompressorProcessor.h
    ${EFFECTS_DIR}/Compressor/CompressorProcessor.cpp
    ${EFFECTS_DIR}/Compressor/CompressorEditor.h
    ${EFFECTS_DIR}/Compressor/CompressorEditor.cpp)

set(SWEPTFILTER_SOURCES
    ${COMMON_SOURCES}
    ${EFFECTS_DIR}/SweptFilter/SweptFilterEngine.h
    ${EFFECTS_DIR}/SweptFilter/SweptFilterProcessor.h
    ${EFFECTS_DIR}/SweptFilter/SweptFilterProcessor.cpp
    ${EFFECTS_DIR}/SweptFilter/SweptFilterEditor.h
    ${EFFECTS_DIR}/SweptFilter/SweptFilterEditor.cpp)

# Réglages communs à tous les plugins : sources, dossier "Source" dans Xcode, modules JUCE
function(audiofx_setup_plugin target)
    set(sources ${ARGN})
    target_sources(${target} PRIVATE ${sources})
    source_group(TREE ${CMAKE_SOURCE_DIR} PREFIX "Source" FILES ${sources})

    target_include_directories(${target} PRIVATE ${EFFECTS_DIR})
    target_compile_features(${target} PRIVATE cxx_std_17)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

#==============================================================================
# Les plugins : chacun a sa propre cible et se compile séparément
#==============================================================================
add_subdirectory(Plugins/H3OAmp)
add_subdirectory(Plugins/H3ODelay)
add_subdirectory(Plugins/H3OHarmonizer)
add_subdirectory(Plugins/H3OChorus)
add_subdirectory(Plugins/H3OReverse)
add_subdirectory(Plugins/H3OUltraTap)
add_subdirectory(Plugins/H3OBandDelay)
add_subdirectory(Plugins/H3OReverb)
add_subdirectory(Plugins/H3OLayeredShift)
add_subdirectory(Plugins/H3OSweptFilter)
add_subdirectory(Plugins/H3OCompressor)
add_subdirectory(Chain/H3OChain)

#==============================================================================
# Test de justesse du pitch shifter, sans JUCE : cible PitchTest
#==============================================================================
add_executable(PitchTest Tests/PitchTest.cpp)
target_include_directories(PitchTest PRIVATE ${EFFECTS_DIR}/Harmonizer)
target_compile_features(PitchTest PRIVATE cxx_std_17)

#==============================================================================
# Test du compresseur, sans JUCE : cible CompressorTest
#==============================================================================
add_executable(CompressorTest Tests/CompressorTest.cpp)
target_include_directories(CompressorTest PRIVATE ${EFFECTS_DIR}/Compressor)
target_compile_features(CompressorTest PRIVATE cxx_std_17)

#==============================================================================
# Test du chorus, sans JUCE : cible ChorusTest
#==============================================================================
add_executable(ChorusTest Tests/ChorusTest.cpp)
target_include_directories(ChorusTest PRIVATE ${EFFECTS_DIR}/Chorus)
target_compile_features(ChorusTest PRIVATE cxx_std_17)

#==============================================================================
# Test du moteur NAM sur un vrai fichier .nam : cible NamTest
#==============================================================================
add_executable(NamTest Tests/NamTest.cpp)
audiofx_link_nam(NamTest)

#==============================================================================
# Test de H3O Amp complet (conversion de fréquence comprise) : cible AmpTest
#==============================================================================
juce_add_console_app(AmpTest PRODUCT_NAME "AmpTest")
target_sources(AmpTest PRIVATE Tests/AmpTest.cpp ${AMP_SOURCES})
target_include_directories(AmpTest PRIVATE ${EFFECTS_DIR})
target_compile_definitions(AmpTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
audiofx_link_nam(AmpTest)
target_link_libraries(AmpTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

#==============================================================================
# Test du vrai composant installé, chargé comme dans Logic : cible PluginHostTest
#==============================================================================
juce_add_console_app(PluginHostTest PRODUCT_NAME "PluginHostTest")
target_sources(PluginHostTest PRIVATE Tests/PluginHostTest.cpp)
target_compile_definitions(PluginHostTest PRIVATE JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PluginHostTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags "-framework CoreAudioKit")

#==============================================================================
# Test du Diatonic Shift (gammes, détection de hauteur, chaîne complète) : cible DiatonicTest
#==============================================================================
add_executable(DiatonicTest Tests/DiatonicTest.cpp)
target_include_directories(DiatonicTest PRIVATE ${EFFECTS_DIR}/Harmonizer)
target_compile_features(DiatonicTest PRIVATE cxx_std_17)

#==============================================================================
# Test de H3O Harmonizer complet (mode Diatonic, presets, état) : cible HarmonizerTest
#==============================================================================
juce_add_console_app(HarmonizerTest PRODUCT_NAME "HarmonizerTest")
target_sources(HarmonizerTest PRIVATE Tests/HarmonizerTest.cpp ${HARMONIZER_SOURCES})
target_include_directories(HarmonizerTest PRIVATE ${EFFECTS_DIR})
target_compile_definitions(HarmonizerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(HarmonizerTest PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

#==============================================================================
# Test du moteur Reverse Shift, sans JUCE : cible ReverseTest
#==============================================================================
add_executable(ReverseTest Tests/ReverseTest.cpp)
target_include_directories(ReverseTest PRIVATE ${EFFECTS_DIR}/Reverse)
target_compile_features(ReverseTest PRIVATE cxx_std_17)

#==============================================================================
# Test du moteur Ultra-Tap, sans JUCE : cible UltraTapTest
#==============================================================================
add_executable(UltraTapTest Tests/UltraTapTest.cpp)
target_include_directories(UltraTapTest PRIVATE ${EFFECTS_DIR}/UltraTap)
target_compile_features(UltraTapTest PRIVATE cxx_std_17)

#==============================================================================
# Test du moteur Band Delay, sans JUCE : cible BandDelayTest
#==============================================================================
add_executable(BandDelayTest Tests/BandDelayTest.cpp)
target_include_directories(BandDelayTest PRIVATE ${EFFECTS_DIR}/BandDelay)
target_compile_features(BandDelayTest PRIVATE cxx_std_17)

#==============================================================================
# Images PNG des interfaces (vérification sans Logic) : cible EditorSnapshot
#==============================================================================
juce_add_console_app(EditorSnapshot PRODUCT_NAME "EditorSnapshot")
target_sources(EditorSnapshot PRIVATE Tests/EditorSnapshot.cpp
    Chain/H3OChain/ChainProcessor.cpp Chain/H3OChain/ChainEditor.cpp Chain/H3OChain/ModulationPanel.cpp Chain/H3OChain/ChainPresets.cpp
    ${AMP_SOURCES} ${HARMONIZER_SOURCES} ${REVERSE_SOURCES} ${CHORUS_SOURCES}
    ${DELAY_SOURCES} ${BANDDELAY_SOURCES} ${ULTRATAP_SOURCES} ${REVERB_SOURCES} ${LAYEREDSHIFT_SOURCES} ${SWEPTFILTER_SOURCES} ${COMPRESSOR_SOURCES})
target_include_directories(EditorSnapshot PRIVATE ${EFFECTS_DIR} Chain/H3OChain)
target_compile_definitions(EditorSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
audiofx_link_nam(EditorSnapshot)
target_link_libraries(EditorSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

#==============================================================================
# Tests du routage de H3O Chain (ordre, branches A / B, sessions) : cible ChainTest
#==============================================================================
juce_add_console_app(ChainTest PRODUCT_NAME "ChainTest")
target_sources(ChainTest PRIVATE Tests/ChainTest.cpp
    Chain/H3OChain/ChainProcessor.cpp Chain/H3OChain/ChainEditor.cpp Chain/H3OChain/ModulationPanel.cpp Chain/H3OChain/ChainPresets.cpp Chain/H3OChain/FactoryPresets.cpp
    ${AMP_SOURCES} ${HARMONIZER_SOURCES} ${REVERSE_SOURCES} ${CHORUS_SOURCES}
    ${DELAY_SOURCES} ${BANDDELAY_SOURCES} ${ULTRATAP_SOURCES} ${REVERB_SOURCES} ${LAYEREDSHIFT_SOURCES} ${SWEPTFILTER_SOURCES} ${COMPRESSOR_SOURCES})
target_include_directories(ChainTest PRIVATE ${EFFECTS_DIR} Chain/H3OChain)
target_compile_definitions(ChainTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
audiofx_link_nam(ChainTest)
target_link_libraries(ChainTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

#==============================================================================
# Mesures du moteur de réverbération, sans JUCE : cible ReverbTest
#==============================================================================
add_executable(ReverbTest Tests/ReverbTest.cpp)
target_include_directories(ReverbTest PRIVATE ${EFFECTS_DIR}/Reverb)
target_compile_features(ReverbTest PRIVATE cxx_std_17)

#==============================================================================
# Test du moteur Layered Shift, sans JUCE : cible LayeredShiftTest
#==============================================================================
add_executable(LayeredShiftTest Tests/LayeredShiftTest.cpp)
target_include_directories(LayeredShiftTest PRIVATE ${EFFECTS_DIR})
target_compile_features(LayeredShiftTest PRIVATE cxx_std_17)

#==============================================================================
# Test du moteur Swept Filter, sans JUCE : cible SweptFilterTest
#==============================================================================
add_executable(SweptFilterTest Tests/SweptFilterTest.cpp)
target_include_directories(SweptFilterTest PRIVATE ${EFFECTS_DIR}/SweptFilter)
target_compile_features(SweptFilterTest PRIVATE cxx_std_17)
