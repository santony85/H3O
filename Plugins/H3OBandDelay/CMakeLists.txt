# Plugin H3O Band Delay : échos filtrés façon Band Delay du H3000
juce_add_plugin(H3OBandDelay
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.banddelay"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sbdl
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Band Delay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Band Delay a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OBandDelay
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${BANDDELAY_SOURCES})
