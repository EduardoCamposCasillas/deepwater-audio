# Ebb — ducker / modulador de volumen para sidechain. Se compila desde el CMakeLists.txt raíz (que ya trae JUCE y C++20):
#   cmake --build build --config Release --target Ebb_VST3

# --- Plugin --------------------------------------------------------------------
juce_add_plugin(Ebb
    PRODUCT_NAME             "Ebb"
    COMPANY_NAME             "Deepwater"
    PLUGIN_MANUFACTURER_CODE Undw          # el fabricante de toda la suite (no cambiarlo)
    PLUGIN_CODE              Ebb1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE          # modo MIDI (Fase 4): las notas disparan la curva
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Fx Dynamics
)

target_sources(Ebb
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(Ebb PRIVATE Source)

target_compile_definitions(Ebb
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(Ebb
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        deepwater::dsp                 # librería DSP compartida de la suite (libs/dsp)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---------------------------------------------------------------------
# Ejecutable independiente de JUCE con los tests del motor y del reloj.
# Se ejecuta con:  ctest --test-dir build -C Release --output-on-failure
option(EBB_BUILD_TESTS "Compilar los tests unitarios de Ebb" ${DEEPWATER_BUILD_TESTS})

if(EBB_BUILD_TESTS)
    add_executable(EbbTests tests/EngineTests.cpp)
    target_include_directories(EbbTests PRIVATE Source)
    target_link_libraries(EbbTests PRIVATE deepwater::dsp)

    if(MSVC)
        target_compile_options(EbbTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(EbbTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME EbbTests COMMAND EbbTests)
endif()
