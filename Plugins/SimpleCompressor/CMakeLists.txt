# Plugin Simple Compressor : compresseur de studio (Clean, Opto, Punch)
juce_add_plugin(SimpleCompressor
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplecompressor"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Scmp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Compressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Compressor a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleCompressor
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${COMPRESSOR_SOURCES})
