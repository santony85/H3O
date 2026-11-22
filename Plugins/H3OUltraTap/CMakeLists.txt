# Plugin H3O Ultra-Tap : delay multi-répétitions façon Ultra-Tap du H3000
juce_add_plugin(H3OUltraTap
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.ultratap"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sutp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Ultra-Tap"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Ultra-Tap a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OUltraTap
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${ULTRATAP_SOURCES})
