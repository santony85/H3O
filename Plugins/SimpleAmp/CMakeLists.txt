# Plugin Simple Amp : préampli / ampli Neural Amp Modeler (fichiers .nam) avec baffle par IR
juce_add_plugin(SimpleAmp
    COMPANY_NAME "Antony"
    BUNDLE_ID "com.antony.simpleamp"
    PLUGIN_MANUFACTURER_CODE Anty
    PLUGIN_CODE Samp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Simple Amp"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Simple Amp a besoin de l'entrée audio pour traiter le son de ta guitare."
    COPY_PLUGIN_AFTER_BUILD TRUE)

audiofx_setup_plugin(SimpleAmp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEntry.cpp
    ${AMP_SOURCES})

audiofx_link_nam(SimpleAmp)
