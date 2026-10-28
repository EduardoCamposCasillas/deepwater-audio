# APRENDIZAJE — Tidechart (acordes y melodías por tonalidad)

Cuaderno de teoría musical y composición. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — El cable: MIDI que entra, MIDI que sale

Tidechart todavía no inventa ni una nota: lo que entra sale igual. Lo que aprende en esta fase es a **ser un buen
cable**: recibir las notas del Piano Roll, mandarlas a otro instrumento y no dejar nunca una nota sonando para
siempre. Todo lo que viene (acordes con una tecla, progresiones, dembow, melodías) se construye encima de este cable,
así que conviene entender bien qué viaja por él.

### Conceptos aprendidos

- **MIDI no es sonido: son instrucciones.** "Empieza a sonar la nota 60 con fuerza 100", "deja de sonar la nota 60".
  Cómo suena lo decide el instrumento que las recibe: las mismas notas en Undertow son un pad y en FLEX un piano. Por
  eso Tidechart no tiene salida de audio útil (escribe silencio) y necesita otro instrumento detrás.
- **Note-on y note-off.** Cada nota son dos mensajes: el de empezar (con la **velocidad**, 1–127, cuánta fuerza; en
  casi todos los instrumentos = volumen y brillo) y el de terminar. Un note-on con velocidad 0 también es un note-off
  (lo manda mucho hardware). Entre los dos, la nota suena: la duración de una nota en el Piano Roll es la distancia
  entre su note-on y su note-off.
- **Número de nota.** Cada tecla es un número de 0 a 127; un semitono = +1, una octava = +12. FL llama **C5** a la 60
  (el Do central) y **A5** a la 69 (el La de 440 Hz). Ojo: otros programas llaman C4 a la 60; Tidechart usa los
  nombres de FL para que coincidan con el Piano Roll.
- **Canal (1–16).** Cada mensaje lleva un canal, como 16 cables dentro de uno. En FL casi siempre es el 1.
- **CC (control change).** Mensajes que mueven un control: CC 1 = rueda de modulación, CC 64 = pedal de sustain. Dos
  especiales: **CC 123 (All Notes Off)** y **CC 120 (All Sound Off)**, "apaga todo lo de este canal".
- **Nota colgada (stuck note).** Un note-on que nunca recibió su note-off: el instrumento la toca para siempre. Pasa
  cuando algo corta el flujo a mitad de una nota (el plugin se reinicia, se carga un proyecto, un efecto MIDI cambia de
  acorde y "olvida" apagar el anterior). Es el error más típico de las herramientas MIDI, y en un pad con release largo
  arruina una sesión. Tidechart lleva la cuenta de **cada nota que mandó y todavía no apagó** (por canal y número) y,
  cuando algo corta el flujo, manda exactamente los note-off que faltan.
- **Pánico.** El botón de emergencia de toda la vida: note-off de todo lo que Tidechart sabe que suena, más CC 123 en
  los 16 canales por si el instrumento tiene notas colgadas que vinieron de otro sitio.
- **Tempo y posición.** Igual que Ebb, Tidechart lee del host el tempo, el compás y dónde va la canción (en negras),
  y detecta los loops y los saltos. Hoy solo se muestran; desde la Fase 4 la progresión cambiará de acorde según esa
  posición (ver el `APRENDIZAJE.md` de Ebb, Fase 1, para el detalle de PPQ, compases y subdivisiones).

### Qué muestra cada parte de la pantalla

- **Reloj del host**: estado (▶ Reproduciendo, ■ Parado, ○ Reloj libre), compás . tiempo . semicorchea, una luz por
  tiempo, tempo, compás, B:S (el contador Bar:Step de FL), loop, saltos y audio (frecuencia y tamaño del bloque).
- **SUENA AHORA**: un teclado con las 128 notas MIDI; se encienden las que suenan **en el instrumento que recibe la
  salida** según lo que salió de Tidechart. Cuando sueltas todo, tiene que quedar apagado: una tecla que se queda
  encendida sería una nota colgada. Al lado, cuántas veces se apagó todo a la fuerza (pánico, reinicio del audio con
  notas sonando, cargar un proyecto).
- **ENTRADA / SALIDA**: los últimos eventos que llegaron y que salieron, el más nuevo arriba, con su posición, canal y
  mensaje. En esta fase las dos columnas son iguales (passthrough); cuando Tidechart arme acordes, en la salida verás
  varias notas por cada una de la entrada.
- **Pánico** y **Bypass** (el mismo bypass del botón de FL; hoy no cambia nada porque Tidechart aún no transforma).

### Ejercicio guiado: rutear Tidechart a un instrumento en FL

1. Carga **Tidechart** en el Channel Rack (**+** → aparece bajo **Deepwater**, como instrumento) y también
   **Undertow** (o FLEX, o cualquier sinte).
2. En Tidechart, abre el **engranaje** del wrapper de FL (Plugin options, arriba a la izquierda de la ventana) → sección
   **MIDI** → **Output port**: pon **1**.
3. En Undertow, el mismo engranaje → **MIDI** → **Input port**: **1**.
4. Selecciona el canal de **Tidechart** en el Channel Rack y toca el teclado de la compu o un teclado MIDI: la columna
   ENTRADA se mueve, la SALIDA igual, el teclado SUENA AHORA se enciende y **suena Undertow**. Si ENTRADA se mueve pero
   no suena nada, el problema está en los puertos (paso 2–3).
5. Abre el **Piano Roll de Tidechart** (no el de Undertow), escribe 4 notas y reprodúcelas en loop. Mira cómo las
   posiciones de los eventos coinciden con dónde pusiste las notas, y cómo **saltos** sube con cada vuelta del loop.
6. Para la canción a mitad de una nota larga: FL manda el note-off y SUENA AHORA queda apagado.
7. Con una nota sonando (mantén una tecla), cambia el **buffer** de audio en Options → Audio settings. FL reinicia el
   audio: Tidechart apaga la nota él solo y el contador "todo apagado" sube 1.

### Receta: una sola parte, dos instrumentos (capas)

Pon **dos** instrumentos a escuchar el **mismo puerto** (p. ej. un pad de Undertow con ataque lento y un pluck corto
de FLEX, los dos con Input port 1). Lo que escribas en el Piano Roll de Tidechart sale por los dos a la vez: el pluck
marca el ritmo (el ataque) y el pad rellena (el cuerpo). Es la técnica de **layering** de casi todo el reggaetón
moderno en acordes y leads, y tiene una ventaja sobre copiar las notas en dos Piano Rolls: cambias una nota en un solo
sitio. Bájale el volumen al pad en el Mixer hasta que se sienta debajo del pluck, no al lado.

### Reto (sin receta)

Escribe en el Piano Roll de Tidechart un acorde con una nota grave separada (p. ej. C3 abajo y E5–G5–B5 arriba) y
haz que el pad suene con todo pero el 808 **solo** con la nota grave, los dos escuchando el mismo puerto. Pista 1:
Tidechart todavía no separa notas, así que el filtro tiene que estar en el lado que recibe. Pista 2: FL deja limitar
qué teclas acepta un canal (el rango del teclado en la configuración del canal); búscalo y deja al 808 solo la zona
grave. (En la Fase 6 Tidechart generará el bajo él mismo.)

### Teoría aplicada: los números de nota son intervalos

Como cada semitono es +1, la distancia entre dos números de nota ES el intervalo: 60 → 64 son 4 semitonos (tercera
mayor: C5–E5), 60 → 63 son 3 (tercera menor: C5–D#5), 60 → 67 son 7 (quinta justa: C5–G5). Toca en el Piano Roll
C5–E5–G5 (60, 64, 67) y luego C5–D#5–G5 (60, 63, 67): solo se movió **una** nota un semitono y el acorde pasó de mayor
(brillante) a menor (oscuro). Mira la columna SALIDA: es exactamente esa diferencia de un número. La Fase 2 convierte
estas cuentas en un motor de teoría (escalas, acordes, grados).

### Vocabulario

- **Note-on / note-off**: empezar y terminar una nota. "La nota se quedó pegada" = se perdió su note-off.
- **Velocidad (velocity)**: la fuerza de la nota (1–127). En el reggaetón, los hi-hats con velocidades alternadas
  (fuerte-suave) son lo que les da "swing" sin mover nada de sitio.
- **Canal MIDI**: uno de los 16 "cables" dentro de un puerto.
- **Puerto MIDI (FL)**: el número que conecta la salida de un plugin con la entrada de otro.
- **Passthrough**: dejar pasar algo sin tocarlo.
- **Layering**: varios instrumentos tocando la misma parte para sumar sus caracteres (pluck + pad, 808 + sub).
- **Pánico (MIDI panic)**: apagar todo a la fuerza.

### Errores comunes

- Escribir las notas en el Piano Roll **del instrumento** en vez del de Tidechart: suenan, pero no pasan por Tidechart
  (la columna ENTRADA no se mueve).
- Dos instancias de Tidechart con el mismo Output port: sus notas se mezclan en los instrumentos que escuchan ese
  puerto. Usa un número distinto por función (acordes 1, bajo 2, melodía 3).
- Tidechart no suena por sí mismo: sin un instrumento escuchando su puerto, el silencio es lo esperado.
