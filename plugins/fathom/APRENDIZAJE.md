# APRENDIZAJE — Fathom (EQ paramétrico)

Cuaderno de mezcla del EQ. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — El lienzo: ver el sonido antes de tocarlo

Fathom todavía no ecualiza: la señal solo pasa por la ganancia de salida. Lo que aprende en esta fase es a **mirar**.
Un EQ es una herramienta para mover volumen *por frecuencias*, y antes de mover nada hay que saber leer dónde está
cada cosa. El visor de esta fase es el lienzo sobre el que, desde la Fase 2, se dibujan las bandas.

### Conceptos aprendidos

- **Frecuencia.** Cuántas veces por segundo vibra algo (Hz). Graves = lento (un 808 en A2 de FL vibra a 55 Hz),
  agudos = rápido (la "s" de una voz, 6–9 kHz). El oído humano va de ~20 Hz a ~20 kHz.
- **Octava = el doble.** 55 → 110 → 220 → 440 Hz son el mismo La en cuatro octavas. El oído oye las frecuencias
  *proporcionalmente*: de 100 a 200 Hz "se siente" igual de lejos que de 5 a 10 kHz.
- **Eje logarítmico.** Por eso el visor no reparte los Hz en línea recta: cada octava ocupa lo mismo. Si fuera lineal,
  todo lo que importa de un bajo (20–250 Hz) cabría en el 1 % izquierdo de la pantalla.
- **Espectro.** Una foto de cuánto nivel hay en cada frecuencia en este momento. Una nota de un instrumento no es una
  sola frecuencia: es una **fundamental** (la nota) más **armónicos** encima (2×, 3×, 4×...), que son el timbre.
- **dBFS.** Nivel digital: 0 dBFS es el máximo antes de recortar; todo lo demás es negativo. La escala de la
  izquierda del visor está en dBFS: un seno de 0 dBFS marca 0.
- **Dos escalas.** La de la derecha (±12 dB) es la del **EQ**: cuánto sube o baja una frecuencia. La de la izquierda
  es la del **analizador**. Son independientes a propósito: un realce de +2 dB (grande para el oído) tiene que verse
  bien aunque el espectro ocupe 90 dB.
- **El mapa de frecuencias** (la franja de abajo; pasa el mouse sobre cada zona):

  | Zona | Rango | Qué vive ahí | De más | De menos |
  |------|-------|--------------|--------|----------|
  | SUB | 20–60 Hz | fundamental del 808, golpe del kick | retumba, se come el headroom | sin peso en el club |
  | GRAVES | 60–250 Hz | cuerpo del bajo y del kick, calidez de voz | pesado | delgado |
  | BARRO | 250–500 Hz | casi todo se acumula aquí | turbio, "embarrado" | hueco |
  | MEDIOS | 500 Hz–2 kHz | caja, nasal; lo que suena en el celular | "de caja", nasal | se pierde en bocinas chicas |
  | PRESENCIA | 2–5 kHz | claridad de voz, ataque de percusión | agresivo, cansa | lejano, apagado |
  | BRILLO | 5–10 kHz | eses, hi-hats, platillos | sibilante, áspero | opaco |
  | AIRE | 10–20 kHz | sensación de espacio | chillón, ruido | cerrado |

- **Frecuencia ↔ nota.** Al pasar el mouse, el visor dice la nota de FL más cercana y cuántos *cents* (centésimas de
  semitono) se aleja. Sirve para saber qué nota resuena: si un 808 retumba en 49 Hz, eso es un G2 de FL.

### Qué hace cada control (y qué muestra la pantalla)

- **ENTRADA** (línea gris): el espectro de lo que llega al plugin.
- **SALIDA** (relleno azul): lo que sale. Lo que cambie entre las dos lo hizo Fathom. Hoy solo cambia con SALIDA: las
  dos curvas tienen la misma forma y la azul sube o baja entera.
- **SALIDA** (perilla, ±24 dB): el volumen al final. Mueve la línea blanca (la curva del EQ): en esta fase la "curva"
  es una recta porque sube o baja todo por igual. Doble clic = 0 dB.
- **±12 dB**: la escala del EQ (6, 12, 24 o 36 dB). No cambia el sonido, solo el zoom vertical de la curva.
- **BYPASS**: escuchar el original, con fundido de 20 ms (sin clics). Es el mismo botón que el de FL.

### Ejercicio de escucha guiado en FL Studio

1. Pon Fathom en el mixer de un loop de dembow completo (kick, 808, percusión, voz si tienes). Reproduce.
2. Mira solo la ENTRADA. Señala con el mouse: ¿dónde está el pico más alto? (casi siempre el 808 o el kick, abajo a la
   izquierda). Lee su nota. ¿Coincide con la tonalidad de la canción?
3. Silencia todo menos el 808 (en el mixer de FL): ¿cuántos picos ves? El primero es la fundamental; los siguientes
   están a 2×, 3×... Ahora solo el hi-hat: ¿en qué zona vive? Ahora la voz.
4. **El sesgo del volumen**: con todo sonando, sube SALIDA a +1.5 dB y alterna con BYPASS con los ojos cerrados (o
   pídele a alguien que lo haga). Casi todo el mundo elige la versión más fuerte como "mejor", aunque no tenga nada
   distinto. Esa trampa es la razón por la que en esta suite se compara siempre con el volumen igualado.

### Receta: el "chequeo de mapa" antes de ecualizar

Antes de tocar un EQ en cualquier pista: (1) mira el espectro con la pista sola y anota en qué zona está su energía
principal; (2) hazlo con la pista que más le choca (kick ↔ 808, voz ↔ acordes); (3) si los dos picos caen en la misma
zona, ahí habrá que tallar espacio (Fases 2–3). Es más rápido que buscar a oído y te entrena a asociar zonas con
sonidos.

### Reto sin receta

Encuentra la **nota** de tu 808 en un beat tuyo usando solo el visor (sin mirar el piano roll). Luego compruébala en el
piano roll. Pistas (si no sale): silencia lo demás; el pico más bajo y alto es la fundamental; si el 808 tiene glide,
pausa y mira el espectro quieto; acuérdate de que A5 de FL = 440 Hz, así que A1 = 27.5 Hz y A2 = 55 Hz.

### Vocabulario

- **Fundamental / armónicos**: la nota y sus múltiplos. En un 808 distorsionado (trap, reggaetón) los armónicos son
  los que hacen que se oiga en el celular aunque la fundamental (40 Hz) no salga por esa bocina.
- **Octava**: el doble de frecuencia. El "bajo una octava más arriba" de muchos drops.
- **Escala logarítmica**: cada octava ocupa lo mismo (como las teclas de un piano).
- **dBFS**: decibelios respecto del máximo digital (0).
- **Headroom**: el margen entre el pico y 0 dBFS. El sub se come el headroom: mucha energía que casi no se oye.
- **Sesgo del volumen (loudness bias)**: lo más fuerte parece mejor. Por eso existen los botones de auto gain.

### Errores comunes (y cuándo NO usarlo)

- **Mezclar con los ojos**: el analizador ayuda a ubicar, pero la decisión es del oído. Un pico enorme en 50 Hz puede
  ser exactamente lo que el beat necesita.
- **Comparar sin igualar el volumen** (ver el ejercicio 4).
- **Esperar que el analizador muestre todo igual de "plano"**: la música real cae hacia los agudos (más energía en
  graves). Un espectro plano sonaría chillón. (En la Fase 5 llega la inclinación de 4.5 dB/oct que compensa eso al
  mirar.)
