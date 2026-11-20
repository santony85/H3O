# Plugin Simple Reverse : Reverse Shift façon H3000 (son joué à l'envers, transposé)
juce_add_plugin(SimpleReverse
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplereverse"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Srev
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Reverse"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Reverse a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleReverse
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${REVERSE_SOURCES})
