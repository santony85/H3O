# Plugin Simple Layered Shift : couches transposées, désaccordées et balayées façon H3000
juce_add_plugin(SimpleLayeredShift
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplelayeredshift"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Slsh
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Layered Shift"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Layered Shift a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleLayeredShift
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${LAYEREDSHIFT_SOURCES})
