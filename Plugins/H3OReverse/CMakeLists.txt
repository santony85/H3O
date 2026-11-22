# Plugin H3O Reverse : Reverse Shift façon H3000 (son joué à l'envers, transposé)
juce_add_plugin(H3OReverse
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.reverse"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Srev
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Reverse"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/H3OIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Reverse a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OReverse
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${REVERSE_SOURCES})
