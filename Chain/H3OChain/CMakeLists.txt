# H3O Chain : le multi-effets. Contient les effets en série (Compressor → Filter → Amp → Harmonizer → Reverse → Layered Shift → Chorus → Delay → Band Delay → Ultra-Tap → Reverb).
# Utilisable comme plugin dans Logic ou comme application autonome.
juce_add_plugin(H3OChain
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.chain"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Schn
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Chain"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Chain a besoin de l'entrée audio pour traiter le son de ton micro ou de ton instrument."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OChain
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainRouting.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainPresets.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainPresets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ModulationSources.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ModulationPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ModulationPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ChainEditor.cpp
    ${SWEPTFILTER_SOURCES}
    ${AMP_SOURCES}
    ${HARMONIZER_SOURCES}
    ${REVERSE_SOURCES}
    ${LAYEREDSHIFT_SOURCES}
    ${CHORUS_SOURCES}
    ${DELAY_SOURCES}
    ${BANDDELAY_SOURCES}
    ${ULTRATAP_SOURCES}
    ${REVERB_SOURCES}
    ${COMPRESSOR_SOURCES})

audiofx_link_nam(H3OChain)
