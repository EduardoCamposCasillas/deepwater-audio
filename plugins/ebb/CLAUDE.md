# CLAUDE.md — Ebb, ducker / modulador de volumen para sidechain

Plugin 3 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del ducker.

Nombre de trabajo: **Ebb** (la marea que baja: el volumen "se retira" y vuelve). Fabricante `Undw`, el de toda la
suite; `COMPANY_NAME "Deepwater"`. El nombre definitivo, el target y el `PLUGIN_CODE` (único en la suite; propuesta:
`Ebb1`) se deciden al empezar la Fase 1.

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
- [ ] Fase 1 — Proyecto base: `plugins/ebb/` con su `CMakeLists.txt` (VST3 + Standalone, efecto mono/estéreo),
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
Pendiente: solo planificado (2026-10-11). Todavía no hay código; la Fase 1 empieza cuando se decida arrancar el plugin.
