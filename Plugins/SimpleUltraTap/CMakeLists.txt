# Plugin Simple Ultra-Tap : delay multi-répétitions façon Ultra-Tap du H3000
juce_add_plugin(SimpleUltraTap
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simpleultratap"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Sutp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Ultra-Tap"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Ultra-Tap a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleUltraTap
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${ULTRATAP_SOURCES})
