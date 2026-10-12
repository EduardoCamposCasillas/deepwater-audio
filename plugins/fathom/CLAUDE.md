# CLAUDE.md — Fathom, EQ paramétrico con analizador

Plugin 4 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del EQ.

Nombre: **Fathom** ("sondear la profundidad": medir antes de tocar, como un EQ que se usa mirando el analizador).
Target `Fathom`, `PLUGIN_CODE Fth1`; fabricante `Undw`, el de toda la suite; `COMPANY_NAME "Deepwater"`. Decidido con
el usuario al empezar la Fase 1 (otras ideas que se consideraron: Isobath, Shoal, Reef).

## Objetivo del proyecto
Construir un EQ paramétrico al nivel de FabFilter Pro-Q 4 (pedido explícito del usuario: "como Pro-Q 4"), con
nombre, diseño y código propios: 24 bandas, todas las formas de filtro, pendientes de 6 a 96 dB/oct, edición directa
sobre el analizador, respuesta analógica sin "aplastamiento" cerca de Nyquist, estéreo por banda (L/R/Mid/Side), EQ
dinámico, modos de fase natural y lineal, dinámica espectral, igualación con una referencia, dibujo a mano de la
curva, color analógico y vista de las demás instancias. Se usa en FL Studio sobre Windows, en todo: voces, 808,
kicks, buses, master.

Pro-Q es la REFERENCIA DE NIVEL, no un plano: nada de copiar su interfaz al píxel ni replicar sus algoritmos
propietarios (su "Natural Phase", su dinámica espectral). Cada función se construye desde ideas publicadas (papers,
libros, blogs técnicos) y se explica antes de implementarla.

## Requisitos propios (además de las reglas del hilo de audio del raíz)
- Mover cualquier control en plena reproducción (frecuencia, ganancia, Q, forma, pendiente, encender una banda,
  cambiar el modo de fase) no causa clics ni zipper noise, ni con automatización rápida.
- Una banda en 0 dB o apagada deja la señal intacta (bit a bit cuando sea posible). Con todo plano, el EQ es
  transparente en todos los modos.
- La curva dibujada es la respuesta REAL del audio (calculada con los mismos coeficientes), no una aproximación.
- Estable con cualquier combinación de parámetros a 44.1, 48, 88.2 y 96 kHz (frecuencias cerca de Nyquist, Q extremos,
  cortes de 96 dB/oct apilados).
- Latencia: 0 en el modo normal; la de los modos de fase lineal/natural se informa al host y el bypass la mantiene.
- Es un efecto: mono y estéreo. El sidechain externo (EQ dinámico, igualación) es opcional.

## Librería DSP compartida
Lo que Fathom va a usar de `libs/dsp` (`deepwater::dsp`):
- `Svf.h`: base de los filtros de las bandas (TPT/ZDF: estable al modular).
- `SpectrumAnalyzer.h` + `AnalyzerTap.h` (vienen de Riptide): el analizador y la toma audio → GUI.
- `Smoothing.h` (`LinearRamp`, `EffectSwitch`), `Decibels.h`, `Pitch.h`, `LevelMeter.h`.
- `Fft.h`: fase lineal y dinámica espectral (Fases 10–11).
- `EnvelopeFollower.h`: detector del EQ dinámico (Fase 9).
- `Oversampler.h`, `FastMath.h`: el color analógico (Fase 13).
- `libs/presets` (`deepwater::presets`): presets, navegador y panel de guardar (Fase 15).

De Ebb no hizo falta mover nada al empezar: lo reutilizable (`TripleBuffer`, `BreakpointCurve`, `TransientDetector`)
ya está en la librería. Lo nuevo que sirva a otros plugins va a la librería con tests: los filtros de EQ (campana,
shelves, cortes de orden alto) y su respuesta en frecuencia le sirven al compresor (filtro del detector), al delay y a
la reverb (filtros del feedback); la convolución por FFT, a la reverb.

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target Fathom_VST3      # solo el plugin
build/plugins/fathom/Release/FathomTests.exe                    # tests del motor (sin JUCE)
```
- VST3: `build/plugins/fathom/Fathom_artefacts/Release/VST3/Fathom.vst3`.
- Standalone: `build/plugins/fathom/Fathom_artefacts/Release/Standalone/Fathom.exe`.

## Estructura
```
plugins/fathom/
  CMakeLists.txt           # plugin y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock y las tomas del analizador
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    dsp/Engine.h             # cadena de audio completa, sin JUCE: ganancia de salida y bypass
    gui/                     # tema, LookAndFeel, formato de valores, visor (EqDisplay: cuadrícula y analizador)
  tests/EngineTests.cpp    # tests del motor (sin JUCE)
```

## Aprendizaje de mezcla (igual de importante que el código)
Además de los 6 puntos del raíz (en efectos, siempre con el volumen igualado), este plugin se enfoca en:
- **El mapa de frecuencias**: sub (20–60 Hz), graves (60–250), medios-graves / "barro" (250–500), medios / "caja"
  (500 Hz–2 kHz), presencia (2–5 kHz), brillo (5–10 kHz) y aire (10–20 kHz); qué instrumento vive en cada zona y cómo
  se oye cada zona de más o de menos. Frecuencia ↔ nota (FL: A5 = 440 Hz).
- **Restar antes que sumar**: cortes angostos para quitar resonancias, subidas anchas y suaves para dar carácter.
- **Q y ancho de banda** (octavas), **pendientes** (por qué un corte de 96 dB/oct suena distinto de uno de 12).
- **Enmascaramiento**: dos instrumentos en la misma zona se tapan (kick y 808, voz y acordes); tallar espacio.
- **Fase**: qué cambia un EQ además del volumen, cuándo importa (graves de un kick, capas en paralelo, buses) y qué
  cuesta la fase lineal (pre-eco, latencia).
- **EQ dinámico vs. estático vs. compresor multibanda**; **Mid/Side** (ensanchar sin enturbiar el centro).
- **El sesgo del volumen**: más fuerte parece "mejor"; por eso se compara siempre con el volumen igualado.
- Contexto del usuario: reggaetón experimental. Ejercicios sobre 808 + kick del dembow, voces con autotune, percusión
  y buses; el master al final.

Actualizar el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/fathom/`) al final de cada fase. Los presets de
fábrica incluyen notas de para qué sirven y por qué.

## Hoja de ruta
- [x] Fase 1 — Proyecto base y lienzo: `plugins/fathom/` (VST3 + Standalone, efecto mono/estéreo) en el CMake raíz.
      Passthrough, ganancia de salida y bypass sin clics. El visor que será el "lienzo" del EQ: eje de frecuencias
      logarítmico (20 Hz–20 kHz), escala en dB, lectura de frecuencia + nota bajo el mouse y el analizador de entrada
      y salida (`SpectrumAnalyzer` de la librería). Tests y pluginval.
- [ ] Fase 2 — La campana: una banda peaking (frecuencia, ganancia, Q) construida desde el SVF; filtro de EQ en la
      librería con su respuesta en frecuencia analítica (para dibujar la curva real) y tests contra la teoría.
      Coeficientes suavizados sin clics. La curva dibujada sobre el analizador.
- [ ] Fase 3 — Todas las formas y 24 bandas: shelves grave/agudo, tilt shelf, flat tilt, notch, band-pass, all-pass,
      cortes grave/agudo de 6 a 96 dB/oct (Butterworth en cascada, con resonancia). Los parámetros de las 24 bandas
      (IDs definitivos), apagadas por defecto.
- [ ] Fase 4 — Edición sobre el visor: nodos arrastrables (doble clic crea una banda donde se clica, arrastrar mueve
      frecuencia/ganancia, rueda = Q, modificadores para ajuste fino y para un solo eje), panel de la banda elegida,
      selección múltiple, deshacer/rehacer y teclado de piano en el eje.
- [ ] Fase 5 — Analizador profesional: inclinación de 4.5 dB/oct, rango, resolución, velocidad, retención de picos,
      congelar; ver entrada/salida/sidechain; "agarrar" un pico del espectro para crear o mover una banda.
- [ ] Fase 6 — Escuchar y comparar: solo de banda (escuchar la zona que toca), auto gain (volumen igualado al
      comparar), escala de ganancia global, ganancia y paneo de salida, invertir la fase, comparación A/B.
- [ ] Fase 7 — Respuesta analógica: corregir el aplastamiento de campanas y shelves agudos cerca de Nyquist
      (bilineal "cramping") sin latencia. Opciones: sobremuestreo vs. diseño "matched" (Vicanek); medir contra el
      prototipo analógico.
- [ ] Fase 8 — Estéreo por banda: Estéreo / L / R / Mid / Side; analizador M/S.
- [ ] Fase 9 — EQ dinámico: por banda umbral, rango (corta o sube), ataque/liberación y umbral automático; detector
      con la forma de la banda o con el sidechain externo; la curva se mueve en vivo en el visor.
- [ ] Fase 10 — Modos de fase: fase lineal (convolución por FFT particionada, varias calidades, latencia informada)
      y fase "natural" propia (magnitud y fase del analógico); cambio de modo sin clics.
- [ ] Fase 11 — Dinámica espectral: una banda dinámica que actúa por bin de la FFT (solo donde hay exceso, no en toda
      la zona): domar resonancias que cambian con las notas.
- [ ] Fase 12 — Igualar y dibujar: igualar el espectro a una referencia (sidechain) creando bandas; dibujar la curva a
      mano y convertirla en bandas.
- [ ] Fase 13 — Color analógico: modos limpio / sutil / cálido (saturación suave con sobremuestreo, con lo aprendido
      en Riptide).
- [ ] Fase 14 — Otras instancias: ver el espectro de las demás instancias de Fathom del proyecto y marcar dónde chocan
      (enmascaramiento); editarlas desde aquí.
- [ ] Fase 15 — Presets con notas educativas (voces, 808 y kick del dembow, percusión, buses, master).
- [ ] Fase 16 — Pulido: CPU (24 bandas con SIMD, coeficientes), GUI escalable, pluginval estricto, pruebas de
      automatización extrema.

## Estado actual
Fase 1 COMPLETADA (2026-10-12). Probada por el usuario en FL Studio: funciona.
- `dsp/Engine.h`: ganancia de salida (±24 dB, `LinearRamp` de 20 ms: llega exacta, 0 dB = bit a bit) y fundido de
  bypass de 20 ms. Mono y estéreo; un tercer canal se deja intacto.
- Parámetros (`versionHintPhase1`): `bypass` (también el bypass del host) y `outputGain` (−24–+24 dB, 0).
- Procesador: tomas `AnalyzerTap<16384>` de entrada (antes del motor) y salida; solo se alimentan con el editor
  abierto (`setAnalyzerActive`). Latencia 0.
- GUI 960 × 560: cabecera (BYPASS), visor `EqDisplay` y perilla SALIDA (bipolar: el arco sale de 0 dB). El visor:
  20 Hz–20 kHz logarítmico; dos escalas sobre las mismas 8 divisiones (derecha EQ ±6/12/24/36 dB con el botón,
  izquierda analizador 0 a −96 dBFS); analizador ENTRADA (línea) y SALIDA (relleno) como el de Riptide (sube casi al
  instante, cae a 40 dB/s, silencio tras 0.25 s sin audio, solo repinta si algo cambia); la curva = la respuesta total
  (hoy la ganancia de salida; punteada en el borde si se sale de la escala); lectura bajo el mouse (Hz, nota de FL con
  cents, dB de cada escala); franja de zonas del mapa de frecuencias con tooltip. Revisada con captura del Standalone.
- Tests `FathomTests`: passthrough bit a bit (con y sin bypass, 4 sample rates, bloques variables), rampa de salida
  (exacta a los 20 ms, sin saltos, límite de ±24 dB), bypass sonando con +12 dB sin clic y bit a bit al terminar,
  mono y canales de más.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: Fase 2 (la campana).
