# Tidechart — herramienta MIDI de acordes y melodías por tonalidad. Se compila desde el CMakeLists.txt raíz (que ya trae
# JUCE y C++20):
#   cmake --build build --config Release --target Tidechart_VST3

# --- Plugin --------------------------------------------------------------------
# Plugin MIDI: IS_SYNTH TRUE para que FL lo cargue como generador en el Channel Rack (le llegan las notas del Piano
# Roll) y NEEDS_MIDI_OUTPUT TRUE para mandar MIDI a otro instrumento. IS_MIDI_EFFECT se descarta: en VST3 no existe esa
# categoría y FL lo trata peor.
juce_add_plugin(Tidechart
    PRODUCT_NAME             "Tidechart"
    COMPANY_NAME             "Deepwater"
    PLUGIN_MANUFACTURER_CODE Undw          # el fabricante de toda la suite (no cambiarlo)
    PLUGIN_CODE              Tdc1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Instrument Tools
)

# Todo el código del plugin (también lo compila TidechartProcessorTests).
set(TIDECHART_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/presets/TidechartPresets.cpp
)

target_sources(Tidechart PRIVATE ${TIDECHART_PLUGIN_SOURCES})

# --- Presets de fábrica (Fase 9) ---------------------------------------------------
# Los .tcpreset de Presets/Factory se compilan DENTRO del plugin (BinaryData.h), como en el resto de la suite.
# CONFIGURE_DEPENDS: un preset nuevo se detecta al compilar, sin reconfigurar a mano.
file(GLOB_RECURSE TIDECHART_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.tcpreset")
juce_add_binary_data(TidechartPresetData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     ${TIDECHART_FACTORY_PRESETS}
)
target_include_directories(Tidechart PRIVATE Source)

target_compile_definitions(Tidechart
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(Tidechart
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        TidechartPresetData
        deepwater::dsp                 # librería compartida de la suite (libs/dsp): el reloj del host, el buzón a la GUI
        deepwater::theory              # teoría musical (libs/theory): nombres de nota, acordes, escalas, grados
        deepwater::presets             # presets de la suite (libs/presets)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---------------------------------------------------------------------
# ctest --test-dir build -C Release --output-on-failure
option(TIDECHART_BUILD_TESTS "Compilar los tests unitarios de Tidechart" ${DEEPWATER_BUILD_TESTS})

if(TIDECHART_BUILD_TESTS)
    # Tests de la etapa MIDI sin JUCE (NoteTracker, MidiStage, EventLog, nombres de nota).
    add_executable(TidechartTests tests/MidiTests.cpp)
    target_include_directories(TidechartTests PRIVATE Source)
    target_link_libraries(TidechartTests PRIVATE deepwater::dsp deepwater::theory)

    if(MSVC)
        target_compile_options(TidechartTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(TidechartTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME TidechartTests COMMAND TidechartTests)

    # Tests del procesador completo (con JUCE): processBlock con MIDI al azar, bloques de cualquier tamaño, bypass,
    # reinicios del audio, pánico, un host que reproduce, salta y hace loop; sin notas colgadas ni reservas de memoria.
    juce_add_console_app(TidechartProcessorTests PRODUCT_NAME "TidechartProcessorTests")
    target_sources(TidechartProcessorTests PRIVATE tests/ProcessorTests.cpp ${TIDECHART_PLUGIN_SOURCES})
    target_include_directories(TidechartProcessorTests PRIVATE Source)
    target_compile_definitions(TidechartProcessorTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Tidechart\""
    )
    target_link_libraries(TidechartProcessorTests
        PRIVATE
            juce::juce_audio_utils
            TidechartPresetData
            deepwater::dsp
            deepwater::theory
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME TidechartProcessorTests COMMAND TidechartProcessorTests)

    # Tests de presets (Fase 9): cada preset de fábrica se lee sin avisos y suena; guardar y cargar, el "*", el estado del
    # proyecto. Con JUCE (parámetros, XML): compila el procesador, como los de los otros plugins.
    juce_add_console_app(TidechartPresetTests PRODUCT_NAME "TidechartPresetTests")
    target_sources(TidechartPresetTests PRIVATE tests/PresetTests.cpp ${TIDECHART_PLUGIN_SOURCES})
    target_include_directories(TidechartPresetTests PRIVATE Source)
    target_compile_definitions(TidechartPresetTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Tidechart\""
    )
    target_link_libraries(TidechartPresetTests
        PRIVATE
            juce::juce_audio_utils
            TidechartPresetData
            deepwater::dsp
            deepwater::theory
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME TidechartPresetTests COMMAND TidechartPresetTests)
endif()
