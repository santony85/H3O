# Plugin Simple Harmonizer : mêmes codes qu'avant, tes projets Logic le retrouvent
juce_add_plugin(SimpleHarmonizer
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simpleharmonizer"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Shrm
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Harmonizer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Harmonizer a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleHarmonizer
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${HARMONIZER_SOURCES})
