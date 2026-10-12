# CLAUDE.md — Ebb, ducker / modulador de volumen para sidechain

Plugin 3 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del ducker.

Nombre: **Ebb** (la marea que baja: el volumen "se retira" y vuelve). Target `Ebb`, `PLUGIN_CODE Ebb1`; fabricante
`Undw`, el de toda la suite; `COMPANY_NAME "Deepwater"`. Decidido con el usuario al empezar la Fase 1 (otras ideas que
se consideraron: Backwash, Swell, Lull, Moonpull, Breaker, Seiche).

## Objetivo del proyecto
Construir un ducker / modulador de volumen de calidad profesional (referencia de nivel: Nicky Romero Kickstart 2,
Xfer LFOTool y el módulo de volumen de Cableguys ShaperBox), con nombre, diseño y código propios, pero más completo
que Kickstart: tres formas de disparar la curva (tempo, MIDI y audio), editor de curvas y ducking por bandas.
Se usa en FL Studio sobre Windows, sobre bajos, 808, pads, acordes, buses y efectos creativos.

**Contexto de diseño clave:** el dueño del proyecto produce reggaetón experimental. En el dembow el kick NO cae en
cada tiempo (patrones irregulares, síncopas, kicks que se mueven entre secciones), así que un ducking "a negras"
fijo no sirve. Por eso los modos MIDI trigger (Fase 4) y audio sidechain (Fase 5) son tan importantes como el modo
tempo: el ducking debe poder seguir el patrón REAL del kick. Las pruebas y ejercicios usan patrones de dembow.

## Requisitos propios (además de las reglas del hilo de audio del raíz)
- La ganancia nunca salta: cambiar profundidad, duración, curva, modo o división en plena reproducción no debe
  causar clics. Una curva redisparada antes de terminar arranca desde la ganancia actual, no desde 1.
- La curva se sincroniza con la posición del host (PlayHead), no con un contador propio: al saltar en la línea de
  tiempo, hacer loop o cambiar el tempo en vivo, el ducking sigue cayendo en su lugar. Sin PlayHead (Standalone o
  host parado) debe comportarse de forma definida.
- Los disparos (MIDI y transitorios) son exactos a la muestra dentro del bloque, con cualquier tamaño de buffer.
- El lookahead del modo audio se informa al host con `setLatencySamples`; el bypass mantiene la misma latencia.
- Es un efecto: mono y estéreo. El bus sidechain (Fase 5) es opcional: sin él conectado el plugin sigue funcionando.

## Librería DSP compartida
Lo que Ebb va a usar de `libs/dsp` (`deepwater::dsp`):
- `Smoothing.h` (`LinearRamp`): suavizado de parámetros y de la curva, fundidos de bypass y de cambio de modo.
- `EnvelopeFollower.h`: base de la detección de transitorios del modo audio (Fase 5).
- `Lfo.h`: sus divisiones de tempo (1/4, 1/8, tresillos…) y la fase ligada al PlayHead sirven de modelo para el
  modo tempo (Fase 2).
- `DelayLine.h`: lookahead del modo audio (Fase 5).
- `Crossover.h`: crossover Linkwitz-Riley para el ducking por bandas (Fase 6), con suma plana.
- `Decibels.h`, `LevelMeter.h`, `AnalyzerTap.h`: profundidad en dB, medidores y la toma audio → GUI (Fase 7).
- `libs/presets` (`deepwater::presets`): presets, navegador y panel de guardar (Fase 8).

Antes de empezar, mover a la librería lo reutilizable de Riptide que haga falta (regla del raíz). Lo nuevo que sirva a
otros plugins va en la librería con tests: la curva por puntos con tensión (Fase 3) le sirve a futuros LFO y
envolventes, y el detector de transitorios (Fase 5) al compresor.

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target Ebb_VST3      # solo el plugin
build/plugins/ebb/Release/EbbTests.exe                      # tests del reloj y del motor (sin JUCE)
```
- VST3: `build/plugins/ebb/Ebb_artefacts/Release/VST3/Ebb.vst3`.
- Standalone: `build/plugins/ebb/Ebb_artefacts/Release/Standalone/Ebb.exe` (sin host: reloj libre a 120 BPM).

## Estructura
```
plugins/ebb/
  CMakeLists.txt           # plugin y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock, lectura del PlayHead y publicación para la GUI
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    TransportSnapshot.h      # lo que el audio publica para la GUI en cada bloque (TripleBuffer)
    dsp/HostClock.h          # reloj musical: posición del host por muestra, saltos, reloj libre; compás.tiempo.semicorchea
    dsp/Engine.h             # cadena de audio completa, sin JUCE
    gui/                     # tema, LookAndFeel, visor del transporte
  tests/EngineTests.cpp    # tests del reloj y del motor (sin JUCE)
```

## Aprendizaje de mezcla y diseño sonoro (igual de importante que el código)
Además de los 6 puntos del raíz (en efectos, siempre con el volumen igualado), este plugin se enfoca en:
- **Qué es el sidechain y por qué se usa**: una señal controla lo que le pasa a otra.
- **Cómo conviven kick y bajo en los graves**: los dos ocupan 40–120 Hz; si suenan a la vez se enmascaran y suman
  picos. Ducking del bajo para que el kick pase limpio.
- **Ducking sutil (mezcla) vs. bombeo exagerado (efecto creativo)**: 1–3 dB que no se oyen como efecto pero limpian
  el grave, contra el "pumping" del house francés o los pads que respiran.
- **Tempo vs. MIDI vs. audio**: cuándo conviene cada uno (patrón fijo, patrón irregular programado, kick grabado o
  con swing/humanizado) y qué se pierde con cada uno.
- Ejercicios sobre **patrones de dembow reales** (el clásico "boom-ch-boom-chick" y variaciones experimentales con
  el kick desplazado), comparando los tres modos sobre el mismo loop.

Actualizar el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/ebb/`) al final de cada fase. Los presets de fábrica
incluyen notas de para qué sirven y por qué.

## Hoja de ruta
- [x] Fase 1 — Proyecto base: `plugins/ebb/` con su `CMakeLists.txt` (VST3 + Standalone, efecto mono/estéreo),
      añadido al CMake raíz y enlazado a `deepwater::dsp`. Passthrough, lectura del tempo y la posición del host
      (PlayHead: BPM, compás, posición en negras, reproduciendo o no) mostrados en una GUI funcional. Tests y
      pluginval. Decidir antes: el nombre y su `PLUGIN_CODE`.
- [ ] Fase 2 — Modo tempo: curva de ducking sincronizada (1/2, 1/4, 1/8, 1/16, 1 compás y tresillos), controles de
      profundidad, duración y suavizado; sin clics en ningún cambio de parámetro.
- [ ] Fase 3 — Curvas: presets de forma (como las de Kickstart) y editor de curva propio con puntos y tensión.
- [ ] Fase 4 — Modo MIDI trigger: cada nota MIDI recibida dispara la curva (para seguir el patrón real del kick del
      dembow). Explicar cómo rutear MIDI hacia el plugin en FL Studio.
- [ ] Fase 5 — Modo audio sidechain: entrada sidechain real (bus auxiliar VST3), detección de transitorios con umbral
      y retención, y opción de lookahead (reportando latencia al host). Explicar cómo configurar el sidechain en el
      mixer de FL Studio.
- [ ] Fase 6 — Ducking por bandas: aplicar la curva solo a graves (crossover ajustable) para que el bajo le deje
      espacio al kick sin "bombear" los agudos; mix dry/wet.
- [ ] Fase 7 — GUI: visualización de la curva y de la forma de onda en vivo, cursor de reproducción, indicador de
      cuándo se dispara.
- [ ] Fase 8 — Presets con notas educativas (dembow, house, trap, pads bombeando como efecto creativo).
- [ ] Fase 9 — Pulido: CPU, pluginval estricto, pruebas a distintos tempos y cambios de tempo en vivo.

## Estado actual
Fase 1 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona.
- Nombre decidido con el usuario: Ebb (`Ebb1`), categoría VST3 `Fx Dynamics`. Layouts mono → mono y estéreo → estéreo
  (el bus sidechain llega en la Fase 5; `NEEDS_MIDI_INPUT` pasa a TRUE en la Fase 4). Latencia 0.
- `dsp/HostClock.h` (sin JUCE): `TransportInfo` (lo que dice el PlayHead, todo opcional), `HostClock` y
  `musicalPosition`. El reloj se resincroniza con el host al principio de CADA bloque y extrapola dentro del bloque
  (`ppqAt (offset)` = inicio + offset · bpm / (60 · fs)). Fuentes: `host` (reproduciendo), `stopped` (la posición queda
  quieta donde la deja el host) y `freeRunning` (sin host o sin posición: cuenta desde 0, o desde donde iba el host, al
  último tempo conocido o 120). Saltos = reproduciendo y la posición difiere de la esperada en más de 1/64 de negra
  (la automatización de tempo se desvía milésimas; un loop, compases). Arrancar no es un salto (`startedThisBlock`).
  Tempo inválido se ignora y se limita a 20–999 BPM; compás inválido (denominador no potencia de 2) se ignora.
  `musicalPosition` usa el inicio de compás del host si es coherente (respeta cambios de compás) y si no asume el
  mismo compás desde el inicio; margen de 1e-6 negras para el redondeo del host.
- `dsp/Engine.h`: passthrough con fundido de bypass de 20 ms (`EffectSwitch`); la salida es la entrada bit a bit.
- Librería (nuevo): `TripleBuffer.h` = buzón del último valor (3 copias + un byte atómico con índice y bit de "nuevo";
  escritor y lector nunca esperan). El procesador publica un `TransportSnapshot` por bloque; la GUI lo lee a 30 fps.
- Parámetros (versionHint 1): `bypass` (devuelto por `getBypassParameter()`).
- GUI 580 × 270: estado (▶ / ■ / ○ en ámbar), compás.tiempo.semicorchea en grande, luces por tiempo del compás, y
  FL (B:S) (compás:paso, como el contador Bar:Step de FL), Tempo, Compás, Posición en negras, Loop, Saltos, Audio (Hz
  y tamaño del bloque). BYPASS en la cabecera. Revisada con captura del Standalone (sin audio en este entorno).
- Tests: `DeepwaterDspTests` (+1: TripleBuffer en un hilo y con dos hilos, 2 millones de escrituras, nunca un valor
  mezclado ni hacia atrás) y `EbbTests` (posición musical en 4/4, 3/4, 6/8, 7/8, inicio de compás del host, redondeo y
  pre-roll; reloj libre continuo; sigue al host con bloques de 1–4096 y cambios de tempo sin saltos falsos, 4 sample
  rates; saltos de loop y adelante, arranque, parado, host que desaparece; tempo/compás inválidos; paso de FL; passthrough y
  bypass bit a bit en mono/estéreo).
- Release con 0 warnings; ctest 6/6; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: Fase 2 (modo tempo).
