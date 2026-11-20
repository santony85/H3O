# Plugin Simple Delay : mêmes codes qu'avant, tes projets Logic le retrouvent
juce_add_plugin(SimpleDelay
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simpledelay"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sdly
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Delay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Delay a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleDelay
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${DELAY_SOURCES})
