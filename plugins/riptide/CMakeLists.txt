# Riptide — saturador. Se compila desde el CMakeLists.txt raíz (que ya trae JUCE y C++20):
#   cmake --build build --config Release --target Riptide_VST3

# --- Plugin --------------------------------------------------------------------
juce_add_plugin(Riptide
    PRODUCT_NAME             "Riptide"
    COMPANY_NAME             "Deepwater"
    PLUGIN_MANUFACTURER_CODE Undw          # el mismo de Undertow: es el fabricante de toda la suite (no cambiarlo)
    PLUGIN_CODE              Rpt1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Fx Distortion
)

# Todo el código del plugin (también lo compila RiptidePresetTests).
set(RIPTIDE_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/presets/RiptidePresets.cpp
)

target_sources(Riptide PRIVATE ${RIPTIDE_PLUGIN_SOURCES})

# --- Presets de fábrica ----------------------------------------------------------
# Los .rtpreset de Presets/Factory se compilan DENTRO del plugin (BinaryData.h), como en Undertow. CONFIGURE_DEPENDS:
# un preset nuevo se detecta al compilar, sin reconfigurar a mano.
file(GLOB_RECURSE RIPTIDE_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.rtpreset")
juce_add_binary_data(RiptidePresetData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     ${RIPTIDE_FACTORY_PRESETS}
)

target_include_directories(Riptide PRIVATE Source)

target_compile_definitions(Riptide
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(Riptide
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        RiptidePresetData
        deepwater::dsp                 # librería DSP compartida de la suite (libs/dsp)
        deepwater::presets             # presets de la suite (libs/presets)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---------------------------------------------------------------------
# Ejecutable independiente de JUCE con los tests del motor de audio.
# Se ejecuta con:  ctest --test-dir build -C Release --output-on-failure
option(RIPTIDE_BUILD_TESTS "Compilar los tests unitarios de Riptide" ${DEEPWATER_BUILD_TESTS})

if(RIPTIDE_BUILD_TESTS)
    add_executable(RiptideTests tests/EngineTests.cpp)
    target_include_directories(RiptideTests PRIVATE Source)
    target_link_libraries(RiptideTests PRIVATE deepwater::dsp)

    if(MSVC)
        target_compile_options(RiptideTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(RiptideTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME RiptideTests COMMAND RiptideTests)

    # Tests de presets (Fase 9): necesitan JUCE (parámetros, estado, XML), así que son una app de consola aparte que
    # compila el procesador completo. Validan cada preset de fábrica, guardar/cargar y el estado del proyecto, y pasan
    # señales de prueba por cada preset para medir su nivel, que no reserve memoria y que cambiar de preset no haga clic.
    juce_add_console_app(RiptidePresetTests PRODUCT_NAME "RiptidePresetTests")
    target_sources(RiptidePresetTests PRIVATE tests/PresetTests.cpp ${RIPTIDE_PLUGIN_SOURCES})
    target_include_directories(RiptidePresetTests PRIVATE Source)
    target_compile_definitions(RiptidePresetTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Riptide\""
    )
    target_link_libraries(RiptidePresetTests
        PRIVATE
            juce::juce_audio_utils
            RiptidePresetData
            deepwater::dsp
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME RiptidePresetTests COMMAND RiptidePresetTests)
endif()
