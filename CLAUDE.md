# CLAUDE.md — Suite de plugins de audio (monorepo)

## Visión
Una suite propia de plugins de audio de nivel profesional para FL Studio (Windows), construida con
C++ moderno y JUCE. Cada plugin tiene como referencia de calidad a los estándares de la industria,
pero con nombre, diseño y código propios.

El dueño del repo está APRENDIENDO programación de audio, DSP, diseño sonoro y mezcla mientras construye.
El objetivo es doble: plugins de nivel profesional y que él entienda cada parte y sepa usarlos.

Este archivo contiene las reglas GENERALES. Cada plugin tiene su propio `CLAUDE.md` en su carpeta con
su hoja de ruta y lo específico; leer ambos antes de trabajar en un plugin.

## Estructura
```
CMakeLists.txt          # raíz: JUCE vía FetchContent una sola vez + add_subdirectory de libs y plugins
libs/
  dsp/                  # librería compartida deepwater::dsp (sin JUCE ni GUI), con tests
    include/deepwater/dsp/   # headers:  #include "deepwater/dsp/Svf.h"
    src/  tests/
  presets/              # presets de la suite deepwater::presets (con JUCE): formato XML, biblioteca, barra y navegador
    include/deepwater/presets/   # headers:  #include "deepwater/presets/PresetPanel.h"
    src/
plugins/
  undertow/             # sintetizador wavetable
  riptide/              # saturador
  ebb/                  # ducker para sidechain
  fathom/               # EQ paramétrico con analizador
  <plugin>/             # cada plugin: CMakeLists.txt, Source/, tests/, CLAUDE.md, APRENDIZAJE.md
scripts/
  null_test.py          # resta dos carpetas de WAV (verificar que un refactor no cambia el sonido)
  level_match.py        # iguala la sonoridad (LUFS) de varias versiones de un audio para compararlas a oído (y a ciegas)
```
- Cada plugin compila a su propio VST3/Standalone independiente. Todos comparten `PLUGIN_MANUFACTURER_CODE Undw` y
  `COMPANY_NAME "Deepwater"`; cada uno tiene su `PLUGIN_CODE` (Undertow `Uwt1`, Riptide `Rpt1`, Ebb `Ebb1`, Fathom `Fth1`).
- Compilar solo el plugin en el que se trabaja: `cmake --build build --config Release --target <Plugin>_VST3`.
- La librería compartida se enlaza estáticamente dentro de cada plugin (`target_link_libraries(... deepwater::dsp)`).

## Comandos
```
cmake -B build -G "Visual Studio 17 2022" -A x64        # VS Community 2022 en esta máquina
cmake --build build --config Release                    # toda la suite
cmake --build build --config Release --target <Plugin>_VST3
ctest --test-dir build -C Release --output-on-failure   # tests de la librería y de todos los plugins
tools/pluginval/pluginval.exe --strictness-level 10 --sample-rates 44100,48000,88200,96000 --block-sizes 1,7,64,256,512,1024,4096 --repeat 3 --randomise --validate "<ruta al .vst3>"
```
- CMake está en `C:\Program Files\CMake\bin` y no está en el PATH: usar la ruta completa o agregarlo.
- Los artefactos quedan en `build/plugins/<carpeta>/<Target>_artefacts/Release/{VST3,Standalone}`. En FL Studio basta
  con agregar `build\plugins` como ruta de búsqueda (tipo VST3): FL busca en las subcarpetas.
- `tools/` (pluginval) no está en git: se descarga a mano.
- Si FL tiene abierto un VST3 de `build/`, el enlazador no puede reemplazarlo: cerrar FL o compilar en otra carpeta
  (`cmake -B out/pv ...`).

## Librería compartida (`libs/dsp`, namespace `deepwater::dsp`)
- Contiene: oversampling halfband ×2 (`Halfband.h`) y en cascada ×2–×16 alrededor de una curva, con latencia entera
  (`Oversampler.h`; los dos con versión en bloque, idéntica bit a bit y unas 2 veces más rápida), filtro DC (`DcBlocker.h`), SVF TPT/ZDF (`Svf.h`), filtros de EQ sobre el SVF con su respuesta en frecuencia exacta (`EqFilter.h`, de Fathom: campana, shelves, tilt, notch, pasa banda, all-pass, cortes Butterworth de 1 a 16 polos con resonancia y flat tilt, en cascadas de hasta 8 etapas), las mismas formas con la magnitud del
  prototipo analógico también cerca de Nyquist y sin latencia (`MatchedEq.h`, de Fathom: diseño "matched" a la Vicanek sobre el mismo SVF, con los
  prototipos `AnalogBiquad`/`AnalogCascade` para medir), crossover Linkwitz-Riley y divisor multibanda
  (`Crossover.h`), filtro de 1 polo TPT y tilt EQ (`OnePole.h`), suavizado de parámetros y fundidos
  (`Smoothing.h`), línea de retardo con lectura lineal, cúbica y Lagrange (`DelayLine.h`), SIMD SSE2 (`Simd.h`),
  `fastTanh` (`FastMath.h`), FFT (`Fft.h`), toma de señal lock-free audio → GUI (`AnalyzerTap.h`), buzón del último valor audio → GUI (`TripleBuffer.h`, venía de Ebb), analizador de
  espectro calibrado, fuera del audio, con picos afinados entre bins (`SpectrumAnalyzer.h`, venía de Riptide), `midiNoteToHz` (`Pitch.h`), dB ↔ ganancia (`Decibels.h`), medidores de
  pico/RMS (`LevelMeter.h`), LFO con divisiones de tempo (`Lfo.h`, venía de Undertow), curva por puntos con tensión
  (`BreakpointCurve.h`, venía de Ebb), seguidor de envolvente
  (`EnvelopeFollower.h`) y detector de golpes por umbral con histéresis y retención (`TransientDetector.h`, venía de Ebb).
- Suavizado: usar `LinearRamp` (llega exacto al objetivo). `SmoothedParameter` tiene un bug conocido (nunca termina de
  llegar) y solo lo usa Undertow; arreglarlo cambiaría su sonido.
- Sin JUCE ni GUI, C++20 estándar. Lo que corre en el hilo de audio va en headers (para que se inlinee); lo que corre
  fuera del audio puede ir en `src/`. Tests en `libs/dsp/tests` (ejecutable `DeepwaterDspTests`, en ctest).
- Mover código de un plugin a la librería es un refactor que NO debe cambiar el sonido: renderizar antes y después
  (p. ej. `UndertowPresetTests --render`) y comprobar con `scripts/null_test.py` que la resta es silencio.
- Cambiar una pieza de la librería afecta a TODOS los plugins que la usan: correr ctest completo y los null tests.

## Librería de presets (`libs/presets`, namespace `deepwater::presets`)
- Nació en Undertow (Fase 10) y pasó a la librería con Riptide (Fase 9). Usa JUCE: es una librería INTERFACE con
  fuentes (cada plugin compila `src/` con su configuración de JUCE) y no tiene tests propios: la prueban
  `UndertowPresetTests`, `RiptidePresetTests` y `EbbPresetTests` (cambiarla = correr los tres).
- `PresetManager.h`: formato XML legible en unidades reales (lo que falta = valor por defecto; en listas, un número es el
  índice de la opción), fábrica (BinaryData del plugin) + carpeta del usuario, preset actual y "*" en el proyecto.
  Cada plugin describe lo suyo en un `PresetFormat` (etiqueta raíz, extensión, categorías con "Init" primero y
  "Otros" al final, carpeta, parámetros excluidos como el bypass, archivos de fábrica) y, opcional, un "estado extra"
  que no es un parámetro (la curva de Ebb: un `ValueTree` guardado como elemento hijo del preset que cuenta para el "*").
- GUI: `PresetPanel.h` (barra + navegador + panel de guardar y todo su comportamiento; el editor solo llama a `addTo`,
  `setBounds`, `update` y `onMessage`), con los colores del plugin en una `Palette`.
- Un plugin nuevo: `Source/presets/<Plugin>Presets.cpp` con su `format()`, `Presets/Factory/<Categoría>/*.<ext>`
  compilados con `juce_add_binary_data`, y un `<Plugin>PresetTests` (ver los de Riptide).

## Plan de plugins (orden acordado)
| # | Plugin | Referencia de nivel | Estado |
|---|--------|---------------------|--------|
| 1 | Undertow — sintetizador wavetable (FM, warp, unison, matriz de modulación, FX) | Serum 2, Vital | ✅ Terminado (ampliación en curso: cuerda pulsada) |
| 2 | Riptide — saturador (modos cinta/bulbo/transistor, multibanda, auto gain) | Decapitator, Saturn 2 | ✅ Terminado |
| 3 | Ebb — ducker / modulador de volumen para sidechain (tempo, MIDI y audio; por bandas) | Kickstart 2, LFOTool, ShaperBox (módulo de volumen) | ✅ Terminado |
| 4 | Fathom — EQ paramétrico con analizador (24 bandas, dinámico, M/S, fase lineal, dinámica espectral) | FabFilter Pro-Q 4 | En curso (Fase 9) |
| 5 | Compresor (estilos limpio y FET/1176) | FabFilter Pro-C 2, 1176 | Pendiente |
| 6 | Delay (sync, ping-pong, filtros y modulación en el feedback) | Valhalla Delay | Pendiente |
| 7 | Reverb algorítmica (FDN, difusión, modulación; modos vintage y espacial) | Valhalla VintageVerb, Supermassive | Pendiente |

- Ebb va antes del compresor porque es más simple (una curva de ganancia, sin detector de nivel continuo en su
  modo principal) y reutiliza las envolventes y el suavizado de `libs/dsp`.
- El delay va antes de la reverb porque reutiliza sus piezas (buffers circulares, filtros, modulación).
- Cada plugin nuevo empieza moviendo a `libs/dsp` lo reutilizable que haya construido el anterior.
- Al iniciar un plugin nuevo, crear su carpeta con su `CLAUDE.md` (hoja de ruta por fases) y su `APRENDIZAJE.md`.

## Modo de trabajo (todos los plugins)
- SIEMPRE por fases pequeñas, una a la vez. No avanzar hasta que yo confirme que la probé en FL Studio.
- Antes de implementar un concepto de DSP nuevo, explicarlo en español en 1–3 párrafos:
  qué problema resuelve, la idea matemática básica y cómo se traduce a código.
- Si hay varias formas de resolver algo, explicar brevemente las opciones y por qué se elige una.
- Al terminar cada fase: compilar sin warnings, pasar pluginval si aplica, explicar cómo probar en FL,
  actualizar "Estado actual" del `CLAUDE.md` del plugin y hacer un commit descriptivo.
- Comentarios en español, educativos pero concisos (el "por qué", no el "qué"). Nombres de código en inglés.
- Nunca copiar código de proyectos GPL/AGPL de terceros ni replicar algoritmos propietarios.
  Se pueden estudiar ideas publicadas (papers, blogs, libros).

## Aprendizaje (todos los plugins)
Al terminar cada fase, en el `APRENDIZAJE.md` del plugin:
1. Qué hace cada control al sonido, en lenguaje musical, relacionado con el analizador y la forma de onda.
2. Ejercicio de escucha guiado en FL Studio (con volumen igualado en los efectos).
3. Receta de sonido o de uso en mezcla con lo construido hasta esa fase, explicando el porqué.
4. Reto sin receta: yo lo intento primero, luego pistas.
5. Vocabulario nuevo con ejemplo en música real.
6. En efectos: errores comunes y cuándo NO usarlo.
Los presets de fábrica incluyen notas educativas.

## Stack técnico
- C++20, MSVC (Visual Studio 2026 o 2022), CMake >= 3.22.
- JUCE 9 (tag 9.0.2) vía FetchContent en el CMake raíz, fijado a un tag estable (nunca a una rama).
- Formatos: VST3 + Standalone. Validación con pluginval.
- Reportar latencia al host (`setLatencySamples`) cuando oversampling, lookahead o filtros la agreguen.
- No copiar automáticamente a `C:\Program Files\Common Files\VST3`; FL Studio escanea las carpetas de artefactos.

## Reglas del hilo de audio (NO negociables)
- Nada de reservar/liberar memoria, locks, I/O, logging ni llamadas al sistema dentro de `processBlock`.
- Buffers reservados en `prepareToPlay`.
- Comunicación GUI ↔ audio solo con atómicos, `AudioProcessorValueTreeState` o colas lock-free.
- Parámetros suavizados (`SmoothedValue`); `juce::ScopedNoDenormals` al inicio de `processBlock`.
- Correcto a 44.1, 48, 88.2 y 96 kHz, con cualquier tamaño de buffer, en mono y estéreo.
- Bypass sin clics y con la misma latencia que el procesamiento activo.

## Estilo de código
- C++ moderno: RAII, `std::unique_ptr`, `std::array`, `constexpr`, `noexcept` en código de audio.
- Nada de `new`/`delete` manuales ni punteros crudos con propiedad.
- DSP en clases puras dentro de `libs/dsp` o `plugins/<plugin>/Source/dsp`, testeables sin JUCE GUI.
- Tests unitarios para piezas de DSP críticas.

## Git
- Commits pequeños y descriptivos, uno o más por fase.
- `.gitignore` excluye `build/`, `out/`, binarios, archivos de usuario de Visual Studio y dependencias descargadas.
- Nunca agregar atribuciones a Claude/Anthropic (`Co-Authored-By`, "Generated with...") en commits ni PRs.
- Licencia del repo: AGPLv3 (`LICENSE`), la que exige la licencia gratuita de código abierto de JUCE.