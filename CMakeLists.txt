cmake_minimum_required(VERSION 3.22)

project(UndertowSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- JUCE vía FetchContent -----------------------------------------------------
# Se fija a un tag (no a una rama) para que el build sea reproducible:
# el mismo CMakeLists.txt siempre descarga exactamente el mismo JUCE.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin --------------------------------------------------------------------
juce_add_plugin(UndertowSynth
    PRODUCT_NAME             "Undertow Synth"
    COMPANY_NAME             "Undertow"
    PLUGIN_MANUFACTURER_CODE Undw          # 4 caracteres, al menos una mayúscula
    PLUGIN_CODE              Uwt1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Instrument Synth
)

# Todo el código del plugin (también lo compila UndertowPresetTests).
set(UNDERTOW_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/Wavetable.cpp
    Source/presets/PresetManager.cpp
    Source/synth/VoiceManager.cpp
    Source/synth/WavetableBank.cpp
)

target_sources(UndertowSynth PRIVATE ${UNDERTOW_PLUGIN_SOURCES})

# --- Presets de fábrica ----------------------------------------------------------
# Los .uwpreset de Presets/Factory se compilan DENTRO del plugin (BinaryData.h): no hay que instalar archivos aparte
# y nadie puede borrarlos por error. CONFIGURE_DEPENDS: un preset nuevo se detecta al compilar, sin reconfigurar a mano.
file(GLOB_RECURSE UNDERTOW_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.uwpreset")
juce_add_binary_data(UndertowPresetData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     ${UNDERTOW_FACTORY_PRESETS}
)

target_include_directories(UndertowSynth PRIVATE Source)

target_compile_definitions(UndertowSynth
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(UndertowSynth
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        juce::juce_dsp
        UndertowPresetData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---------------------------------------------------------------------
# Ejecutable independiente de JUCE con los tests de DSP y de voces.
# Se ejecuta con:  ctest --test-dir build -C Release --output-on-failure
option(UNDERTOW_BUILD_TESTS "Compilar los tests unitarios" ON)

if(UNDERTOW_BUILD_TESTS)
    enable_testing()

    add_executable(UndertowTests
        tests/DspTests.cpp
        Source/dsp/Wavetable.cpp
        Source/synth/VoiceManager.cpp
        Source/synth/WavetableBank.cpp
    )
    target_include_directories(UndertowTests PRIVATE Source)

    if(MSVC)
        target_compile_options(UndertowTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(UndertowTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME UndertowTests COMMAND UndertowTests)

    # Tests de presets: necesitan JUCE (parámetros, estado, XML), así que son una app de consola aparte que compila el
    # procesador completo. Validan cada preset de fábrica, guardar/cargar, el estado del proyecto y renderizan cada
    # preset para medir su nivel.
    juce_add_console_app(UndertowPresetTests PRODUCT_NAME "UndertowPresetTests")
    target_sources(UndertowPresetTests PRIVATE tests/PresetTests.cpp ${UNDERTOW_PLUGIN_SOURCES})
    target_include_directories(UndertowPresetTests PRIVATE Source)
    target_compile_definitions(UndertowPresetTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Undertow Synth\""
    )
    target_link_libraries(UndertowPresetTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            UndertowPresetData
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME UndertowPresetTests COMMAND UndertowPresetTests)
endif()
