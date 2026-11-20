# Simple Chain : le multi-effets. Contient les effets en série (Amp → Harmonizer → Reverse → Chorus → Delay → Band Delay → Ultra-Tap).
# Utilisable comme plugin dans Logic ou comme application autonome.
juce_add_plugin(SimpleChain
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simplechain"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Schn
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Chain"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Chain a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleChain
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainEditor.cpp
    ${AMP_SOURCES}
    ${HARMONIZER_SOURCES}
    ${REVERSE_SOURCES}
    ${CHORUS_SOURCES}
    ${DELAY_SOURCES}
    ${BANDDELAY_SOURCES}
    ${ULTRATAP_SOURCES})

audiofx_link_nam(SimpleChain)
