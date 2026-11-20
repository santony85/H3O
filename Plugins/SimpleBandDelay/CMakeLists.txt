# Plugin Simple Band Delay : échos filtrés façon Band Delay du H3000
juce_add_plugin(SimpleBandDelay
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplebanddelay"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sbdl
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Band Delay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Band Delay a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleBandDelay
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${BANDDELAY_SOURCES})
