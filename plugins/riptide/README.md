# Riptide

**Saturador** en formato VST3 y Standalone para Windows, escrito en C++20 con JUCE: seis modos con carácter (cinta,
bulbo, transistor, clip duro, rectificador y wavefold), tono antes y después de la curva, mezcla en paralelo con auto
gain, hasta 4 bandas y modulación por envolvente y LFO. La referencia de nivel son Soundtoys Decapitator y FabFilter
Saturn 2, con nombre, diseño y código propios. Es el segundo plugin de la [suite Deepwater Audio](../../README.md).

![Interfaz de Riptide con el preset "Bajo Gruñido, Sub Limpio"](docs/screenshot.png)

> **Estado:** las 10 fases de la hoja de ruta están completadas y probadas en FL Studio. Tiene 26 presets de fábrica
> con notas que explican cada ajuste, una interfaz escalable con la curva y el espectro en vivo, y un motor que procesa
> por bloques (la mitad de CPU que antes de la optimización, con el mismo sonido bit a bit). Ver la
> [guía de uso](#guía-de-uso) y los [límites conocidos](#límites-conocidos).

## Qué tiene

- **Seis modos de saturación**, cada uno con su curva y su comportamiento según el nivel:
  *Tape* (rodilla muy suave, comprime más los agudos, suma cuerpo en 70 Hz), *Tube* (asimétrico: armónicos pares),
  *Transistor* (limpio hasta que muerde: impares altos), *Hard Clip* (recorte seco), *Rectify* (octava arriba, sin
  fundamental) y *Fold* (wavefolding: el timbre barre al subir el drive). Cambiar de modo con música no hace clic.
- **Oversampling seleccionable** (Off, 2x, 4x, 8x, 16x) con filtros halfband de fase lineal. La latencia se informa al
  DAW y cambia con el factor sin clics.
- **Tono:** Low Cut y High Cut que se ponen antes (PRE) o después (POST) de la curva, *emphasis* (decide qué
  frecuencias se saturan más, con su inversa exacta después) y tilt.
- **Mix dry/wet** con la señal limpia alineada a la latencia (paralelo sin filtrado de peine), **AUTO GAIN** (la
  saturación cambia el carácter, no el volumen) y **DELTA** (escuchar solo lo que agrega la saturación).
- **Multibanda:** hasta 4 bandas con crossovers Linkwitz-Riley de 24 dB/oct (suma plana sin saturación), modo y drive
  propios por banda, solo y mute.
- **Modulación:** un seguidor de envolvente (la saturación reacciona a la dinámica) y un LFO (6 formas, libre o
  sincronizado al tempo y a la posición de la canción). Cada uno mueve un destino: drive, mix, emphasis, su frecuencia,
  tilt o el drive de una banda.
- **Visores:** la curva de transferencia en vivo (con el tramo que recorre tu señal) y un analizador de espectro
  antes/después con las bandas teñidas y los cruces arrastrables. Medidores IN/OUT de pico y RMS con LED de clip.
- **Presets:** navegador con categorías y búsqueda, tus propios presets con notas y 26 de fábrica (batería, bajo, voz,
  instrumentos, bus, master y creativos), cada uno con el porqué de sus ajustes y cuándo no usarlo.
- **Interfaz escalable** de 70 a 200 %, recordada en el proyecto.
- Correcto a 44.1, 48, 88.2 y 96 kHz, con bloques de 1 a 4096 muestras, en mono y estéreo. Bypass sin clics y con la
  misma latencia que el procesado. Validado con [pluginval](https://github.com/Tracktion/pluginval) en strictness 10.

### Cómo fluye la señal

```
              ┌────────────────────────────── por banda (1 a 4) ──────────────────────────────┐
entrada ─► Input ─► PRE: Low/High Cut ─► emphasis ─► crossover ─► drive de la banda ─► modo ─► auto gain ─┐
   │        ▲                                                       ▲                                    │
   │        └── envolvente / LFO (Drive, Emphasis, Band Drive…) ────┘                                    │
   │                                                                                                     ▼
   │                                                    Σ bandas ─► de-emphasis ─► POST: Low/High Cut ─► tilt ─► wet
   │                                                                                                            │
   └─► retardo = latencia (dry alineada) ───────────────────────────────────────────────────────────────► Mix ─► Delta ─► Output ─► salida
```

El emphasis sube una zona antes de la curva y la baja exactamente lo mismo después: el balance tonal no cambia, pero
esa zona satura más. El retardo de la dry es lo que permite mezclar en paralelo sin que las dos señales se cancelen.

## Compilar

Desde la raíz del repositorio (ver los requisitos en el [README de la suite](../../README.md#compilar)):

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Riptide_VST3 Riptide_Standalone
```

| Formato | Ruta |
|---|---|
| VST3 | `build/plugins/riptide/Riptide_artefacts/Release/VST3/Riptide.vst3` |
| Standalone | `build/plugins/riptide/Riptide_artefacts/Release/Standalone/Riptide.exe` |

### Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

- `RiptideTests` (`tests/EngineTests.cpp`, sin JUCE): curvas y armónicos de cada modo, aliasing, latencia, bypass bit a
  bit, cambios sin clics, tono, mix/delta, auto gain, multibanda, modulación e independencia del tamaño de bloque.
- `RiptidePresetTests` (`tests/PresetTests.cpp`, con JUCE): cada preset de fábrica, guardar y cargar, estado del
  proyecto, niveles igualados, 0 reservas de memoria en `processBlock` y una prueba de tortura (un tercio de los
  parámetros al azar cada 6 bloques, en 4 sample rates).

```bash
build/plugins/riptide/Release/RiptideTests.exe --cpu                                   # tabla de CPU
build/plugins/riptide/RiptidePresetTests_artefacts/Release/RiptidePresetTests.exe --verbose        # nivel de cada preset
build/plugins/riptide/RiptidePresetTests_artefacts/Release/RiptidePresetTests.exe --notes          # notas de los presets
build/plugins/riptide/RiptidePresetTests_artefacts/Release/RiptidePresetTests.exe --render <carpeta>   # WAV de cada preset
python scripts/level_match.py <carpeta> --ciego    # igualar la sonoridad (LUFS) y comparar a ciegas
```

## Guía de uso

### Dónde ponerlo

- **FL Studio:** agrega `build\plugins` como ruta de búsqueda de tipo **VST3** (ver el
  [README de la suite](../../README.md#en-fl-studio)). Riptide es un efecto: va en una ranura del **Mixer** (en el
  insert de la pista, en un bus o en el master), no en el Channel Rack.
- **Standalone:** útil para ver la interfaz. La entrada de audio arranca silenciada para evitar realimentación: actívala
  en **Options** si quieres pasar audio por él.

La latencia depende del oversampling (a 48 kHz: 39 / 44 / 46 / 47 muestras con 2x / 4x / 8x / 16x, menos de 1 ms) y se
informa al DAW, que la compensa. Off no tiene latencia.

### La interfaz

- **Cabecera:** ◀ ▶ y el nombre del preset (clic = navegador), **Guardar** y el menú **···**; **OVERSAMPLING**; el zoom
  (porcentaje) y **BYPASS** (el mismo que el interruptor de la ranura en el Mixer de FL).
- **IN / OUT** (a los lados): medidores. Barra tenue = pico, sólida = RMS, línea = pico de los últimos 1.5 s, LED rojo =
  alguna muestra pasó de 0 dBFS (clic para apagarlo). IN mide lo que llega a la curva: ya con Input, los cortes PRE y el
  emphasis.
- **CURVA:** la función de transferencia que suena ahora (entrada en horizontal, salida en vertical; la diagonal punteada
  es "sin cambios"). El tramo brillante es el que recorre tu señal: si se queda en la parte recta, casi no satura; si
  llega a la rodilla, satura. Con varias bandas, una curva por banda en su color.
- **ESPECTRO:** gris = ANTES, relleno = DESPUÉS (los botones encienden y apagan cada uno). Con el mouse encima lees la
  frecuencia, la nota (notación de FL) y el nivel. Con varias bandas se ven teñidas, y sus cruces se arrastran desde las
  asas de abajo (doble clic = cruce por defecto).
- **Debajo:** los seis modos, TONO, NIVEL Y MEZCLA y BANDAS. **MODULACIÓN** a la derecha.
- **Perillas:** arrastra arriba/abajo; doble clic = valor por defecto. Cuando una perilla está modulada, un arco celeste
  muestra el valor que suena de verdad.
- **Zoom:** el botón del porcentaje o la esquina inferior derecha. Se guarda con el proyecto y como preferencia.

### Controles

**Modo y nivel**
| Control | Qué hace |
|---|---|
| Modo | El carácter de la saturación (ver la tabla siguiente). |
| Input | El **drive** (−24 a +24 dB): cuánto se empuja la señal contra la curva. Es el control principal. |
| Output | Volumen final (−24 a +24 dB). Sirve para igualar el volumen con el bypass. |
| Oversampling | Off / 2x / 4x / 8x / 16x. **4x** es el punto de partida; 8x–16x para Hard Clip o Fold con agudos fuertes; Off solo para oír (o buscar) el aliasing. |
| Bypass | La señal original, con la misma latencia y sin clic. |

| Modo | Suena | Armónicos | Para qué |
|---|---|---|---|
| **Tape** | Redondo, comprime, suaviza agudos, +2 dB de cuerpo en 70 Hz | Impares discretos | Buses, master, teclados: pegar y suavizar |
| **Tube** | Lleno, grueso; con drive alto "ronca" | Pares (octavas) + impares | Voces, bajos, synths delgados: calidez |
| **Transistor** | Limpio hasta que muerde, filo y presencia | Impares altos | Guitarras, cajas, bajos que tienen que cortar |
| **Hard Clip** | Nada hasta 0 dBFS, después recorte seco | Impares, muchos | Controlar picos; distorsión agresiva |
| **Rectify** | Fuzz de octava, la nota se va una octava arriba | Pares, sin fundamental | Efecto, en paralelo |
| **Fold** | Suave al principio; al pasar de ~+4 dBFS el timbre cambia por completo | Metálicos | Diseño sonoro, con un LFO en el drive |

**Tono**
| Control | Qué hace |
|---|---|
| Low Cut + PRE/POST | Paso alto (20 Hz = Off … 2 kHz). **PRE:** esos graves no llegan a la curva (no satura y tampoco suena). **POST:** satura con todo y después se quitan los graves. |
| High Cut + PRE/POST | Paso bajo (500 Hz … 20 kHz = Off). **POST** (por defecto): quita el "fizz" que creó la curva. **PRE:** la curva sigue creando agudos por encima del corte. |
| Emphasis / Emph Freq | Sube (o baja) una zona antes de la curva y la devuelve igual después: esa zona satura más (o menos) sin cambiar el balance tonal. ±12 dB, de 50 Hz a 10 kHz. |
| Tilt | Al final de todo: más brillante (+) o más oscuro (−), con pivote en 1 kHz. No cambia qué se satura. |

**Nivel y mezcla**
| Control | Qué hace |
|---|---|
| Mix | 0 % = la original, 100 % = solo la saturada. Por debajo de 100 % es saturación en paralelo: vuelve la pegada y se queda la densidad. |
| AUTO GAIN | Compensa el volumen que cambia la curva al subir el drive; la lectura de debajo dice cuántos dB. No compensa los filtros ni el reparto del Mix: ajusta Output si hace falta. |
| DELTA | Solo lo que agrega la saturación (procesada − original). Para escuchar, no para dejarlo puesto. |

**Bandas**
| Control | Qué hace |
|---|---|
| 1 2 3 4 | Número de bandas. Con 1 es un saturador normal. Cambiarlo hace un fundido corto: es para elegir, no para automatizar. |
| Cruce 1, 2, 3 | Frecuencias entre bandas (por defecto 120 Hz, 1 kHz y 5 kHz). También se arrastran en el espectro. |
| Modo de la banda | *Global* (el de arriba) o uno propio. |
| Drive de la banda | Se suma a Input (±24 dB). Negativo = esa banda más limpia que el resto. |
| S / M | Solo y mute (el solo gana). Con DELTA y una banda en solo oyes lo que la saturación le agrega a esa banda. |
| auto ±x dB | La compensación del auto gain de esa banda. |

**Modulación**
| Control | Qué hace |
|---|---|
| Envolvente: Attack / Release | Qué tan rápido sube y baja la envolvente (0.1–100 ms y 5 ms–2 s). En graves, Release de 80 ms o más. |
| Envolvente: Sens | Sensibilidad (±24 dB). Escucha lo que **entra** a Riptide (antes de Input). Ajústala mirando la barra: ni siempre llena ni siempre vacía. |
| LFO: Forma | Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold. |
| LFO: SYNC / Rate / División | Con SYNC, en figuras (8 compases a 1/32, tresillos y puntillos), en fase con la canción; sin SYNC, 0.02–20 Hz. |
| Destino + Amount | Drive, Mix, Emphasis, Emph Freq, Tilt o Band 1–4 Drive; Amount de −100 a +100 %. El 100 % recorre toda la perilla (en Drive, 48 dB): empieza con 15–30 %. Negativo en la envolvente = menos drive en cada golpe. |

Todos los parámetros se pueden automatizar desde el DAW (en FL: clic derecho en el parámetro del plugin → *Create
automation clip*).

### Flujo de trabajo

1. **Nivel de entrada.** Que la pista llegue con unos −18 dBFS RMS (medidor IN con Input en 0 dB). Si llega mucho más
   fuerte o más débil, el mismo Input satura muy distinto.
2. **Enciende AUTO GAIN** antes de decidir nada. Sin igualar el volumen, el oído siempre elige "más fuerte".
3. **Elige el modo** con un drive moderado (+6 a +9 dB) y alterna entre ellos.
4. **Ajusta Input** hasta que el carácter aparezca, y retrocede un poco. Mira la CURVA: el tramo brillante debe tocar la
   rodilla, no recorrerla entera.
5. **Tono:** decide qué se satura (emphasis, Low Cut PRE) y limpia lo que sobra después (High Cut POST, tilt).
6. **Mix** si perdiste pegada. **DELTA** un momento para oír qué estás agregando.
7. **Compara con BYPASS** y ajusta Output hasta que el volumen sea el mismo. Si con el volumen igualado no suena mejor,
   quítalo.

### Tres recetas para empezar

- **Calidez en una voz:** *Tube*, Input **+4.5 dB**, AUTO GAIN. Poco drive: armónicos pares que engordan sin ensuciar.
  Sube Input hasta que la voz se sienta "más cerca" y retrocede un poco. (Preset: *Voz Cálida Tube*.)
- **Un 808 que se oye en el celular:** *Tube*, Input **+9 dB**, Emphasis **+7.5 dB** en **100 Hz**, High Cut POST en
  **6 kHz**, AUTO GAIN. Para comprobarlo, pon después un EQ que corte todo por debajo de 150 Hz y alterna el bypass.
  (Preset: *808 en el Celular*.)
- **Bus de batería en paralelo:** *Transistor*, Input **+18 dB**, AUTO GAIN, Mix **30 %**, Low Cut PRE en **90 Hz**,
  High Cut POST en **7 kHz** y Output **+3 dB** (el corte PRE quita graves de la parte saturada y el auto gain no lo
  compensa): los golpes pegan igual y entre golpe y golpe hay más relleno. (Preset: *Bus Batería Paralelo*.)

### Presets

Clic en el nombre abre el navegador: categorías a la izquierda, lista y búsqueda en el centro (busca también en las
notas: prueba "paralelo", "envolvente", "808") y las notas a la derecha. Un clic o las flechas = cargar para escuchar;
doble clic o Enter = cargar y cerrar; Esc = cerrar. **Guardar** pide nombre, categoría, autor y notas; tus presets
quedan en `Documentos\Riptide\Presets` como `.rtpreset` (XML legible) y se pueden borrar desde el navegador (van a la
papelera). El menú **···** tiene *Init*. Los presets no cambian el bypass.

| Categoría | Presets |
|---|---|
| Init | Init |
| Batería | Bombo Redondo, Caja con Mordida, Toms con Cuerpo |
| Bajo | 808 en el Celular, Bajo Gruñido, Sub Limpio, Bajo Paralelo Tube, Bajo Synth con Filo |
| Voz | Voz Cálida Tube, Voz Locución, Voz Presente Cinta |
| Instrumentos | Guitarra Crunch, Pad que Respira, Synth Engordado, Teclados Cinta Vieja |
| Bus | Bus Batería 3 Bandas, Bus Batería Cinta, Bus Batería Paralelo, Bus Batería Pegada y Cuerpo |
| Master | Master Cinta Sutil, Master Clipper de Picos |
| Creativo | Color al Azar, Fold Metálico, Lo-Fi Aliasing, Octavador Rectify, Radio AM |

Con las señales de prueba, cada preset queda a ±1.5 dB del bypass. Son puntos de partida: el Input correcto depende de
con qué nivel llega tu pista. Los de fábrica están en [`Presets/Factory`](Presets/Factory) y cada archivo solo escribe
lo que cambia respecto a *Init*, así que se lee como una receta.

### Errores comunes y cuándo no usarlo

- **Comparar con distinto volumen.** El error número uno. Usa AUTO GAIN o iguala con Output contra el bypass.
- **Oversampling Off con Hard Clip o Fold** en material brillante: aliasing (tonos metálicos que no siguen a la nota).
- **16x "por si acaso"** en muchas pistas: gasta CPU sin diferencia audible con las curvas suaves. 4x basta casi siempre.
- **Rectify o Fold en un bajo que lleva la armonía:** la fundamental desaparece. Úsalos en una capa en paralelo.
- **Tape en todo:** el cuerpo en 70 Hz se acumula pista tras pista.
- **Low Cut PRE para "limpiar" el bajo:** también quita esos graves de la salida. Para graves limpios y medios sucios usa
  emphasis negativo en graves, dos bandas (*Bajo Gruñido, Sub Limpio*) o el Mix.
- **DELTA puesto al exportar:** si una pista suena como un fantasma, revisa DELTA y los S/M de las bandas.
- **Cuándo no saturar:** pistas que ya vienen distorsionadas o muy comprimidas, instrumentos acústicos donde la dinámica
  es lo importante, y el master si no puedes comparar con el volumen igualado.

Hay ejercicios de escucha, más recetas, retos y vocabulario de cada fase en **[APRENDIZAJE.md](APRENDIZAJE.md)**, el
cuaderno de mezcla del proyecto.

## Rendimiento

CPU de un núcleo a 48 kHz, estéreo, bloques de 256 (laptop; mejor de 5 con `RiptideTests --cpu`), antes y después de
la optimización de la Fase 10. El audio es idéntico bit a bit: 384 escenarios renderizados con el motor viejo y el
nuevo dieron el mismo SHA-256.

| Escenario | Antes | Ahora |
|---|---|---|
| Init (Tape, 4x) | 1.55 % | 0.98 % |
| Tape, oversampling Off | 0.79 % | 0.59 % |
| Tape 16x | 4.19 % | 2.17 % |
| 4 bandas, 4x | 5.07 % | 3.18 % |
| 4 bandas, 16x | 16.6 % | 8.5 % |
| Peor caso: 4 bandas 16x + tono + mix + auto gain + modulación | 16.6 % | 9.3 % |

La mejora vino de procesar el oversampling por tandas de 32 muestras en vez de muestra a muestra: así el procesador no
tiene que esperar a que una escritura en memoria esté lista para leerla con una carga SIMD.

## Estructura del código

Todo es relativo a `plugins/riptide/`; lo compartido con la suite (oversampling, crossovers, filtros, LFO, seguidor de
envolvente, analizador) está en [`libs/dsp`](../../libs/dsp) y el sistema de presets en [`libs/presets`](../../libs/presets).

```
Source/
  PluginProcessor.*   parámetros, estado, processBlock, publicación de medidores y visores
  PluginEditor.*      interfaz (contenido a tamaño fijo dibujado con una escala: zoom)
  Parameters.h        IDs de parámetros (no cambian: los proyectos guardados dependen de ellos)
  dsp/                motor sin JUCE: Engine (la cadena), Saturator (modos), Tone, AutoGain, Bands, Modulation
  gui/                tema, LookAndFeel, medidores, curva de transferencia y espectro
  presets/            formato .rtpreset (categorías, carpeta, BinaryData)
Presets/Factory/      presets de fábrica, compilados dentro del plugin
tests/                tests del motor (sin JUCE) y de presets (con JUCE)
APRENDIZAJE.md        cuaderno: conceptos, ejercicios, recetas, retos y vocabulario de cada fase
```

## Hoja de ruta

- [x] 1 — Proyecto base: passthrough, input/output, medidores y bypass sin clics
- [x] 2 — Saturación tanh con oversampling seleccionable (2x–16x) y filtro DC
- [x] 3 — Seis modos con carácter: Tape, Tube, Transistor, Hard Clip, Rectify y Fold
- [x] 4 — Tono: cortes PRE/POST, emphasis y tilt
- [x] 5 — Mix dry/wet alineado, auto gain y delta
- [x] 6 — Multibanda: crossovers Linkwitz-Riley, hasta 4 bandas
- [x] 7 — Dinámica y modulación: envolvente y LFO
- [x] 8 — Interfaz profesional: curva en vivo, espectro antes/después, bandas, zoom
- [x] 9 — Presets con notas educativas
- [x] 10 — Pulido: CPU a la mitad, pluginval estricto, comparación con el volumen igualado

### Límites conocidos

- El AUTO GAIN es un modelo de cada curva, no una medición: con mucho drive puede quedar hasta un par de dB por debajo
  (Rectify con un tono puro, casi 3 dB). Para la decisión final, iguala de oído con Output.
- El auto gain no compensa los cortes, el tilt ni el reparto del Mix (los presets que lo necesitan llevan Output).
- Con BYPASS el motor sigue calculando (a propósito: al volver no hay saltos), así que cuesta lo mismo que activo.
- El analizador mide en mono ((L + R) / 2) y por debajo de 100 Hz su resolución es de ~11 Hz.
- Cambiar el número de bandas o el oversampling hace un fundido corto a silencio: no conviene automatizarlos.

## Licencia

[GNU AGPLv3](../../LICENSE). Riptide usa [JUCE](https://juce.com), que se distribuye bajo AGPLv3 o bajo una licencia
comercial.
