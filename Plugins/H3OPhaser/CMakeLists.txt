# Plugin H3O Phaser : phaser stéréo de 2 à 12 étages
juce_add_plugin(H3OPhaser
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.phaser"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sphs
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Phaser"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/H3OIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Phaser a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OPhaser
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${PHASER_SOURCES})
