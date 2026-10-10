# CLAUDE.md — Saturador (JUCE / C++ moderno)

## Objetivo del proyecto
Construir un saturador de calidad profesional (referencia de nivel: Soundtoys Decapitator y FabFilter Saturn 2),
con nombre, diseño y código propios. Se usa principalmente en FL Studio sobre Windows, en bajos, voces,
baterías, buses y master.

Es el segundo plugin del dueño del proyecto, que ya construyó un sintetizador wavetable con JUCE.
Sigue APRENDIENDO programación de audio, DSP y ahora también MEZCLA: el objetivo es doble,
un plugin de nivel profesional y que él entienda cada parte y sepa cuándo y cómo usar saturación.

## Modo de trabajo (muy importante)
- Trabajar SIEMPRE por fases pequeñas. Una fase a la vez.
- Antes de implementar un concepto de DSP nuevo, explicarlo en español en 1–3 párrafos:
  qué problema resuelve, la idea matemática básica y cómo se traduce a código.
- Al terminar cada fase: compilar, confirmar que no hay warnings y explicar cómo probarla en FL Studio.
- NO avanzar a la siguiente fase hasta que yo confirme que la probé en FL Studio.
- Si hay varias formas de resolver algo, explicar brevemente las opciones y por qué se elige una.
- Comentarios en el código en español, educativos pero concisos (explicar el "por qué", no el "qué").
- Al terminar cada fase, actualizar la sección "Estado actual" de este archivo.
- Nunca copiar código de proyectos GPL/AGPL de terceros ni intentar replicar algoritmos propietarios.
  Se pueden estudiar ideas publicadas (papers, blogs, libros), no copiar código.

## Librería DSP compartida
El sintetizador ya tiene piezas probadas (oversampling, filtros, suavizado de parámetros, utilidades).
- En la Fase 1, mover el DSP reutilizable a una librería compartida (repo o carpeta común) que usen
  ambos plugins, sin romper el sintetizador.
- Todo DSP nuevo que sirva para futuros plugins (EQ, compresor, delay, reverb) va en esa librería.
- La librería no depende de la GUI y tiene tests para las piezas críticas.

## Aprendizaje de mezcla y diseño sonoro (igual de importante que el código)
Al terminar cada fase:
1. **Qué hace cada control al sonido**, en lenguaje musical: calidez, brillo, aspereza, densidad, pegada,
   presencia. Relacionarlo con el analizador de espectro (qué armónicos aparecen) y la forma de onda.
2. **Ejercicio de escucha en FL**, siempre con el volumen igualado (gain-matched), sobre material real:
   un loop de batería, un bajo, una voz. Qué mover, de qué valor a cuál y qué notar.
3. **Receta de uso**: cómo usar lo construido en un caso real de mezcla (ej. dar presencia a un 808
   en bocinas pequeñas, engrosar una voz, pegar un bus de batería), explicando por qué.
4. **Reto sin receta**: describir un objetivo de mezcla, dejar que yo lo intente y luego dar pistas.
5. **Vocabulario** nuevo con definición corta y ejemplo de dónde se escucha en música real
   (ej. armónicos pares/impares, headroom, clipping suave/duro, aliasing, crossover).
6. **Errores comunes**: cuándo NO usar saturación o cuándo es demasiada.

Actualizar el archivo `APRENDIZAJE.md` del proyecto al final de cada fase (conceptos, recetas de uso,
vocabulario, retos completados). Los presets de fábrica incluyen notas de para qué sirven y por qué.

## Stack técnico
- C++20, MSVC (Visual Studio 2026 o 2022), JUCE 8 vía CMake FetchContent fijado a un tag estable, CMake >= 3.22.
- Formatos: VST3 + Standalone.
- Validación con pluginval antes de cerrar fases que toquen parámetros, estado o procesamiento.
- Reportar latencia correctamente al host (`setLatencySamples`) cuando el oversampling o los filtros la agreguen.

## Reglas del hilo de audio (NO negociables)
- Nada de reservar/liberar memoria, locks, I/O, logging ni llamadas al sistema dentro de `processBlock`.
- Buffers reservados en `prepareToPlay`; cambiar el factor de oversampling no debe causar clics ni crasheos.
- Parámetros suavizados (`SmoothedValue`); `juce::ScopedNoDenormals` al inicio de `processBlock`.
- Correcto a 44.1, 48, 88.2 y 96 kHz, con cualquier tamaño de buffer, en mono y estéreo.
- Bypass sin clics y con la misma latencia que el procesamiento activo.

## Estilo de código
- C++ moderno: RAII, `std::unique_ptr`, `std::array`, `constexpr`, `noexcept` en código de audio.
- Nada de `new`/`delete` manuales. DSP separado de JUCE en clases puras y testeables.
- Nombres en inglés para el código; comentarios en español.

## Hoja de ruta
- [ ] Fase 1 — Proyecto base y librería compartida: passthrough con input/output gain, medidores de nivel,
      DSP del synth movido a la librería compartida (el synth sigue compilando y sonando igual).
- [ ] Fase 2 — Waveshaping básico + oversampling seleccionable (2x a 16x), filtro DC, comparación con
      y sin oversampling en el analizador para escuchar y ver el aliasing.
- [ ] Fase 3 — Modos de saturación con carácter: cinta (suave, compresión, realce de graves),
      bulbo (asimétrico, armónicos pares), transistor (más duro, impares), clip duro, rectificador y wavefold.
      Cada modo con su propia curva y comportamiento según el nivel de entrada.
- [ ] Fase 4 — Tono: filtros pre y post saturación (HP/LP), tilt EQ y control de "emphasis"
      para decidir qué frecuencias se saturan más.
- [ ] Fase 5 — Mix dry/wet con compensación de fase/latencia, compensación automática de volumen
      (auto gain) y modo delta (escuchar solo lo que agrega la saturación).
- [ ] Fase 6 — Multibanda: crossovers Linkwitz-Riley (hasta 4 bandas), modo y drive por banda,
      solo/mute por banda, suma plana cuando no hay saturación.
- [ ] Fase 7 — Dinámica y modulación: envelope follower y LFO que modulan el drive u otros parámetros.
- [ ] Fase 8 — GUI profesional: curva de transferencia en vivo, analizador de espectro antes/después,
      visualización de bandas, escalable.
- [ ] Fase 9 — Presets con notas educativas (batería, bajo, voz, bus, master, efectos creativos).
- [ ] Fase 10 — Pulido: optimización de CPU, pluginval estricto, comparación gain-matched contra referencias.

## Estado actual
Fase 1 sin empezar.