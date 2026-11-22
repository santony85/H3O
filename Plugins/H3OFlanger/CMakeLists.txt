# Plugin H3O Flanger : flanger stéréo, classique ou through-zero
juce_add_plugin(H3OFlanger
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.flanger"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sflg
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Flanger"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/H3OIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Flanger a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OFlanger
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${FLANGER_SOURCES})
