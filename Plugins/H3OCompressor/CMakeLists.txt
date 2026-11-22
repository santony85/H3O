# Plugin H3O Compressor : compresseur de studio (Clean, Opto, Punch)
juce_add_plugin(H3OCompressor
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.compressor"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Scmp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Compressor"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/H3OIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Compressor a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OCompressor
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${COMPRESSOR_SOURCES})
