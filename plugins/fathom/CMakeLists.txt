# Fathom — EQ paramétrico con analizador. Se compila desde el CMakeLists.txt raíz (que ya trae JUCE y C++20):
#   cmake --build build --config Release --target Fathom_VST3

# --- Plugin --------------------------------------------------------------------
juce_add_plugin(Fathom
    PRODUCT_NAME             "Fathom"
    COMPANY_NAME             "Deepwater"
    PLUGIN_MANUFACTURER_CODE Undw          # el fabricante de toda la suite (no cambiarlo)
    PLUGIN_CODE              Fth1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Fx EQ
)

# Todo el código del plugin (también lo compilan FathomInstanceTests, FathomPresetTests y FathomUiSnapshot).
set(FATHOM_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/presets/FathomPresets.cpp
)

target_sources(Fathom PRIVATE ${FATHOM_PLUGIN_SOURCES})

# --- Presets de fábrica (Fase 15) --------------------------------------------------
# Los .fhpreset de Presets/Factory se compilan DENTRO del plugin (BinaryData.h), como en Undertow, Riptide y Ebb.
# CONFIGURE_DEPENDS: un preset nuevo se detecta al compilar, sin reconfigurar a mano.
file(GLOB_RECURSE FATHOM_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.fhpreset")
juce_add_binary_data(FathomPresetData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     ${FATHOM_FACTORY_PRESETS}
)

target_include_directories(Fathom PRIVATE Source)

target_compile_definitions(Fathom
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(Fathom
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        FathomPresetData
        deepwater::dsp                 # librería DSP compartida de la suite (libs/dsp)
        deepwater::presets             # presets de la suite (libs/presets)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---------------------------------------------------------------------
# Ejecutable independiente de JUCE con los tests del motor.
# Se ejecuta con:  ctest --test-dir build -C Release --output-on-failure
option(FATHOM_BUILD_TESTS "Compilar los tests unitarios de Fathom" ${DEEPWATER_BUILD_TESTS})

if(FATHOM_BUILD_TESTS)
    add_executable(FathomTests tests/EngineTests.cpp)
    target_include_directories(FathomTests PRIVATE Source)
    target_link_libraries(FathomTests PRIVATE deepwater::dsp)

    if(MSVC)
        target_compile_options(FathomTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(FathomTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME FathomTests COMMAND FathomTests)

    # Fase 14: con JUCE, varios procesadores completos en el mismo proceso (las otras instancias).
    juce_add_console_app(FathomInstanceTests PRODUCT_NAME "FathomInstanceTests")
    target_sources(FathomInstanceTests PRIVATE tests/InstanceTests.cpp ${FATHOM_PLUGIN_SOURCES})
    target_include_directories(FathomInstanceTests PRIVATE Source)
    target_compile_definitions(FathomInstanceTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Fathom\""
    )
    target_link_libraries(FathomInstanceTests
        PRIVATE
            juce::juce_audio_utils
            FathomPresetData
            deepwater::dsp
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME FathomInstanceTests COMMAND FathomInstanceTests)

    # Fase 15, con JUCE: los presets de fábrica (que se lean, sus notas, que suenen sin NaN ni reservas de memoria y
    # con el volumen igualado), guardar/cargar, el estado del proyecto, el deshacer y cambiar de preset sonando.
    juce_add_console_app(FathomPresetTests PRODUCT_NAME "FathomPresetTests")
    target_sources(FathomPresetTests PRIVATE tests/PresetTests.cpp ${FATHOM_PLUGIN_SOURCES})
    target_include_directories(FathomPresetTests PRIVATE Source)
    target_compile_definitions(FathomPresetTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            "JucePlugin_Name=\"Fathom\""
    )
    target_link_libraries(FathomPresetTests
        PRIVATE
            juce::juce_audio_utils
            FathomPresetData
            deepwater::dsp
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME FathomPresetTests COMMAND FathomPresetTests)

    # Fase 16, con JUCE: automatización extrema sobre el procesador completo (todos los parámetros al azar, barridos
    # rápidos sin clics y el audio en tiempo real mientras el hilo de mensajes cambia presets, A/B, deshacer y el proyecto).
    juce_add_console_app(FathomAutomationTests PRODUCT_NAME "FathomAutomationTests")
    target_sources(FathomAutomationTests PRIVATE tests/AutomationTests.cpp ${FATHOM_PLUGIN_SOURCES})
    target_include_directories(FathomAutomationTests PRIVATE Source)
    target_compile_definitions(FathomAutomationTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1   # runDispatchLoopUntil: los temporizadores del procesador corren
            "JucePlugin_Name=\"Fathom\""
    )
    target_link_libraries(FathomAutomationTests
        PRIVATE
            juce::juce_audio_utils
            FathomPresetData
            deepwater::dsp
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_test(NAME FathomAutomationTests COMMAND FathomAutomationTests)

    # Herramienta (no está en ctest): capturas PNG de la GUI con varias instancias sonando.
    #   FathomUiSnapshot <carpeta>
    juce_add_console_app(FathomUiSnapshot PRODUCT_NAME "FathomUiSnapshot")
    target_sources(FathomUiSnapshot PRIVATE tests/UiSnapshot.cpp ${FATHOM_PLUGIN_SOURCES})
    target_include_directories(FathomUiSnapshot PRIVATE Source)
    target_compile_definitions(FathomUiSnapshot
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1   # runDispatchLoopUntil: los temporizadores del editor corren
            "JucePlugin_Name=\"Fathom\""
    )
    target_link_libraries(FathomUiSnapshot
        PRIVATE
            juce::juce_audio_utils
            FathomPresetData
            deepwater::dsp
            deepwater::presets
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
