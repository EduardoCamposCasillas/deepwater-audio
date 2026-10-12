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
    dsp/TempoCurve.h         # modo tempo: divisiones, posición en el ciclo y la curva estirada a la duración (funciones puras)
    dsp/CurveShapes.h        # formas de fábrica (curvas por puntos) y cuál coincide con una curva dada
    dsp/Triggers.h           # modos de disparo (tempo, MIDI), el disparo dentro del bloque, filtro y nombres de nota (FL)
    dsp/Engine.h             # cadena de audio completa, sin JUCE: curva, rampas, glide, suavizado y bypass
    gui/                     # tema, LookAndFeel (perillas), visor del transporte, vista de la curva y medidor,
                             # editor de la curva (CurveEditor.h), luz de actividad MIDI (MidiActivityView.h)
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
- [x] Fase 2 — Modo tempo: curva de ducking sincronizada (1/2, 1/4, 1/8, 1/16, 1 compás y tresillos), controles de
      profundidad, duración y suavizado; sin clics en ningún cambio de parámetro.
- [x] Fase 3 — Curvas: presets de forma (como las de Kickstart) y editor de curva propio con puntos y tensión.
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
Fase 4 IMPLEMENTADA (2026-10-12), pendiente de probar en FL Studio.
- `dsp/Triggers.h` (nuevo): `TriggerMode` (tempo, MIDI; la Fase 5 añade audio al final), `Trigger` (muestra dentro del
  bloque + velocidad 0–1), filtro de nota (0 = todas, 1–128 = nota 0–127) y `flNoteName` (60 = C5, como FL).
- `dsp/Engine.h`: `process` recibe un `std::span<const Trigger>` ordenado (en modo tempo se ignora). Cada disparo
  reinicia la curva en su muestra; desde ahí cuenta negras con `clock.getBeatsPerSample()` (también con el host parado
  o sin host: notas tocadas en vivo disparan al último tempo). En MIDI la DIVISIÓN es el LARGO de la curva
  (largo × DURACIÓN). Pasado el largo el golpe queda "terminado" en el y del último punto: alargar la división después
  no la revive. Antes de la primera nota, al entrar en modo MIDI y al parar el host (play → stop) no hay golpe (sin
  ducking). Velocidad opcional: forma × velocidad. El estado que decide la curva (`CurveState`: modo, período, host
  corriendo, velocidad, hay golpe) se compara entre bloques y cualquier cambio hace el glide de 30 ms; arrancar el host
  no. Sin glide, cambiar el largo 1/4 → 1/16 salta 0.74, tempo → MIDI 0.74, velocidad on → off 0.44 (comprobado).
  Un redisparo solo pasa por el suavizado (baja desde la ganancia actual), como un golpe del modo tempo.
- Parámetros nuevos (versionHint 4): `mode` (elección Tempo/MIDI, Tempo: los proyectos anteriores suenan igual),
  `midiNote` (elección de 129: "Todas", "C0 (0)"… "G10 (127)"; Todas), `velocity` (bool, off).
- Procesador: `NEEDS_MIDI_INPUT TRUE`, `acceptsMidi() = true`. `collectTriggers` recorre el MidiBuffer (note-on con
  velocidad > 0), cuenta todas las notas (atómicos: recibidas, disparos, última nota) y, en modo MIDI y si pasan el
  filtro, las pasa como disparos a un `std::array<Trigger, 256>` reservado (si sobran, gana la última).
- GUI 960 × 640: fila de controles con MODO + NOTA MIDI, DIVISIÓN/LARGO (la etiqueta y el tooltip cambian con el modo)
  + FORMA, columna ACTIVIDAD MIDI (luz: acento = disparó, ámbar = llegó sin disparar; última nota; "USAR ÚLTIMA NOTA"
  = MIDI learn de un clic; VELOCIDAD) y las 3 perillas. NOTA y VELOCIDAD se atenúan en modo tempo. La vista previa en
  modo MIDI dibuja un solo golpe desde el reposo en el espacio de dos ciclos. Capturas del Standalone (tempo y MIDI)
  revisadas.
- Tests `EbbTests` (+4): filtro y nombres de nota; golpes exactos a la muestra con un kick de dembow desplazado (pasos
  1, 7, 11, 15) a 4 sample rates, iguales con bloques de 1–7; modo tempo ignora las notas (idéntico); velocidad;
  redisparo con el paso limitado por el suavizado; cambios de largo/modo/velocidad sonando < 0.01; curva terminada no
  revive; notas con el host parado; parar suelta el golpe sin clic y queda en 1 exacto. Release con 0 warnings;
  ctest 6/6; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: que el usuario pruebe la Fase 4 en FL Studio; después, Fase 5 (modo audio sidechain).

### Fase 3 (completada 2026-10-12, probada en FL)
- Librería: `deepwater/dsp/BreakpointCurve.h` (con test en `DeepwaterDspTests`): curva x ∈ [0, 1] → y ∈ [0, 1] por
  puntos (máx. 32, tamaño fijo). Tensión por tramo en [−1, 1]: `expm1(k·t) / expm1(k)`, k = tensión · 8. Escalón
  vertical si dos puntos comparten x (gana el de la derecha). Edición que nunca desordena y `sanitise`.
- `dsp/CurveShapes.h`: 8 formas de fábrica. y = nivel de volumen; x = del golpe al final de la DURACIÓN; pasada la
  duración se queda en el y del último punto. "Clásica" (por defecto) = la smoothstep de la Fase 2 (error ≤ 0.006).
- `dsp/Engine.h`: `setCurve` deja la curva pendiente y se aplica con el glide de 30 ms (sin él, Lenta → Rápida salta 0.82).
- La curva NO es un parámetro: hijo `<Curve><Point x y tension/>` del estado; mensajes → audio por un
  `TripleBuffer<BreakpointCurve>`; `curveVersion` avisa a la GUI. Estado sin curva o con < 2 puntos → forma por
  defecto. El menú FORMA muestra la forma de fábrica idéntica o "Personalizada".
- GUI: `CurveEditor` (arrastrar, Mayús = cuadrícula 1/8 × 1/4, doble clic añade/quita, clic derecho quita, asa de
  tensión; doble clic = recta). La interacción con el ratón no se puede automatizar (JUCE ignora PostMessage).

### Fase 2 (completada 2026-10-11, probada en FL)
- `dsp/TempoCurve.h`: 9 divisiones (1 bar, 1/2, 1/4, 1/8, 1/16 y sus tresillos; "1 bar" = el compás del host). La
  cuadrícula arranca en cada inicio de compás (`HostClock::barOrigin`: el del host si es coherente, si no 0), así una
  división que no cabe justa (1/2 T en 3/4) se reinicia en el "1". Margen de 1e-9 negras para el redondeo del host.
  Forma fija (hasta la Fase 3): smoothstep, `1 − x²(3 − 2x)` con x = tiempo desde el golpe / (duración · ciclo).
- `dsp/Engine.h`: ganancia = 1 − profundidad · forma, calculada en cada muestra desde `clock.ppqAt (i)` (exacta a la
  muestra con cualquier bloque). Profundidad con rampa de 20 ms, duración de 50 ms. Cambio de división/compás o parada
  del host: "glide" (diferencia curva vieja − nueva en la 1.ª muestra del bloque, desvanecida en 30 ms). Arrancar NO
  hace glide (el primer golpe suena entero). Parado = sin ducking; reloj libre (Standalone) = ducking a 120 BPM.
  Suavizado final: 1 polo sobre la ganancia, `coef = 1 − exp(−3 / (ms · fs))` (≈95 % en esos ms; 0 = sin suavizar);
  copia el objetivo a < 1e-6, así profundidad 0 es bit a bit. Los golpes y los saltos del host solo pasan por el
  suavizado (un redisparo baja desde la ganancia actual).
- Parámetros nuevos (versionHint 2): `division` (elección, 1/4), `depth` (0–100 %, 70; texto con dB), `duration`
  (5–100 %, 70), `smoothing` (0–20 ms con sesgo, 3 ms al centro; 2). `versionHint` → `versionHintPhase1`/`Phase2`.
- Medidor: el procesador acumula la ganancia mínima en un `std::atomic<float>` y la GUI la toma con `exchange (1)`.
- GUI 640 × 530: transporte (170 px), `CurveView` (dos ciclos con los ajustes y el suavizado simulado a escala real,
  ms del ciclo y de la vuelta, medidor de reducción 0–24 dB que vuelve a 30 dB/s) y fila de DIVISIÓN + 3 perillas
  (estilo Riptide) con tooltips. Revisada con captura del Standalone.
- Tests `EbbTests` (+5): curva pura; golpes en la muestra exacta a 4 sample rates, iguales con bloques de 1–7 y de
  4096, tresillos y "1 bar" en 7/8; cambios de división/duración/profundidad sonando sin saltos > 0.01 (sin glide el
  de división salta 0.28–0.61: comprobado quitándolo); loop a mitad de curva; parar sin clic y quedar en 1 exacto;
  reloj libre; profundidad 0 y bypass bit a bit.
- Release con 0 warnings; ctest 6/6; pluginval strictness 10 ampliado: SUCCESS.
