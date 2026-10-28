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

## Fase 2 — El motor de teoría: escalas, acordes y grados

Esta fase construye el "cerebro" musical de Tidechart (`libs/theory`): sabe qué notas tiene cada escala, cómo se
arma cualquier acorde, cómo se llama, qué papel juega en una tonalidad y de qué modo viene si "no pertenece". Todavía
no genera notas (eso es la Fase 3), pero ya **escucha**: la fila **ACORDE** nombra lo que suena mientras tocas.

### Conceptos aprendidos

- **Clase de altura vs. nota escrita.** Para el piano, Re♯ y Mi♭ son la misma tecla (la misma *clase de altura*:
  todas las Mi♭ de todas las octavas). Para la música no: en Si mayor la nota es **Re♯** (la 3.ª de Si) y en Do menor es
  **Mi♭** (la 3.ª menor de Do). La regla: **cada escala usa las 7 letras una vez**. Fa mayor es F G A **B♭** C D E: si
  escribieras A♯ tendrías dos "A" y ninguna "B". Por eso Tidechart guarda letra + alteración, y los intervalos llevan
  dos datos: cuántas letras sube (3.ª = 3 letras) y cuántos semitonos (menor = 3, mayor = 4).
- **Intervalos que dan el carácter.** La **3.ª** decide mayor (4 semitonos, brillante) o menor (3, oscuro). La
  **5.ª justa** (7) es el "pegamento" neutro: por eso se puede omitir sin que el acorde cambie de nombre. La **7.ª
  menor** (10) añade movimiento (blues, R&B, la dominante); la **7.ª mayor** (11), un brillo que flota (maj7, el sonido
  "chill"). El **tritono** (6 semitonos) es el intervalo más inestable: está dentro de toda dominante (en G7, Si–Fa) y
  pide resolver.
- **Escalas y modos.** Un modo es la escala mayor empezando en otra nota, y cada uno tiene **una nota que lo
  distingue**: dórico = menor con 6.ª mayor (menos triste, más "groove"); **frigio = menor con ♭2** (el semitono encima
  de la tónica: oscuro, español, el del perreo más tenso); lidio = mayor con ♯4 (soñador, de película); mixolidio =
  mayor con ♭7 (rock, "mayor relajado"); eólico = el menor natural; locrio = con 5.ª disminuida (casi no se usa como
  tonalidad). La **menor armónica** sube la 7.ª para tener sensible (el Mi mayor en La menor: el V que tira de vuelta
  al i) y el **frigio dominante** es su 5.º modo: ♭2 con 3.ª mayor, el sonido árabe / flamenco.
- **Acordes = terceras apiladas.** 1–3–5 es la tríada; sumando otra 3.ª, 1–3–5–7 (séptima); otra más, la 9.ª; luego
  11.ª y 13.ª. Los **sus** cambian la 3.ª por la 2.ª o la 4.ª (sin 3.ª no es ni mayor ni menor: ambiguo, abierto); los
  **add** suman una nota sin la 7.ª (add9 = color sin sonar a jazz); **6** suma la 6.ª. En el reggaetón y el R&B, m7, m9
  y maj7 son el pan de cada día; en el trap y el perreo oscuro, menores a secas y el ♭2 frigio.
- **Inversiones.** Las mismas notas con otra en el bajo: C/E (1.ª inversión, la 3.ª abajo) suena más ligero, "en
  camino"; C/G (2.ª, la 5.ª abajo) es inestable. El bajo cambia hasta el **nombre**: C E G A con Do abajo es **C6**; con
  La abajo es **Am7**. La fila ACORDE lo muestra en "también:".
- **Grados en números romanos.** Describen el acorde por su posición en la tonalidad: mayúscula = mayor, minúscula =
  menor, ° = disminuido, ø7 = semidisminuido, + = aumentado. **i–♭VI–♭III–♭VII** es la misma progresión en La menor
  (Am F C G) que en Do♯ menor (C♯m A E B): aprender los grados es aprender todas las tonalidades a la vez. La ♭ se mide
  contra la escala **mayor** de la tónica: en menor, el acorde de la 6.ª nota es ♭VI.
- **Funciones armónicas.** **Tónica** (casa: I, vi, iii, ♭III), **subdominante** (salir de casa: ii, IV, ♭VI, ♭II) y
  **dominante** (tensión que pide volver: V, vii°, ♭VII). Una progresión cuenta una historia T → S → D → T. ♭VII–i es
  la "cadencia modal": vuelve a casa sin sensible, más suave que V–i, y es de las que más se oyen en el pop latino.
- **Préstamo modal.** Usar en Do mayor un acorde de Do menor (u otro modo con la misma tónica): **♭VI** (A♭), **♭VII**
  (B♭) y **iv** (Fm) son los clásicos. Suenan épicos o melancólicos porque traen notas oscuras (A♭, E♭, B♭) a un
  contexto brillante. El motor dice de qué modos viene cada acorde (A♭ en Do: frigio, eólico, locrio, menor armónica).

### Qué muestra la fila ACORDE

- **Nombre** del acorde que suena en el instrumento (lo que salió de Tidechart), con ♭ y ♯ bien escritos (E♭, no D♯,
  cuando el acorde lo pide). Si hay un bajo distinto de la fundamental: C/E.
- **Inversión** y qué tono está en el bajo ("1.ª inversión (bajo: 3)"), o "sin 5.ª" si la omitiste.
- **Cada nota** con su función dentro del acorde: E♭5 (♭3), B♭5 (♭7), D6 (9)...
- **También:** otras formas de llamar a las mismas notas.
- Con **dos notas**, el intervalo en palabras (3.ª menor, tritono...). Con una, su nombre.

### Ejercicio guiado: construir y nombrar en FL

Con el ruteo de la Fase 1 (Tidechart → puerto 1 → Undertow o FLEX con un piano o un pad):

1. En el Piano Roll de Tidechart escribe **C5 E5 G5** a la vez (un acorde de 1 compás) y dale play en loop: ACORDE dice
   **C**, estado fundamental, C5 (1) E5 (3) G5 (5).
2. Baja **E5 a D#5**: ACORDE dice **Cm** y escribe la nota como **E♭5 (♭3)**, no D♯, aunque el Piano Roll diga D#.
   Escucha: una sola tecla un semitono abajo y todo se oscurece.
3. Agrega **A#5** (B♭): **Cm7**. Agrega **D6**: **Cm9**. Quita G5: sigue siendo Cm9, "sin 5.ª". Mira cómo cada nota
   nueva aparece con su número.
4. Ahora C5 E5 G5 **A5**: **C6**. Baja el A5 una octava (A4) para que quede en el bajo: **Am7**, y en "también:"
   aparece C6/A. Mismas notas, otro acorde: lo decide el bajo.
5. Inversiones: escribe **E5 G5 C6** (C/E) y luego **G4 C5 E5** (C/G). Escucha cuál descansa y cuál quiere ir a otro
   lado.
6. Modos: escribe **E4 + F4** (el ♭2 de Mi frigio) y luego E4 + F#4. La fila dice "intervalo de 2.ª menor" y "2.ª
   mayor". La 2.ª menor sobre la tónica es todo el carácter del frigio.

### Receta: la progresión del reggaetón, por grados

Escribe en La menor, un acorde por compás, en posición cerrada desde A4: **Am – F – C – G** (i–♭VI–♭III–♭VII). Mira
la fila ACORDE en cada compás. Por qué funciona: i (tónica) → ♭VI (subdominante, el más emocional: trae la ♭6, Fa) →
♭III (tónica relativa, alivio) → ♭VII (dominante modal que vuelve a i sin cerrar del todo: por eso el loop nunca
termina y aguanta 3 minutos repitiéndose). Cambia **G** por **E** (V, de la menor armónica, con Sol♯): el final tira
mucho más fuerte hacia Am. Más dramático, menos "loop".

### Reto (sin receta)

Pasa la idea a **Mi frigio** (grados i ♭II ♭III iv v° ♭VI ♭vii) y escribe una progresión de **2 acordes** que use el
♭II y suene oscura (el dembow llega en la Fase 4; mientras, ponlo con el FPC). Pistas: 1) el ♭II de Mi es Fa mayor.
2) Prueba Em – F y luego Em – Fmaj7. 3) Para aún más tensión, Em con el ♭2 encima: E G B D F (la fila la llama Em7♭9).

### Análisis de canciones

- **"Despacito"** (Luis Fonsi y Daddy Yankee): el loop **Bm – G – D – A**. Leído en Si menor es **i–♭VI–♭III–♭VII**,
  la receta de arriba; leído en Re mayor es vi–IV–I–V. Es la misma progresión vista desde dos tónicas: lo que decide
  cuál es "la casa" es dónde descansan la melodía y el bajo. El pop y el reggaetón comparten esqueleto; la diferencia
  está en el modo que se siente y en el ritmo.
- **Cadencia andaluza** (i–♭VII–♭VI–V; en La menor, Am G F E): la de "Hit the Road Jack" y de medio flamenco. El bajo
  baja por grados hasta un V mayor (con el Sol♯ de la armónica). En un puente, esa caída es inconfundible.
- Ejercicio: elige una canción de reggaetón o trap latino que te guste, toca sus acordes en el Piano Roll de Tidechart
  (de oído o con una transcripción) y apunta qué nombres e inversiones salen en la fila ACORDE. ¿Usa séptimas? ¿Algún
  acorde fuera de la escala (préstamo)?

### Vocabulario

- **Clase de altura (pitch class)**: la nota sin la octava (todas las Do).
- **Enarmónico**: dos nombres para la misma tecla (D♯ = E♭). En "Despacito" (Si menor) la 5.ª de Si se escribe F♯.
- **Intervalo**: la distancia entre dos notas; **tritono**: 6 semitonos, el más tenso.
- **Modo**: una escala con una nota característica (el ♭2 del frigio, el ♯4 del lidio).
- **Tríada / séptima / extensiones (9, 11, 13)**: acordes de 3, 4 y más notas apiladas por terceras.
- **sus / add**: cambiar la 3.ª por la 2.ª o la 4.ª / añadir una nota sin la 7.ª.
- **Inversión**: el acorde con otro tono en el bajo; **slash chord**: C/E, o con un bajo ajeno, F/G.
- **Grado / número romano**: el acorde por su posición en la tonalidad (♭VI, V7, iiø7).
- **Tónica, subdominante, dominante**: reposo, alejamiento, tensión.
- **Préstamo modal (modal interchange)**: un acorde de otro modo con la misma tónica (♭VI y ♭VII en mayor).
- **Sensible (leading tone)**: la 7.ª a un semitono de la tónica (Sol♯ en La menor armónica), la que tira hacia casa.

### Errores comunes

- Fiarse del nombre del Piano Roll: FL siempre dice sostenidos (D#5). En Do menor esa nota es E♭: la fila ACORDE da el
  nombre musical.
- Pensar que el acorde depende solo de las notas: el **bajo** lo cambia (C6 / Am7). Si la fila dice algo inesperado,
  mira qué nota está más abajo.
- Llenar todo de 9.ª, 11.ª y 13.ª: en el reggaetón el ritmo necesita espacio. Las extensiones lucen en pads y keys
  suaves; para un pluck que lleva el dembow, tríadas o m7 suelen funcionar mejor.
