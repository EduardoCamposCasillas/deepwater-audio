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
- `EqFilter.h` (nació en la Fase 2, completo en la Fase 3): `EqCoefficients` (mezcla de la entrada con las salidas
  LP/BP/HP del SVF: campana, shelves, tilt shelf, notch, pasa banda, all-pass, LP/HP de 1 y 2 polos) con
  `response()`/`magnitudeDb()` exactas para esos coeficientes; `EqCascade` (hasta 8 etapas en serie: Butterworth de
  1–16 polos con resonancia) y `FlatTiltDesign` (pendiente constante en dB/oct); `EqFilter` y `EqCascadeFilter`.
- `MatchedEq.h` (nació en la Fase 7): las mismas formas con la magnitud del prototipo analógico cerca de Nyquist
  (`MatchedEq`, misma interfaz que `EqCoefficients`/`EqCascade`), los prototipos `AnalogBiquad`/`AnalogCascade` y
  `EqCascade::blend` (pasar de un diseño al otro sin clic).
- `SpectrumAnalyzer.h` + `AnalyzerTap.h` (vienen de Riptide): el analizador y la toma audio → GUI. En la Fase 5
  ganó `peak()` (el pico de una zona afinado con una parábola por los bins: < 0.05 bin).
- `AnalyzerTap.h` ganó `pushSide` en la Fase 8 (escribe el side, (L − R) / 2, para el analizador M/S).
- `Smoothing.h` (`LinearRamp`, `EffectSwitch`), `Decibels.h`, `Pitch.h`, `LevelMeter.h`.
- `RealFft.h` y `Convolver.h` (nacieron en la Fase 10): FFT real para el audio y convolución por FFT particionada con
  fundido entre kernels (los modos de fase; la reverb los va a usar). `Fft.h` (la clara, fuera del audio): dinámica
  espectral (Fase 11).
- `EnvelopeFollower.h`: se probó para el detector del EQ dinámico (Fase 9) y se descartó: suaviza el nivel en lineal y,
  con el umbral muy por debajo de la señal, el ataque efectivo se volvía casi instantáneo. El detector de la Fase 9
  (RMS corto + ataque/liberación sobre la ganancia en dB) vive en `Source/dsp/Dynamics.h`; si el compresor (plugin 5)
  lo necesita, se mueve a la librería entonces.
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
    PluginProcessor.h/.cpp   # parámetros, estado (con la vista del analizador y el otro lado del A/B), processBlock,
                             # el bus de sidechain, las tomas del analizador (mid y side), el historial de deshacer
                             # y el solo
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    dsp/Engine.h             # cadena de audio completa, sin JUCE: 24 bandas en serie (con su dinámica), modos de fase,
                             # escala de ganancia, salida, auto gain, balance, polaridad, solo y bypass; la memoria de
                             # los modos de fase a pedido (providePhaseMemory)
    dsp/PhaseDesign.h        # modos de fase: tamaños y latencias, el pedido de diseño y PhaseDesigner (FIR de fase
                             # lineal y de corrección natural, con caché por banda)
    dsp/PhaseStage.h         # los modos de fase en el audio: convolucionadores, kernels entre hilos (KernelStore), hilo
                             # de diseño, transiciones sin clic, dry retrasado y detectores alineados
    dsp/Dynamics.h           # EQ dinámico: el detector de una banda (zona del solo, RMS corto, rodilla, rango,
                             # ataque/liberación sobre la ganancia, umbral automático)
    dsp/AutoGain.h           # cuánto cambia la sonoridad la curva (ruido rosa con peso K de BS.1770), por banda;
                             # con bandas de un solo canal, matrices 2×2 sobre una mezcla típica (side −6 dB)
    dsp/Solo.h               # la zona que se escucha en solo según la forma (y el canal) de la banda
    dsp/Band.h               # una banda: formas (Shape), pendientes, canal (Placement: estéreo/L/R/mid/side), ajustes
                             # (BandSettings), parámetros suavizados, fundido de encendido/cambio de forma o de canal,
                             # respuesta analógica o bilineal (con su rampa), ganancia dinámica (con los coeficientes
                             # interpolados) y coeficientes para la GUI
    edit/UndoStack.h         # pila de deshacer/rehacer (sin JUCE): pasos con solo los parámetros que cambiaron
    edit/EditHistory.h       # deshacer con JUCE: escucha los gestos de los parámetros (no la automatización del host);
                             # fotos y carga de todos los parámetros para el A/B
    edit/NodeEditing.h       # cuentas de la edición sin JUCE: arrastrar nodos, rueda = Q, banda libre, notas
    analysis/SpectrumView.h  # cuentas del analizador sin JUCE: inclinación, rango, resolución, balística, retención
                             # de picos, canales (mid/side), ajustes de la vista (ViewSettings) y búsqueda de picos
    gui/                     # tema (con el color de cada banda), LookAndFeel, formato de valores, atajos (Keys),
                             # visor (EqDisplay: cuadrícula, analizador, curvas, nodos editables, teclado de piano,
                             # agarrar picos, curvas por canal), BandEdits (cambiar parámetros con gestos), panel de
                             # la banda elegida (BandPanel, con SOLO, CANAL y la fila de la dinámica), ajustes del
                             # analizador (AnalyzerPanel), columna de salida
                             # (OutputPanel: escala, auto gain, balance, polaridad, ganancia, fase y calidad)
  tests/EngineTests.cpp    # tests del motor (con auto gain, solo, estéreo por banda, EQ dinámico y modos de fase), de la
                           # edición y del analizador (sin JUCE)
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
- [x] Fase 2 — La campana: una banda peaking (frecuencia, ganancia, Q) construida desde el SVF; filtro de EQ en la
      librería con su respuesta en frecuencia analítica (para dibujar la curva real) y tests contra la teoría.
      Coeficientes suavizados sin clics. La curva dibujada sobre el analizador.
- [x] Fase 3 — Todas las formas y 24 bandas: shelves grave/agudo, tilt shelf, flat tilt, notch, band-pass, all-pass,
      cortes grave/agudo de 6 a 96 dB/oct (Butterworth en cascada, con resonancia). Los parámetros de las 24 bandas
      (IDs definitivos), apagadas por defecto.
- [x] Fase 4 — Edición sobre el visor: nodos arrastrables (doble clic crea una banda donde se clica, arrastrar mueve
      frecuencia/ganancia, rueda = Q, modificadores para ajuste fino y para un solo eje), panel de la banda elegida,
      selección múltiple, deshacer/rehacer y teclado de piano en el eje.
- [x] Fase 5 — Analizador profesional: inclinación de 4.5 dB/oct, rango, resolución, velocidad, retención de picos,
      congelar; ver entrada/salida/sidechain; "agarrar" un pico del espectro para crear o mover una banda.
- [x] Fase 6 — Escuchar y comparar: solo de banda (escuchar la zona que toca), auto gain (volumen igualado al
      comparar), escala de ganancia global, ganancia y paneo de salida, invertir la fase, comparación A/B.
- [x] Fase 7 — Respuesta analógica: corregir el aplastamiento de campanas y shelves agudos cerca de Nyquist
      (bilineal "cramping") sin latencia. Opciones: sobremuestreo vs. diseño "matched" (Vicanek); medir contra el
      prototipo analógico.
- [x] Fase 8 — Estéreo por banda: Estéreo / L / R / Mid / Side; analizador M/S.
- [x] Fase 9 — EQ dinámico: por banda umbral, rango (corta o sube), ataque/liberación y umbral automático; detector
      con la forma de la banda o con el sidechain externo; la curva se mueve en vivo en el visor.
- [x] Fase 10 — Modos de fase: fase lineal (convolución por FFT particionada, varias calidades, latencia informada)
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
Fase 10 COMPLETADA (2026-10-25). Probada por el usuario en FL Studio: funciona.
- Tres modos (`dsp/PhaseDesign.h`, `PhaseMode`): cero latencia (lo de siempre), natural y lineal. Parámetros nuevos
  (versionHint 10): `phaseMode` (Zero Latency / Natural Phase / Linear Phase, cero latencia por defecto: un proyecto de
  las Fases 1-9 suena igual) y `linearQuality` (Low / Medium / High / Very High / Maximum, Medium por defecto). Entran al
  deshacer, al A/B y a la automatización.
- Fase lineal: FIR de `2048 << calidad` muestras a 44.1/48 kHz (×2 a 88.2/96, ×4 sobre 100 kHz) por muestreo en
  frecuencia: |H| de cada banda (los mismos coeficientes que la curva, `Band::coefficientsFor`) en una grilla de 2N
  bins, producto, FFT inversa, centrado en N/2 y ventana de Hann (se probó Hann/Blackman/Kaiser 6-12 en numpy: Hann
  da el menor error en graves y el piso de un corte de 96 dB/oct queda bajo −180 dB igual). Latencia N/2 + 512
  (bloque de la convolución): 1536 / 2560 / 4608 / 8704 / 16896 muestras (35 / 58 / 104 / 197 / 383 ms a 44.1). Con
  bandas L/R, dos FIR; con mid/side, la matriz 2×2 por bin y cuatro FIR (L→L, R→L, L→R, R→R).
- Natural: las bandas IIR (siempre matched: `Engine::scaled` y `getBandSettings` fuerzan `analog`) + un FIR de
  corrección C = analógico / IIR (`Band::analogPrototype`, nuevo; el flat tilt no se corrige). Donde el IIR baja de
  −40 dB, C → 1 (peso |H|² / (|H|² + 0.01²)); |C| limitado a ±12 dB. 256 muestras a 44.1/48 (se probaron 1024 y 512:
  mismo error), partición 32: latencia 160 muestras (3.6 ms). Error contra el prototipo analógico (campana 12 kHz,
  shelf 5 kHz, corte 18 kHz; campana Q 8 en 15 kHz con corte de 48 dB/oct, shelf grave y all-pass; campana Q 20 en
  18 kHz + notch + shelf 19 kHz): ≤ 0.07 dB y 0.4° a las 4 frecuencias de muestreo (cero latencia: hasta 1.9 dB y 179°).
- Librería (nuevo): `RealFft.h` (FFT real de N puntos con media FFT compleja, tablas en `prepare`, sin reservar al
  transformar; float para el audio, double para diseñar) y `Convolver.h` (`ConvolutionKernel` con caminos
  entrada → salida y trozos vacíos salteados, `PartitionedConvolver` uniformemente particionado overlap-save, hasta 4
  entradas y 4 salidas, latencia P, fundido lineal entre kernels en un bloque, `getAudibleKernel` = el kernel de lo que
  está saliendo). Tests en `DeepwaterDspTests`.
- Motor (`dsp/PhaseStage.h`): tres convolucionadores (natural y dos de fase lineal: cambiar de calidad es cambiar de
  uno al otro), cada uno con 3 lugares de kernel (`KernelStore`: estados atómicos libre/escribiéndose/listo/en uso,
  sin locks). Los diseña un hilo propio (`std::thread`, duerme 1 ms sin trabajo; el audio pide a lo sumo un diseño a la
  vez por convolucionador por `TripleBuffer` y solo si cambió algo: la dinámica, desde 0.05 dB); offline (o en los
  tests) se diseña en el audio y el render es determinista. El diseñador cachea |H| (o C) de cada banda por bin: mover
  un nodo recalcula una banda. Un kernel nuevo se funde con el viejo en un bloque; con todo plano el kernel es un
  impulso y, cuando es lo que SE OYE (no solo lo calculado: `getAudibleKernel`), la salida es un retardo exacto (bit a
  bit). Primero se cambiaba con el kernel calculado: la automatización extrema en lineal daba 0.09 de segunda diferencia
  (cota 0.024); con lo audible, 0.007.
- Transición de modo: el modo nuevo arranca sin oírse (su convolucionador se llena: N + P muestras; si las bandas IIR
  estaban quietas por la fase lineal, se reinician y se esperan 50 ms) y después fundido de 20 ms con smoothstep (el
  fundido recto dejaba un quiebre de pendiente: 0.0014 contra 0.00085 de cota; con la curva en S, 0.00052). El dry del
  bypass, el solo y la mezcla salen de una línea de retardo con la latencia de cada modo (se funde igual). Latencia
  informada = la del modo pedido.
- EQ dinámico en lineal: los detectores escuchan la entrada (y el sidechain) retrasados N/2 (alineados con el centro
  del FIR) y su ganancia entra al FIR siguiente. No escuchan lo que dejaron las bandas anteriores (en cero latencia sí).
- Memoria: 15-35 MB por instancia con todo (designer + kernels + FDL). En tiempo real se reserva recién al pedir natural
  o lineal: la etapa sin memoria se queda en cero latencia y avisa; el temporizador del procesador (10 Hz, hilo de
  mensajes) llama a `Engine::providePhaseMemory`, que prepara una etapa nueva y se la ofrece al audio (estado atómico
  libre/ofrecida/devuelta); el audio la cambia si la suya está quieta en cero latencia y la vieja se destruye en el hilo
  de mensajes. Offline (y los tests) siempre con memoria en `prepare`. Una vez reservada, se queda.
- Procesador: `setRealtime (! isNonRealtime())` en cada bloque; `setLatencySamples` en `prepareToPlay` y desde el
  temporizador cuando cambia (no desde el audio); `getTailLengthSeconds` = el largo del FIR.
- GUI: FASE / CALIDAD / latencia al final de la columna de SALIDA (CALIDAD atenuada fuera de lineal); ANALÓGICA atenuado
  en natural; en el visor, con fase lineal o natural la curva total (y la lectura de arriba) es la del FIR real (la GUI
  diseña el mismo con su `PhaseDesigner`, como mucho cada 0.12 s; lineal = la respuesta del FIR, natural = curva de las
  bandas + |C|); con curvas por canal se quedan las de las bandas. Aviso "FASE LINEAL" / "FASE NATURAL". Cabecera
  "Fase 10: modos de fase".
- CPU medida (5 bandas, estéreo, bloques de 256, 44.1 kHz): cero latencia 0.34 % de un núcleo, natural 0.88 %, lineal
  0.74 / 0.81 / 0.99 / 1.31 / 2.14 %; a 96 kHz 1.24, 2.38 y 1.44-5.10 %. OJO: la convolución calcula un bloque cada P
  muestras: con bloques del host chicos, un callback de cada 512/64 hace todo el trabajo (pico); repartirlo es Fase 16.
- Tests nuevos `FathomTests`: latencias; plano y bypass = la entrada retrasada bit a bit (natural y lineal); lineal
  simétrico (< 1e-6) y su magnitud = la curva (< 0.1 dB, fase < 0.5°); natural contra el analógico en 3 casos; cambiar
  de modo y de calidad sonando sin clic; lineal por canal (mid no toca el side, side no toca el centro, izquierda no
  toca la derecha, < 1e-4; nivel = cero latencia con la parte que le toca); dinámica en lineal (−11.97 contra −12.00 dB;
  lo suave intacto); hilo de diseño (−9.00 dB con la banda puesta en marcha); pre-eco (campana −12 dB Q 8 en 100 Hz:
  −300 / −74 / −38 dB de energía antes del golpe); resolución por calidad (+9 dB Q 4 en 40 Hz: 2.93 / 4.72 / 6.62 /
  7.96 / 8.66 dB); automatización extrema en natural y lineal sin clic; mono (side = retardo exacto); solo en lineal;
  memoria a pedido. `DeepwaterDspTests`: `RealFft` contra `fft()` de 4 a 8192 puntos (float y double), convolución
  particionada = directa (< 1e-5, 3 caminos, bloques al azar), trozos vacíos salteados, fundido entre kernels.
- GUI no revisada con capturas (esta vez el permiso para lanzar el script de captura fue denegado): la revisó el usuario en FL.
- Release con 0 warnings; FathomTests y DeepwaterDspTests: todos pasan; ctest 7/8: UndertowTests falló en su test de
  CPU (20.7 % y 20.9 % contra 20 %; ni Undertow ni lo que usa de `libs/` cambió: en la librería solo hay headers
  nuevos); pluginval strictness 10 ampliado: SUCCESS (antes y después de la memoria a pedido).

### Lo que quedó de la Fase 9
Probada por el usuario en FL Studio: funciona.
- Cada banda con ganancia (campana, shelves, tilts) puede ser dinámica: `DynamicSettings` en `BandSettings::dynamics`
  (encendido, rango, umbral, ataque, liberación, umbral automático, detector externo) y `canBeDynamic(shape)`.
- Detector (`dsp/Dynamics.h`, `BandDynamics`): la zona de `soloZoneFor` (con el canal de la banda; los tilts escuchan su
  canal entero) sobre lo que llega a la banda o sobre el sidechain; nivel RMS (1 polo sobre x², en double) con ventana
  de 2 ciclos de lo más grave de la zona (1-20 ms; campana 2/(f·min(Q, 1)), shelf de agudos 2/f, el resto 20 ms) + 3 dB
  (un seno lee su pico); en estéreo, el lado más fuerte (los dos canales se tocan igual). Curva: 1 dB por dB sobre el
  umbral hasta el rango, rodilla suave de 6 dB (`gainChangeDb`). Ataque/liberación = 1 polo sobre la GANANCIA en dB
  (ataque si tiene que actuar más; a 0.001 dB del objetivo se copia: vuelve bit a bit). Umbral automático = promedio en
  dB del nivel en 1.5 s (sin contar lo que está bajo −70 dB) + 3 dB. Todo eso cada 16 muestras (`Band::dynamicInterval`);
  rango, umbral, AUTO y encendido con rampas de 20 ms a ese ritmo.
- Primera versión con el `EnvelopeFollower` de la librería (pico con ataque en lineal): con el umbral 34 dB debajo, una
  banda con 10 ms de ataque cortaba 12 dB en < 1 ms (el test de clics lo marcó). Reemplazado por lo de arriba.
- `Band::setDynamicGain(db)`: rampa de 16 muestras; si nada más se mueve, se calculan los coeficientes del destino y se
  interpolan con `EqCascade::blend` (si otra rampa se mueve, se recalcula todo en cada muestra como siempre). La
  ganancia + la dinámica se limita a ±30 dB. `Band::coefficientsFor(settings, sr, dynamicDb)` para la curva en vivo.
- Motor: `processDynamic` alimenta el detector con la muestra que entra a la banda (o la del sidechain: uno mono va a los
  dos lados; con la pista mono, el promedio) antes de procesarla. `Engine::process(..., sidechain, sidechainChannels)`;
  `getDynamics(b)`. La escala de ganancia también escala el rango. Auto gain y solo usan la ganancia propia (no la
  dinámica). Una banda que se apaga del todo suelta su dinámica (`stop`).
- Parámetros nuevos por banda (versionHint 9, al final del layout): `band<N>Dynamic` (off), `band<N>DynRange` (−30 a
  +30, −6), `band<N>Threshold` (−60 a +6, −20), `band<N>Attack` (0.5-300 ms log, 10), `band<N>Release` (5-3000 ms log,
  150), `band<N>AutoThreshold` (off), `band<N>DynSource` (Band/Sidechain). Un proyecto de las Fases 1-8 suena igual.
  Entran al deshacer, al A/B y a la automatización.
- Procesador: el sidechain se lee siempre (antes, solo con el editor abierto, para el analizador) y va al motor;
  `getDynamicState(b)` (ganancia en vivo, nivel, umbral usado) en atómicos para la GUI.
- GUI: el panel de la banda creció a 186 px (ventana 1082 × 714) con una fila nueva: DINÁMICO, medidor (nivel de la zona
  de −60 a +6, raya del umbral, ganancia ahora), DETECTOR, AUTO (bajo "UMBRAL"), RANGO, UMBRAL, ATAQUE, LIBERACIÓN;
  atenuados según la forma, el encendido y AUTO. Visor: la curva y el relleno de la banda son los de AHORA (se mueven en
  vivo), la curva en reposo punteada mientras se mueve, línea vertical del nodo hasta el rango con una raya y un punto
  blanco donde está; "din ... (ahora ...)" en la etiqueta; "EQ dinámico" en el menú del nodo; aviso "DINÁMICA CON
  SIDECHAIN: SIN CONECTAR". La cabecera dice "Fase 9: EQ dinámico" (decía Fase 6).
- Tests nuevos `FathomTests`: curva de ganancia (rodilla, rango, continuidad); zona fuerte = la curva con la ganancia
  dinámica (< 0.01 dB), zona suave y fuera de la zona = bit a bit; nivel de un seno = su pico, 6 dB sobre el umbral = 6
  dB (corte y subida); ataque y liberación = ln 2·τ para la mitad del cambio con el umbral 34 dB abajo; umbral automático
  (sostenido no se toca, +12 dB de golpe = todo el rango, si se queda pasa a ser lo normal); sidechain (y sin él, bit a
  bit); side no ve el centro, escala del rango, auto gain sin cambios; apagarla sin clic y después bit a bit; todo al
  azar cada bloque con un sidechain a golpes sin clic (cota a 1/4 de la de antes: sin interpolar los coeficientes sale
  entre 0.3 y 0.9 de la cota, con la interpolación < 0.08).
- GUI revisada con una captura del Standalone (`PrintWindow`: `CopyFromScreen` falla en esta sesión) solo en reposo: los
  clics simulados no llegan a la ventana. Lo interactivo (medidor, curva en vivo, línea del rango) lo revisó el usuario en FL.
- Release con 0 warnings; FathomTests: todos pasan; ctest 7/8: UndertowTests falló en su test de CPU (21.0 % y 22.1 %
  contra 20 %, con Docker y el navegador abiertos; ni Undertow ni `libs/` cambiaron en esta fase); pluginval strictness
  10 ampliado: SUCCESS.

### Lo que quedó de la Fase 8
Probada por el usuario en FL Studio: funciona.
- Cada banda actúa en estéreo, L, R, mid o side (`Placement` en `Band.h`, `BandSettings::placement`). Mid/side sin ida
  y vuelta: se filtra M = (L + R)/2 (o S = (L − R)/2) y a L y R se les suma solo el cambio d = filtrada − M (side: L + d,
  R − d): con la banda en 0 dB, d = 0 y la salida es bit a bit. L y R usan el filtro de su canal; mid y side, el del
  canal 0. En mono: side no hace nada, el resto procesa el canal. Cambiar de canal = el mismo fundido que cambiar de
  forma (`needsSwitch` compara el canal; `startShape` lo cambia en silencio con el filtro en limpio).
- Motor: `Band::processStereo/processMono` en el bucle de bandas (por tramos de 64, igual que antes). Solo:
  `Band::zoneStereo/zoneMono` = lo que deja pasar el filtro SOLO en su canal (izquierda = la zona en L y R en silencio;
  mid = la zona en los dos; side = L = z, R = −z); `soloZoneFor` copia el canal y el motor lo trata como otra clase de
  filtro (fundido a la mezcla y vuelta).
- Auto gain: si alguna banda encendida no es estéreo, en cada punto la respuesta es una matriz 2×2 sobre (L, R)
  (estéreo h·I, izquierda diag(h, 1), mid I + (h − 1)·P_M, side I + (h − 1)·P_S), producto en orden de las bandas, y la
  potencia es traza(T·C·Tᴴ)/traza(C) con C = mid de potencia 1 y side 0.25 (−6 dB) sin correlación. Todo estéreo = la
  cuenta de antes (|Π h|²). Mono (`setMono`, el motor lo pone en cada bloque): producto de las bandas que no son side.
  Cada banda guarda su H compleja por punto. Valores (campana +6 dB, 2 kHz, Q 0.5): estéreo −2.80, mid −2.37,
  izquierda −1.62, side −0.72; mid + side iguales = estéreo; izquierda + derecha = estéreo.
- Parámetro nuevo por banda (versionHint 8): `band<N>Stereo` (lista Stereo/Left/Right/Mid/Side, Stereo por defecto:
  un proyecto de las Fases 1-7 suena igual). Los 24 van al final del layout. Entran al deshacer, al A/B y a la
  automatización.
- Analizador M/S: tres tomas más en el procesador (side de entrada, salida y sidechain, con `AnalyzerTap::pushSide`;
  en mono no se escriben). `ViewSettings::channelsIndex` (Mid (L+R) / Side (L−R) / Mid + Side), guardado como
  "channels" en `AnalyzerView`. Side = las tres trazas leen las tomas del side; Mid + Side = las de siempre y una traza
  más (`outputSide`, línea violeta `theme::spectrumSide`) que solo se analiza en esa vista. Cambiar de vista reinicia
  las trazas y los picos retenidos.
- GUI: CANAL en el panel de la banda (columna nueva entre PENDIENTE y las perillas, que pasaron de 108 a 100 px) y en
  la descripción; "Canal" en el menú del nodo (actúa sobre las elegidas); letra L/R/M/S arriba a la derecha del nodo y
  en la etiqueta. Curvas: si hay bandas L/R se dibujan las curvas L (celeste) y R (rosada); si hay M/S, M (blanca) y S
  (violeta); cada una = bandas estéreo + las de ese canal (exacta con una sola clase; con L/R y M/S mezcladas es una
  guía). La lectura de arriba da el valor de cada curva. Aviso "ANALIZADOR: SIDE" / "MID + SIDE (VIOLETA)". CANALES en
  la columna del analizador, arriba de INCLINACIÓN.
- Tests nuevos `FathomTests`: L/R tocan solo su canal (el otro bit a bit); mid/side cambian solo su parte (la otra
  < 1e-6) con la ganancia de la curva; 0 dB en mid/side = bit a bit; una side no toca un sonido al centro y una mid uno
  en contrafase (bit a bit); mono (side = bit a bit, el resto = estéreo); canal al azar en 3 bandas cada bloque sin clic;
  auto gain (orden estéreo < mid < izquierda < side < 0, mid + side = estéreo, L + R = estéreo, mono); solo side (centro
  = 0 exacto, nivel = zona) y solo izquierda (derecha = 0 exacto). `DeepwaterDspTests`: `pushSide`.
- Sin cambios: latencia 0, los IDs de antes, el sonido con todas las bandas en estéreo.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS. La GUI no se revisó con capturas del
  Standalone (la captura de pantalla falló); el usuario la revisó en FL.

### Lo que quedó de la Fase 7
Probada por el usuario en FL Studio: funciona.
- Opciones consideradas: sobremuestrear (×4 de CPU en 24 bandas y latencia o fase de los filtros del sobremuestreo) vs.
  diseño "matched" (M. Vicanek, "Matched Second Order Digital Filters", 2016) a la frecuencia de muestreo original. Se
  eligió el matched: latencia 0, mismo costo por muestra quieto. También se probó un ajuste libre de polos y ceros en 5
  puntos: frágil (denominadores no realizables) e impreciso en graves; descartado.
- Librería: `MatchedEq.h` (nuevo) con la misma interfaz que las formas de `EqCoefficients` y los cortes de `EqCascade`.
  Polos por invarianza al impulso (`Poles::second/first`, con 1 − r, a(1), a(−1) y 1 − a2 por expm1: precisos con polos
  pegados a z = 1); |B|² = B0·φ0 + B1·φ1 + B2·φ2 y `numerator` (la raíz de fase mínima; si no hay numerador real, B2 se
  limita a −W²); `fromBiquad` escribe cualquier biquad estable sobre el SVF (g² = a(1)/a(−1), k·g = 2(1 − a2)/a(−1),
  lp = H(DC) − dry, hp = H(Nyquist) − dry, bp de b0 − b2): la curva sigue saliendo de `EqCoefficients::response()`.
  Por forma: campana = DC, pico y derivada 0 en el pico (Vicanek), la que baja = inversa exacta de la que sube; shelves
  = la versión con los polos abajo (graves que sube, agudos que baja) con DC, la frecuencia y Nyquist, y la otra es su
  inversa; con Q alto los ceros también por invarianza al impulso (matched-Z), mezclados entre Q 1 y 2 y apagados si los
  ceros pasan de 0.35–0.45·fs (la mezcla de dos numeradores de fase mínima con los mismos polos es de fase mínima: las
  condiciones de Jury de 2º orden son lineales); tilt = shelf de agudos × 1/A; notch = ceros en el círculo y DC exacto;
  pasa banda = DC (cero), centro y Nyquist; all-pass = ceros espejo (|H| = 1 exacto); cortes de graves = cero doble en
  DC (o simple en 6 dB) e igual en el corte; etapas de los cortes de agudos = DC, x_c/4 y x_c (igualar Nyquist dejaba
  ±1 dB por etapa en la banda que pasa, que en 8 etapas suma +8 dB). 0 dB = los coeficientes de la bilineal (bit a bit).
  `AnalogBiquad`/`AnalogCascade`: los prototipos en Hz absolutos. `EqFilter.h` ganó `EqCoefficients::blend`,
  `EqCascade::blend` y `EqCascade::butterworthStageQ` (la cuenta de antes, sin cambios).
- Error máximo contra el analógico (`DeepwaterDspTests`, 4 sample rates, 20 Hz–20 kHz, Q 0.3–2, ±3 a ±24 dB; lo peor
  siempre en los últimos kHz antes de Nyquist a 44.1/48): banda ≤ 5 kHz campana 1.2 dB (bilineal 8.9), shelves 0.7
  (4.2), notch/pasa banda/cortes 1.3 (24.8); banda 10 kHz campana 1.2 (11.1), shelves 0.6 (7.2), resto 4.5 (86, en la
  zona ya cortada); banda 15–20 kHz a 44.1/48 campana 2.3 (14.2), shelves 2.6 (14.8), resto 8.3 (99). A 96 kHz la
  campana queda en 0.06 dB. Medido en el audio (`FathomTests`, campana +12 dB 10 kHz + shelf −9 dB 6 kHz): 0.63 dB a
  44.1 (bilineal 3.9), 0.015 dB a 96 (0.86). Alternar en cada bloque: lo que sale sobre 14 kHz = 0.000005 (de golpe,
  0.0054).
- `dsp/Band.h`: `BandSettings::analog` (true por defecto); `makeCascade` elige `MatchedEq` o la bilineal (`single<>`);
  el flat tilt es igual en los dos. Cambiar de diseño = rampa `analogMix` de 20 ms durante la que se calculan los dos y
  se interpolan los coeficientes (sin fundido a silencio). `Solo.h` copia `analog` a la zona; `AutoGain` lo compara.
- Parámetro nuevo (versionHint 7): `analogResponse` (bool, ON por defecto). Un proyecto de las Fases 1-6 se abre con la
  respuesta analógica (cambia arriba de ~5 kHz). Entra al deshacer, al A/B y a la automatización. El procesador lo
  copia a las 24 bandas en `getBandSettings`.
- GUI: botón ANALÓGICA en la cabecera (a la izquierda de A/B) con su explicación en el tooltip; la curva del visor
  cambia con él (`EqDisplay::sameSettings` compara `analog`).
- Tests nuevos: `DeepwaterDspTests` (contra el prototipo analógico por zona y contra la bilineal; pico, mesetas, −3 dB
  de los cortes, +x/−x, all-pass, notch, punto medio con Q 0.71, graves = bilineal con Q 40 en 10 Hz a 96 kHz, 0 dB =
  coeficientes de identidad; `fromBiquad` = biquad directo; audio = curva arriba; todas las formas con todo saltando en
  cada muestra; `blend` en los extremos); `FathomTests` (campana + shelf agudos medidos en el audio contra el analógico y
  contra la curva a 4 sample rates, la bilineal más lejos; graves iguales; alternar analógica/bilineal en cada bloque
  con bandas agudas de ±24 dB sin clic).
- Tests de la Fase 6 ajustados: "un shelf de graves sobre Nyquist sube todo 6 dB" solo es cierto con la bilineal (el
  test fija `analog = false`).
- Sin cambios: los IDs de las bandas, el analizador, latencia 0. `EqFilter.h` solo lo usa Fathom.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS. El botón ANALÓGICA no se revisó con
  capturas del Standalone; se revisó en FL.

### Lo que quedó de la Fase 6
- `dsp/Engine.h`: después de las bandas, un multiplicador por canal = salida × auto gain × balance × polaridad, todo con
  `LinearRamp` de 20 ms (si todo vale 1, bit a bit); después el solo (fundido de 20 ms entre la mezcla y la zona) y al
  final el bypass. `setGainScale` (0–2: multiplica `gainDb` de cada banda en `setBand`, incluido el flat tilt; las bandas
  ya suavizan los dB), `setPan` (balance: L = min(1, 1 − p), R = min(1, 1 + p); en mono no hace nada),
  `setPhaseInverted` (rampa +1 → −1, pasa por 0), `setAutoGain`, `setSolo (banda o −1)`, `getAutoGainDb`, `isSoloing`.
  Los ajustes pedidos antes de `prepare` arrancan sin rampa. La firma de `prepare` no cambió.
- `dsp/AutoGain.h`: 60 puntos (cada 1/6 de octava desde 20 Hz, tope 0.45·fs), peso = |K|² del filtro K de BS.1770
  (high-pass 2 polos 38.13 Hz Q 0.5 + shelf +4 dB 1681.97 Hz) normalizado; con puntos equiespaciados en octavas el ruido
  rosa pesa igual en todos. Compensación = −10·log10(Σ w·Π|H_b|²), límite ±24 dB. Cada banda guarda su |H|² y solo se
  recalcula si cambian sus ajustes (dos bandas apagadas = iguales); la suma solo si algo cambió. En el motor se actualiza
  una vez por bloque y solo con el auto gain encendido. Valores (48 kHz): shelf agudos +3 dB 1 kHz → −2.25; shelf graves
  +3 dB 200 Hz → −0.49; campana +2 dB 3 kHz Q 0.7 → −0.72; campana +6 dB Q 8 → −0.23; corte 30 Hz → +0.03; corte
  100 Hz 24 dB/oct → +0.29. La curva del visor NO incluye el auto gain (se muestra el número en la columna de salida).
- `dsp/Solo.h`: `soloZoneFor` = campana/notch/pasa banda/all-pass → pasa banda con su frecuencia y su Q; shelf y corte de
  graves → pasa bajos (el corte con su misma pendiente, el shelf 12 dB/oct, Q 0.71); de agudos → pasa altos; tilts →
  sin zona (`canSolo`). En el motor la zona es otra `Band` sobre la señal ORIGINAL (0 dB en el centro); la misma banda y
  la misma clase de filtro = sus rampas; otra banda u otra clase = fundido a la mezcla, `prepare` de la zona nueva en
  silencio y fundido al solo. Funciona aunque la banda esté apagada. El bypass manda sobre el solo.
- Parámetros nuevos (versionHint 6): `gainScale` (0–200 %, 100), `outputPan` (−100…100, "C" / "L n" / "R n"),
  `phaseInvert` (off), `autoGain` (off). Un proyecto de las Fases 1-5 suena igual.
- Procesador: `setSoloBand`/`getSoloBand` (atómico, no es parámetro: no se guarda; el editor lo pone en −1 al cerrarse),
  `getAutoGainDb` (atómico desde el audio). A/B: hijo `Compare` del estado del APVTS con `side` (0 = A, 1 = B) y un hijo
  `Other` con un atributo por parámetro (ID = valor en sus unidades; lo que falta = valor por defecto). `switchCompareSide`
  (solo sin ediciones a medias): foto del activo → `Other`, carga del otro (la primera vez, copia del activo) con
  `EditHistory::load` (con gestos, sin paso de deshacer) y `swapStack` con `otherSideHistory`: cada lado tiene su propio
  historial. `copyToOtherSide`. Cubre los mismos parámetros que el deshacer (todos menos el bypass).
- GUI: ventana de 1082 px (antes 960): columna nueva `gui/OutputPanel.h` a la derecha del analizador (ESCALA, AUTO GAIN
  con lo que compensa, BALANCE, Ø POLARIDAD, GANANCIA; la perilla de salida salió de la columna del analizador).
  Cabecera: A / B / "A → B" (o "B → A") a la izquierda de DESHACER. Panel de la banda: SOLO (amarillo, `theme::solo`)
  junto a ON; sigue a la banda del panel; atenuado en los tilts. Visor: "Escuchar su zona (SOLO)" en el menú del nodo,
  aviso "SOLO: BANDA n" (o que el tilt no tiene zona); curvas y nodos con la escala aplicada; arrastrar y crear dividen
  por la escala (mínimo 0.25) para que el nodo siga al mouse; la etiqueta muestra "+6.0 dB → +3.0 dB" con escala ≠ 100 %.
- Tests nuevos `FathomTests`: escala (50 % = la curva a la mitad < 0.01 dB; 0 % bit a bit), balance y polaridad
  (valores exactos, rampa sin saltos, mono, invertir y volver = bit a bit), auto gain (plano = 0 dB y bit a bit; todo
  +6 dB → −6 ± 0.05; angosto y extremos ≈ 0; all-pass 0; todo cortado = límite; incremental = desde cero; tono al mismo
  nivel ±0.1 dB de punta a punta; encenderlo con la banda saltando cada bloque sin clic), solo (zona medida = curva
  < 0.01 dB, centro > −1 dB, lejos < −24 dB, tilt sin solo, bypass manda) y cambiar de solo entre bandas y clases de
  filtro sin saltos (con el fundido en 10 µs el test falla: salto 0.5 contra cota 0.14) y bit a bit al soltarlo.
- Revisada con clics automatizados en el Standalone (ON, AUTO GAIN, SOLO y su aviso, A → B con cambios en B y vuelta a
  A, cada lado con su DESHACER). OJO capturas: PowerShell debe llamar a `SetProcessDPIAware` (si no, la ventana sale
  recortada) y los clics necesitan una pausa entre bajar y soltar.
- Sin cambios: las bandas, sus parámetros e IDs, el analizador, latencia 0.
- Release con 0 warnings; ctest 8/8 (UndertowTests falló una vez por su test de CPU, 20.7 % contra 20 %, sin cambios en
  Undertow; al repetirlo pasó: 19.2 %); pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 5
- `analysis/SpectrumView.h` (sin JUCE): inclinación k·log2(f / 1 kHz) con 0 / 3 / 4.5 / 6 dB/oct (4.5 por defecto);
  rango 48 / 72 / 96 / 120 dB (múltiplos de 8: la cuadrícula cae en números redondos; 96 por defecto); resolución =
  FFT de 2048 a 16384 a 44.1/48 kHz (el doble a 88.2/96: misma duración; media = 4096, la de antes); velocidad =
  constante de tiempo de subida + caída en dB/s (`follow`, independiente de los cuadros por segundo; media = 40 dB/s, la
  de antes); retención de picos No / 2 s (después cae a 20 dB/s) / Siempre (`hold`); `ViewSettings`; `findPeaks`
  (máximo de su vecindario de ±1/6 de octava y prominencia ≥ 6 dB sobre el valle más alto de los dos lados; en una
  meseta, uno solo) y `nearestPeak`.
- Librería: `SpectrumAnalyzer::peak (lowHz, highHz, sr)`: el bin más fuerte de la zona + parábola en dB por él y sus
  vecinos (< 0.05 bin y < 0.4 dB con Hann; si la zona no tiene un máximo local, el bin). Aditivo: no cambia nada de lo
  que ya usaban Riptide y Fathom.
- Procesador: bus de entrada "Sidechain" (estéreo, activo por defecto como en Ebb; acepta mono, estéreo o
  desactivado); el motor procesa solo los canales del bus principal (`getMainBusNumInputChannels`). Tercera toma
  `sidechainTap` (solo si el bus está conectado) e `isSidechainConnected()`. Tomas de 32768 muestras (la FFT máxima a
  96 kHz). La vista del analizador se guarda en el proyecto como hijo `AnalyzerView` del estado del APVTS (no son
  parámetros ni entran al deshacer; lo que falta = valor por defecto: los proyectos de las Fases 1-4 se ven igual que
  antes, salvo la inclinación).
- GUI: `gui/AnalyzerPanel.h` en la columna derecha (sobre SALIDA): ENTRADA / SALIDA / SIDECHAIN, INCLINACIÓN, RANGO,
  RESOLUCIÓN, VELOCIDAD, PICOS, BORRAR PICOS y CONGELAR (no se guarda). El editor guarda la vista al cambiarla y en
  cada cuadro compara con la del proyecto (cargar un proyecto con la ventana abierta). Visor: sidechain en ámbar,
  retención como línea fina sobre cada espectro visible, avisos "CONGELADO" y "SIDECHAIN SIN CONECTAR"; los botones
  ENTRADA/SALIDA salieron de la cabecera del visor.
- Agarrar picos (`EqDisplay`): con el mouse en el visor se buscan los picos del espectro de la SALIDA (si no se ve, la
  entrada; si no, el sidechain). A menos de 14 px en horizontal y 40 px de la línea aparece una marca con Hz y nota
  (afinados con `peak()`); clic y arrastrar = campana nueva en la primera banda libre, encendida, 0 dB, Q 4, justo en
  el pico, y se arrastra como un nodo (crear + arrastrar = un paso de deshacer); mientras tanto el espectro se congela.
  El segundo clic de un doble clic no agarra otra vez ni crea otra banda. Ctrl mientras se arrastra UN nodo = imán al
  pico más cercano (24 px); al alejarse vuelve a seguir al mouse. Los nodos tienen prioridad sobre los picos.
- Arreglado de paso: el tooltip de DESHACER tenía la "ú" codificada dos veces ("Ãº").
- Tests nuevos: `DeepwaterDspTests` (`peak()` entre bins a 4 frecuencias de muestreo y una zona sin máximo local);
  `FathomTests` (inclinación, tamaños de FFT, valores por defecto = los de antes, balística igual a 30 y 60 cuadros por
  segundo, constante de tiempo, retención en sus tres modos, picos con prominencia/meseta/piso/el más cercano, y de
  punta a punta: un seno de 87.31 Hz y otro de 2.5 kHz se agarran a < 0.5 Hz a 44.1 y 96 kHz).
- Revisada con clics automatizados en el Standalone (panel, SIDECHAIN sin conectar, CONGELAR, rango 120 dB, PICOS,
  la vista guardada al cerrar y abrir). Agarrar picos e imán NO se pudieron probar ahí (el Standalone silencia la
  entrada): solo con los tests de punta a punta; probarlos en FL.
- Sin cambios: el audio (motor, bandas, parámetros e IDs), latencia 0.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 4
- Edición sobre el visor (`gui/EqDisplay.h`): doble clic en el vacío = crear en la primera banda LIBRE (apagada y con
  sus valores por defecto; `edit::freeBand`), campana con la ganancia de la altura del clic; bajo 25 Hz corte de graves,
  sobre 16 kHz corte de agudos (shelves y cortes nacen con Q 0.71). Doble clic en un nodo = apagar/encender. Supr =
  borrar las elegidas (vuelven a sus valores por defecto: libres). Arrastrar = frecuencia en octavas + ganancia en dB
  (`edit::dragged`; shelves × 2 porque su nodo está en el punto medio; formas sin ganancia solo en horizontal; límite
  20 Hz–20 kHz), por diferencias entre eventos: Shift = fino (× 0.1) a mitad del arrastre sin saltos, Alt = solo el eje
  que más se movió. Rueda sobre un nodo = Q (Q × 2^deltaY: ≈ × 1.18 por muesca; Shift fino); el paso de deshacer de la
  rueda se cierra tras 0.5 s quieta. Selección múltiple: Ctrl+clic, rectángulo en el vacío (Ctrl = agrega), Ctrl+A =
  las que están en uso, Esc = solo la del panel; la del panel (`focusedBand`) siempre está entre las elegidas.
  Clic derecho en un nodo: encender/apagar, forma, pendiente (solo cortes), afinar a la nota más cercana, borrar (para
  todas las elegidas); en el vacío: crear con cualquier forma, elegir todas. Etiqueta con banda/forma/Hz/nota/dB/Q
  junto al nodo bajo el mouse o arrastrado. Nodos: llenos si suenan, huecos si están apagadas pero en uso (o es la del
  panel), anillo blanco = elegidas. Ayudas en los tooltips y en el pie del editor.
- Teclado de piano bajo el visor (16 px, entre la curva y las etiquetas de Hz): un semitono = una columna (eje log),
  negras arriba, cada Do con su nombre de FL (C5 = MIDI 60); la tecla de la banda del panel en su color. Clic en una
  tecla = la banda del panel justo a esa nota (y encendida).
- `gui/BandEdits.h`: cambia parámetros como una perilla (abre el gesto de cada parámetro la primera vez que lo toca y
  los cierra todos en `end()`); `setSettings`, `reset` (valores por defecto).
- Deshacer/rehacer (`edit/EditHistory.h` en el procesador: sobrevive a cerrar la ventana): escucha
  `parameterGestureChanged` de todos los parámetros menos el bypass; foto "antes" al abrirse el primer gesto, "después"
  al cerrarse el último (+ `beginTransaction`/`endTransaction` para agrupar), y `UndoStack` guarda solo los parámetros
  que cambiaron (deshacer no pisa la automatización del host, que no manda gestos). Aplica con gestos (el host graba).
  Límite 200 pasos. Cubre visor, panel y salida. Se eligió frente al `UndoManager` del APVTS porque ese registra al
  copiar el estado al ValueTree (con temporizador, mezclando la automatización) y no por gesto.
- Atajos (`gui/Keys.h`, `commandLetter`): JUCE en Windows entrega Ctrl+letra como el carácter de control (Ctrl+Z =
  código 26), así que comparar con `KeyPress ('z', ctrl, 0)` fallaba. Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z en el editor
  (botones DESHACER/REHACER en la cabecera también); Supr/Ctrl+A/Esc en el visor (que toma el foco al hacer clic).
- El procesador expone `getDefaultBandSettings (b)` (leídos de los parámetros) y `getEditHistory()`. Editor de 640 px
  de alto (antes 620) por el teclado.
- Tests `FathomTests` nuevos: `UndoStack` (solo lo que cambió, orden, rehacer se borra con un cambio nuevo, límite),
  arrastre (octavas/dB, el nodo del shelf sigue al mouse medido con la curva real, cortes solo en horizontal, límites,
  Q de la rueda), banda libre, forma según la posición, notas exactas y teclas negras.
- Revisada con clics automatizados en el Standalone (crear, arrastrar, Shift en grupo, Alt, rueda, rectángulo,
  Ctrl+clic, menú, Supr, Ctrl+Z, botón deshacer, teclado de piano).
- Sin cambios: el audio (motor, bandas, parámetros e IDs), el analizador y las escalas.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 3
- Librería (`EqFilter.h`): `EqCoefficients` ganó la salida high-pass del SVF (`highPass`; con 0 la campana suena
  igual) y las formas: `lowShelf`/`highShelf` (Cookbook sobre el SVF a la Simper: corte del SVF ÷/× √A, la frecuencia
  es el punto medio de la subida; Q 0.71 = sin rebote), `tiltShelf` (shelf de agudos × 1/A: graves −g/2, agudos +g/2,
  0 dB en la frecuencia), `notch` (1 − k·BP), `bandPassFilter` (k·BP, 0 dB en el centro), `allPass` (1 − 2k·BP),
  `lowPass2`/`highPass2` y `lowPass1`/`highPass1` (1 polo con el SVF: k = 2 y un cero que cancela un polo). `EqCascade`:
  hasta 8 etapas en serie, `butterworthHighPass/LowPass (hz, orden 1-16, q)`: Q de cada etapa 1/(2·sin((2i−1)π/2n)),
  la resonancia multiplica la etapa más resonante por q·√2 (q 0.71 = Butterworth puro, −3 dB en el corte).
  `FlatTiltDesign`: 16 pares polo-cero de 1er orden, uno por octava desde 0.4·fs hacia abajo (8 etapas de SVF con polos
  reales), razón aplicada en el eje deformado (sin tan() por muestra al moverla), normalizado a 0 dB exactos en el
  pivote; error < 0.012 dB por dB/oct hasta 1 kHz y < 0.2 dB por dB/oct a 20 kHz (la bilineal curva el final).
  Tests nuevos en `DeepwaterDspTests`: shelves (mesetas, punto medio exacto, +x/−x, Q 0.71 monótono, 0 dB bit a bit),
  notch/pasa banda/all-pass (hueco < −100 dB, ancho −3 dB, all-pass plano < 1e-5 dB también medido), Butterworth 1–16
  polos = |H|² = 1/(1 + (Ωc/Ω)^2n) exacto (< 0.00001 dB) y medido, resonancia; flat tilt (recta, pivote, bit a bit
  con 0); todas las formas estables con todo cambiando en cada muestra.
- `dsp/Band.h`: `Shape` (orden del parámetro: campana, shelf graves, shelf agudos, tilt shelf, flat tilt, corte graves,
  corte agudos, notch, pasa banda, all-pass; agregar solo al final), `slopes` {6, 12, 18, 24, 30, 36, 48, 72, 96},
  `usesGain/usesQ/usesSlope`. Flat tilt: ganancia × 0.1 = dB/oct (±3 dB/oct). Rampas de 20 ms en octavas, dB y log Q
  como en la Fase 2, más un fundido `mix` de 20 ms (salida = x + mix·(filtrada − x)): encender/apagar, y cambiar de
  forma o de pendiente = fundido a 0, cambio con el filtro en limpio (reset) y fundido a 1. Apagada y con el fundido en
  0 la banda está "idle": no procesa (cambio respecto de la Fase 2, donde el SVF corría siempre).
- `dsp/Engine.h`: 24 bandas en serie, procesadas por tramos de 64 muestras banda por banda (buffer fijo para la señal
  original del bypass); `BandArray`, `setBand (índice, ajustes)`, `getBand (índice)`.
- Parámetros (IDs definitivos, `params::band (b, ajuste)` = "band<b+1><Ajuste>"): por banda `Enabled` (off),
  `Frequency` (cada banda en la suya: 30, 40, 50, 65, 85, 115, 150, 200, 260, 350, 460, 600, 800, 1000, 1400, 1800,
  2400, 3200, 4200, 5500, 7300, 9600, 12500, 16000 Hz), `Gain`, `Q`, `Shape` (choice, Bell por defecto: los proyectos de
  la Fase 2 suenan igual) y `Slope` (choice, 12 dB/oct). La banda 1 conserva `versionHintPhase2` en sus 4 ajustes de la
  Fase 2; lo demás, `versionHintPhase3`. Cambió el valor inicial de `band1Enabled` (on → off) y `band1Frequency`
  (1 kHz → 30 Hz): no afecta a proyectos guardados (guardan sus valores).
- GUI: panel con selector de banda (las encendidas con ●), ON, FORMA, PENDIENTE (activa solo en cortes), FRECUENCIA /
  GANANCIA / Q (las que la forma no usa, atenuadas) con el color de la banda, y un texto según la forma (centro y ancho,
  punto medio, corte −3 dB y pendiente, dB/oct del flat tilt...). Visor: curva total (suma en dB de las 24 bandas + la
  salida), relleno de cada banda encendida en su color (la elegida más marcado y con su curva propia), nodos numerados
  (sobre la curva de la banda si la forma tiene ganancia, si no en 0 dB; la elegida se ve aunque esté apagada); clic en
  un nodo = elegir la banda. Combos con letra de 13 px. Revisada con capturas del Standalone (clics automatizados).
- Tests `FathomTests` nuevos: cada forma y varias pendientes con otras bandas encendidas = curva de la GUI (< 0.00001
  dB), 24 campanas en 0 dB bit a bit, corte/notch/flat tilt/corte de 6 dB apagados = bit a bit e idle, forma/pendiente/
  encendido/ajustes al azar en 3 bandas cada bloque sin clics (mitad de la cota; sin fundidos sale 1000 veces sobre
  ella), 24 bandas al azar con extremos estables. Los de la Fase 2 adaptados a la API de 24 bandas.
- Sin cambios: salida ±24 dB, bypass con fundido, tomas del analizador, latencia 0, escalas del visor.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: Fase 11 (dinámica espectral).
