# Plugin H3O Amp : préampli / ampli Neural Amp Modeler (fichiers .nam) avec baffle par IR
juce_add_plugin(H3OAmp
    COMPANY_NAME "H3O"
    BUNDLE_ID "com.h3o.amp"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Samp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "H3O Amp"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "H3O Amp a besoin de l'entrée audio pour traiter le son de ta guitare."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(H3OAmp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${AMP_SOURCES})

audiofx_link_nam(H3OAmp)
