# Plugin H3O Octaver : octaves en dessous et au-dessus, analogique ou polyphonique
juce_add_plugin(H3OOctaver
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.octaver"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Soct
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Octaver"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/H3OIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Octaver a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OOctaver
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${OCTAVER_SOURCES})
