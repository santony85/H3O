# Plugin Simple Reverb : réverbérations Room, Hall et Plate façon H3000
juce_add_plugin(SimpleReverb
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplereverb"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Srvb
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Reverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Reverb a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleReverb
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${REVERB_SOURCES})
