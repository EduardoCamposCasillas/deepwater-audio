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

## Fase 3 — La paleta: un acorde por tecla

Tidechart deja de solo escuchar: ahora **toca**. Eliges una tonalidad y un modo, la **PALETA** muestra los 7 acordes
que salen de esa escala, y cada tecla que le llega (del Piano Roll o de un teclado) dispara un acorde entero. Se
compone con una sola línea de notas, una por acorde, y cambiar de tonalidad o de modo cambia todos los acordes a la vez.

### Conceptos aprendidos

- **Acordes diatónicos.** Apilar terceras *dentro de la escala*: desde cada nota, la nota que está 2 lugares más
  arriba en la escala y la que está 4 lugares más arriba (y 6 lugares, para la séptima). Sin salirte de las 7 notas
  aparecen solos los 7 acordes "de la casa". Unos salen mayores y otros menores porque la escala no es pareja: sus
  terceras tienen 3 o 4 semitonos según dónde caigan. En La menor: **i ii° ♭III iv v ♭VI ♭VII** (Am Bdim C Dm Em F G).
  En Do mayor: I ii iii IV V vi vii° (C Dm Em F G Am Bdim). Son **las mismas 7 notas**: La menor es la relativa de Do
  mayor, solo cambia dónde está la casa.
- **El modo cambia la paleta entera.** Con la tónica en La, pasar de menor a **dórico** cambia una nota (F → F♯), y
  con ella tres acordes: el iv (Dm) pasa a **IV (D)**, el ♭VI (F) a **vi° (F♯dim)** y el ii° (Bdim) a **ii (Bm)**. A
  **frigio**: el ii° pasa a **♭II (B♭)**, el acorde más oscuro del menor. A **menor armónica**: el v (Em) pasa a
  **V (E)**, la dominante con sensible. Probarlo en la paleta es la forma más rápida de oír qué hace cada modo.
- **Grados = transportar gratis.** Con "Teclas blancas = grados", la tecla C es siempre el I (o la i), D el ii... en
  **cualquier** tonalidad. Una progresión escrita como C–A–E–B (i–♭VI–♭III–♭VII) suena en La menor como Am F C G. Si
  cambias la tónica a Re, suena Dm B♭ F C sin mover una nota del Piano Roll. Es pensar en grados, como un músico de
  sesión: "es un uno-seis-tres-siete en Re".
- **Funciones con color.** Cada acorde lleva una franja: **verde agua = tónica** (reposo), **lavanda = subdominante**
  (salir de casa), **naranja = dominante** (tensión que pide volver). Una progresión que funciona suele recorrer los
  tres colores. Una que se queda en uno (solo tónicas) se siente estática, y eso a veces es justo lo que se busca en un
  loop de reggaetón.
- **Tríadas y séptimas.** La séptima diatónica le da a cada grado su color de 4 notas: en menor, **i m7**, **♭III maj7**,
  **iv m7**, **♭VI maj7**, **♭VII 7**. Ese es el sonido del R&B y del reggaetón "romántico": el mismo esqueleto con más
  carne.
- **Voicing de partida.** La fundamental va en la tecla y las demás notas encima, lo más juntas posible (*posición
  cerrada*). Con "+ Bajo" se suma la fundamental una octava abajo: el acorde suena más lleno y el bajo deja claro cuál
  es. Elegir las inversiones para que las voces se muevan poco entre un acorde y el siguiente es la Fase 4.

### Qué hace cada control

- **Tónica** (A, B♭, F♯...): la nota "de casa". La escritura (B♭ o A♯) la decide sola, según el modo, con las menos
  alteraciones posibles.
- **Modo**: qué notas tiene la escala. Debajo de los acordes se ven las 7 notas y el carácter del modo en una frase.
- **Tríadas / Séptimas**: acordes de 3 o 4 notas.
- **Teclas blancas = grados**: C = I, D = ii, E = iii, F = IV, G = V, A = vi, B = vii (las mayúsculas y minúsculas
  dependen del modo). Las negras no suenan. La octava de la tecla decide la octava del acorde (C5 dispara la tónica cerca
  de C5).
- **Notas de la escala**: cada nota de la escala dispara su acorde con la fundamental en esa misma tecla (en La menor,
  A = Am, C = C). Las notas que están fuera de la escala no suenan. Sirve para tocar "a ojo" sobre un teclado.
- **+ Bajo**: la fundamental una octava abajo.
- **Los 7 pads**: grado en números romanos, nombre del acorde, función y la tecla que lo dispara. **Mantén pulsado** un
  pad para escuchar el acorde (sale por el puerto MIDI, igual que las teclas). **Arrastra** de un pad a otro para
  encadenarlos. Se enciende el que suena, venga del ratón o del Piano Roll.
- **ACORDE** (abajo) ahora conoce la tonalidad: dice el grado y su función ("♭VI (subdominante)"), escribe las notas
  como en la escala y, si tocas algo fuera de ella en bypass, dice de qué modo viene ("prestado de frigio...").
- **Bypass**: las teclas vuelven a pasar tal cual (la Fase 1). Activarlo o quitarlo apaga lo que sonaba.

### Ejercicio guiado: componer con una línea

Con el ruteo de siempre (Tidechart → puerto 1 → un pad o un piano en Undertow o FLEX):

1. Tidechart en **A, menor (eólico), Tríadas, Teclas blancas**. Pasa el ratón por los pads manteniendo pulsado: escucha
   el i (casa), el iv (melancólico), el v (débil, sin sensible) y el ♭VI (el más emotivo).
2. En el Piano Roll de Tidechart escribe **C5 – A5 – E5 – B5**, una nota por compás, en loop: suena **Am – F – C – G**.
   Mira cómo se encienden los pads y lo que dice ACORDE en cada compás.
3. Sin tocar el Piano Roll, cambia la tónica a **D**: Dm – B♭ – F – C. Después a **F♯**: F♯m – D – A – E. Es la misma
   canción en otra tonalidad, a la altura que pida la voz.
4. Vuelve a La y cambia el modo a **dórico**: el ♭VI (A5) se vuelve **F♯dim**. Ese acorde suena raro en este loop:
   cada modo tiene progresiones que le quedan bien y otras que no.
5. Cambia a **menor armónica**: G5 dispara **E** (V) en vez de Em. Escribe C5 – G5 en loop (i–V): la vuelta a casa
   tira mucho más fuerte.
6. Pasa a **Séptimas** y activa **+ Bajo**: el mismo loop suena a R&B. Compara con tríadas a volumen parecido.

### Receta: perreo oscuro en dos acordes

**E, frigio, Tríadas, + Bajo**. En el Piano Roll, solo dos teclas: **C5** (i = Em) y **D5** (♭II = F). Escribe el
golpe del dembow con las notas, en **3 + 3 + 2**: cuenta 8 semicorcheas por medio compás (dos tiempos) y pon un golpe
corto (una semicorchea) en la **1.ª, la 4.ª y la 7.ª**. Son 6 golpes por compás. Dos compases de Em y dos de F. Por qué funciona: el ♭II está a un semitono de la tónica, así que
el bajo (E → F → E) se mueve lo mínimo y crea la máxima tensión. El ritmo repetido sostiene la canción aunque solo
haya dos acordes. Para más oscuridad, pon el F solo en el último golpe del 4.º compás: el oído espera volver a Em y
el ♭II aparece como una sombra. Con una tecla por golpe, escribir el ritmo de los acordes es escribir una sola línea
de notas.

### Reto (sin receta)

Escribe **8 compases** en La menor con teclas blancas que cuenten una historia T → S → D → T (mira los colores) y
terminen de forma que pidan volver al principio. Después escucha el mismo Piano Roll en **dórico**, **frigio** y
**menor armónica**, y anota qué acorde cambió en cada caso y si mejoró o empeoró la progresión.
Pistas: 1) una historia mínima es i – iv – v – i (C F G C). 2) Para que el final pida volver, termina en un acorde de
dominante (naranja), no en la i. 3) En menor armónica, el V del compás 8 es el que más tira hacia el compás 1.

### Teoría aplicada: por qué la paleta de menor no es "la de mayor, pero triste"

La menor y Do mayor tienen las mismas 7 notas y los mismos 7 acordes. Lo que cambia es **cuál es la casa**: en La
menor, Am es el reposo y C es un "alivio" (♭III, tónica relativa); en Do mayor, C es el reposo y Am es la "melancolía"
(vi). Por eso "Despacito" se puede leer de las dos formas (Fase 2). El bajo, el primer y el último acorde del loop y
dónde descansa la melodía deciden la casa. Si quieres que suene menor, empieza y termina en la i, y ponla en los
tiempos fuertes.

### Análisis de canciones

- **"Oye como va"** (Tito Puente, popularizada por Santana): todo el tema es un vaivén entre **Am7 y D9**. En la paleta
  es **A dórico**, i – IV (C5 y F5 con séptimas). El IV **mayor** dentro de un tono menor es la marca del dórico: el
  F♯ le da la luz que el eólico (Dm, iv) no tiene. Pruébalo: mismo Piano Roll en eólico y en dórico.
- **i – ♭VI – ♭III – ♭VII** (C A E B en teclas blancas): el loop de la Fase 2, ahora con una nota por acorde. Es la
  base de cientos de temas de reggaetón y pop latino. Escucha cómo el ♭VII (naranja) empuja de vuelta a la i sin
  sensible: cierra "a medias", por eso aguanta el loop.
- Busca un tema de perreo oscuro que te guste, encuentra su tónica (la nota a la que todo vuelve) y prueba en la
  paleta eólico contra frigio: si en algún momento aparece un acorde un semitono por encima de la tónica, es frigio.

### Vocabulario

- **Acorde diatónico**: el que se arma solo con notas de la escala (los 7 de la paleta).
- **Grado**: la posición del acorde en la escala (i, ♭VI...), independiente de la tonalidad.
- **Relativa**: la tonalidad mayor y la menor con las mismas notas (Do mayor ↔ La menor, Fa mayor ↔ Re menor).
- **Transportar**: llevar una canción a otra tonalidad sin cambiar los grados (paso 3 del ejercicio).
- **Posición cerrada**: las notas del acorde lo más juntas posible encima de la fundamental.
- **Disparo (trigger)**: una nota que no suena ella misma sino que hace sonar otra cosa (aquí, un acorde).
- **Vamp**: un vaivén de 1 o 2 acordes que se repite toda la sección ("Oye como va", la receta del frigio).

### Errores comunes

- **No suena nada con las negras.** Con "Teclas blancas = grados" las negras no disparan nada, y con "Notas de la
  escala" tampoco las notas que no están en la escala. Es a propósito: así nunca se cuela un acorde fuera de la tonalidad.
- **Cambiar la tonalidad con una nota sostenida y no oír el cambio.** El acorde que ya suena se queda como está hasta
  que se suelta la tecla, y el siguiente ya sale en la tonalidad nueva. Así cambiar un parámetro nunca deja notas
  colgadas.
- **Notas que se solapan en el Piano Roll.** Si una nota empieza antes de que termine la anterior, los dos acordes
  suenan juntos un instante. Las notas que comparten se vuelven a atacar, una sola vez. Para un cambio limpio, que cada
  nota termine donde empieza la siguiente.
- **Demasiado grave con "+ Bajo".** En la octava 4 o más abajo, el bajo extra se mete con el 808. Usa "+ Bajo" en pads y
  keys solos, o sube el acorde una octava.
- **Pensar que el modo es solo "más triste" o "más alegre".** Cada modo tiene progresiones propias: el ♭II es del
  frigio, el IV mayor en menor es del dórico, el V mayor en menor es de la armónica. La paleta enseña cuáles son.

## Fase 4 — La progresión: voces que caminan, el dembow y acordes prestados

Hasta aquí cada acorde sonaba solo, en estado fundamental, mientras durara la tecla. Ahora Tidechart **compone en el
tiempo**: una **progresión** de hasta 8 pasos que sigue a FL compás a compás, con un **ritmo** de acordes (el dembow y
sus variantes) y un **voicing** que mueve las voces lo menos posible. La paleta suma una fila de **acordes prestados**
de otros modos, y cada clic puede ir armando la progresión.

### Conceptos aprendidos

- **Voicing.** El acorde dice *qué* notas suenan; el voicing dice *en qué octava y en qué orden*. Am puede ser A C E,
  C E A, E A C, abierto en dos octavas... Todas son "Am", pero suenan distinto: más grave o más brillante, más junto o
  más ancho.
- **Conducción de voces.** Piensa cada nota del acorde como una voz de un coro. En estado fundamental, de Am a F cada
  voz salta (A→F, C→A, E→C). Con conducción de voces se busca, de todas las inversiones, la que obliga a moverse
  **lo menos posible**: Am (A C E) → F como **A C F**. A y C se quedan quietas (son **notas comunes** de los dos
  acordes) y solo E sube un semitono a F. La progresión del reggaetón queda así: **A C E → A C F → G C E → G B D**,
  12 semitonos de movimiento en total contra más del doble en estado fundamental. Suena más suave, más "producido",
  y deja espacio a la melodía.
- **Inversiones automáticas.** Las inversiones no se eligen a mano: salen solas de buscar el camino corto. F como
  A C F es la **1.ª inversión** (la 3.ª abajo: F/A, lo verás en la fila ACORDE); C como G C E es la **2.ª** (C/G). En
  Do mayor, el clásico I–IV–V–I sale como C E G → C F A → B D G → C E G: cada voz se mueve 2 semitonos como mucho.
- **Abierto (drop 2).** Se toma el voicing cerrado y la 2.ª voz desde arriba baja una octava: C E G → E3 C G. Las
  mismas notas, más separadas: más ancho, de pad o de piano de balada. En graves, en cambio, se embarra: úsalo en
  registros medios.
- **Ritmo de acordes.** Un patrón son golpes sobre 16 semicorcheas por compás. El **dembow** es **3+3+2**: en cada
  medio compás, golpes en las semicorcheas 1, 4 y 7 (contando desde 1): *boom - - ch - - boom - | boom - - ch - -
  boom -*. Es el **tresillo** de la habanera, que viene del Caribe y está en el reggae, el dancehall y el reggaetón.
  Sus variantes: **largo** (el 3+3+2 estirado a todo el compás, en corcheas: los acordes respiran), **a contratiempo**
  (solo el "ch": los acordes contestan al bombo en vez de pisarlo), **desplazado** (movido una corchea: mismo dibujo,
  otro apoyo) y **con huecos** (deja aire para la voz).
- **Acento.** Cada golpe tiene su fuerza (un % de la velocidad de la tecla): en el dembow, el primero de cada mitad
  pega más. Un instrumento que responde a la velocidad (un piano, un pluck con filtro por velocidad) hace que el patrón
  "baile" en vez de sonar a máquina.
- **La progresión como una pista más.** El paso que suena depende solo de **dónde está la canción**: la progresión se
  cuenta desde el compás 1 y se repite. Si saltas al compás 7 o haces loop de una sección, suena el acorde de ese
  compás, igual que el resto de pistas. Con FL parado no hay ritmo: el acorde suena sostenido para que lo escuches.
- **Préstamo modal en la práctica.** La fila **PRESTADOS** muestra los acordes de los modos con la misma tónica que no
  están en tu escala, ordenados por lo que más se usa. En **La menor**: **ii (Bm)** y **IV (D)** del dórico, **♭II
  (B♭)** del frigio, **V (E)** de la menor armónica. En **Do mayor**: **iv (Fm)**, **♭VI (A♭)**, **♭VII (B♭)** del
  menor. Un prestado trae una nota de fuera (el F♯ del IV dórico, el B♭ del ♭II frigio): por eso "colorea".
- **Mediante cromática.** Dos acordes mayores (o menores) cuyas fundamentales están a una 3.ª, con notas cambiadas
  que no son de la escala: I–III en mayor (C–E). Ni del modo ni prestados del modo paralelo: Tidechart los marca como
  "cromático". Suenan a cine, a sorpresa.

### Qué hace cada control

- **Teclas / Progresión.** *Teclas*: cada tecla dispara su acorde, como en la Fase 3 (ahora con ritmo y voicing).
  *Progresión*: **cualquier tecla abajo** hace sonar la progresión siguiendo a FL. La octava de la tecla elige el
  registro (C5 = el de la paleta; C4, una octava abajo) y su velocidad, la fuerza. En el Piano Roll basta **una nota
  larga** donde quieras acordes.
- **Ritmo.** Sostenido (el acorde dura su paso), negras, corcheas, contratiempo y las 5 variantes del dembow. La tira
  RITMO dibuja el patrón (más claro = más acento) y marca la semicorchea en la que va FL.
- **Voicing.** Estado fundamental (la Fase 3: cada acorde salta), conducción de voces, abierto.
- **Plantillas.** Progresiones para empezar, cada una con su modo y una frase de por qué funciona.
- **El texto romano.** La progresión escrita: *i ♭VI ♭III ♭VII*. Se puede escribir y pulsar Enter: `i bVI bIII bVII`,
  `i bII`, `Imaj7 IV7 iv9`. Minúscula = menor, `b` = bemol, `o` = disminuido, `+` = aumentado, y el tipo detrás (7, maj7,
  9, sus2...).
- **Agregar con clic.** Encendido, cada clic en un pad de la paleta (también en los prestados) lo agrega al final.
- **Los pasos.** Grado, acorde, función (color), tiempos y, si es prestado, de qué modo. Mantener pulsado = escuchar.
  **Clic derecho**: duración (1 a 16 tiempos), tipo de acorde (sus2, add9, maj7, m9...), mover, duplicar, quitar.
  **Rueda**: un tiempo más o menos. El paso donde está FL se enciende.
- **PRESTADOS.** Mantener pulsado = escuchar; pasa el ratón para leer de dónde viene.

### Ejercicio guiado: del bloque al coro

Ruteo de siempre (Tidechart → puerto 1 → un piano o un pad en Undertow o FLEX). Tidechart en **A, menor (eólico),
Tríadas**.

1. En PROGRESIÓN elige **Progresión (sigue a FL)**, ritmo **Sostenido**, voicing **Estado fundamental**. En el Piano
   Roll de Tidechart escribe **una sola nota C5 de 4 compases**, y loop de 4 compases. Play: Am – F – C – G, un compás
   cada uno. Mira el teclado de SUENA AHORA: los bloques saltan de un lado a otro.
2. Cambia a **Conducción de voces** (puedes hacerlo con FL sonando). Escucha: los acordes ahora "se deslizan". Mira el
   teclado: las teclas encendidas casi no se mueven. En ACORDE aparecen **F/A** y **C/G**: las inversiones que eligió.
3. **Abierto**: más ancho. Compara los tres a volumen parecido con el mismo sonido. ¿Cuál deja más espacio para una
   voz?
4. Ritmo **Dembow (3+3+2)**. Pon un bombo en el FPC en las semicorcheas 1 y 9 de cada compás y una caja en 4, 7, 12 y
   15. Los acordes quedan cosidos a la batería. Prueba **Dembow a contratiempo**: los acordes caen solo en los huecos
   del bombo (llamada y respuesta).
5. Mueve la nota del Piano Roll a **C4**: la progresión entera baja una octava. A **C6**, sube.
6. Haz loop solo del compás 3: suena siempre **C** (el ♭III), porque la progresión sigue la posición de la canción.

### Receta: perreo oscuro con un prestado

**E, frigio**, Plantilla **Perreo oscuro (i–♭II)**, voicing **Conducción de voces**, **+ Bajo**, ritmo **Dembow
largo**. Una nota C5 larga en el Piano Roll. Em (E G B) y F (F A C) se alternan y cada voz sube un semitono o un tono y
vuelve; el bajo sube y baja un semitono: tensión pura. Ahora el giro: enciende **Agregar con clic**, toca el pad de la
**i** (Em) y después, en PRESTADOS, el **V (B)**, de la menor armónica. Progresión: i – ♭II – i – V.
Por qué funciona: el ♭II tira hacia abajo (F → E, un semitono) y el V tira hacia arriba (D♯ → E, la sensible): la
tónica queda apretada entre dos semitonos, el sonido del perreo más oscuro y del flamenco. Con el dembow largo los
acordes respiran y dejan el centro para la voz.

### Reto (sin receta)

Arma **a mano, con Agregar con clic**, una progresión de 4 pasos en **Do mayor** que use **dos prestados** distintos
y termine pidiendo volver al principio. Ponle el ritmo y el voicing que mejor le queden. Antes, escríbela en papel en
números romanos. Pistas: 1) el ♭VI (A♭) y el ♭VII (B♭) del menor son la pareja épica: ♭VI – ♭VII – I cierra como en
una película. 2) El iv (Fm) justo antes del I es la "cadencia plagal menor": nostálgica. 3) Si el último paso es un
acorde naranja (dominante), el loop pide volver.

### Teoría aplicada: por qué la conducción de voces suena "mejor"

El oído sigue **líneas**, no bloques. Cuando las voces se mueven poco, cada una forma una pequeña melodía (A → A → G →
G en la voz de abajo de la progresión del reggaetón; E → F → E → D arriba) y la armonía se oye como un movimiento
continuo. Las notas comunes hacen de pegamento entre acordes. Cuando todo salta, el oído oye golpes separados. Por eso
los pianistas y los arreglistas tocan así casi siempre, y por eso el **estado fundamental** se reserva para cuando se
quiere fuerza bruta (power chords, un piano martillado). Una consecuencia práctica: con conducción de voces, la nota
más aguda del acorde se mueve poco, y una melodía que va por encima tiene su espacio.

### Análisis de canciones

- **"Dem Bow"** (Shabba Ranks, 1990): de su ritmo (producido por Steely & Clevie) sale el nombre del patrón. Escucha la
  caja en el 3+3+2 y compárala con la tira RITMO en **Dembow**. El reggaetón tomó ese ritmo y lo hizo base de todo un
  género. El dembow dominicano lo acelera y lo hace más seco.
- **"Despacito"**: Bm – G – D – A en loop, i–♭VI–♭III–♭VII en Si menor. Pon la plantilla **Reggaetón** con tónica **B**,
  conducción de voces y **Dembow largo**: es el esqueleto armónico y rítmico. Luego cambia a **Sostenido**: sin el
  ritmo, la misma armonía suena a balada.
- **"Creep"** (Radiohead): G – B – C – Cm, **I – III – IV – iv** (la plantilla **Mediante**). El B mayor (III) es una
  mediante cromática con un D♯ fuera de la escala; el Cm (iv) es un prestado del menor. Dos colores "de fuera" en
  cuatro acordes: de ahí la sensación rara y triste.
- Ejercicio: toma un tema de reggaetón que te guste, saca su tónica y sus acordes (de oído o con una transcripción) y
  escríbelos en el texto romano. ¿Cuáles salen como prestados? ¿Qué ritmo de la lista se parece más al de sus acordes?

### Vocabulario

- **Voicing**: cómo se reparten las notas de un acorde (orden y octavas).
- **Conducción de voces (voice leading)**: pasar de un acorde a otro moviendo cada voz lo menos posible.
- **Notas comunes**: las que comparten dos acordes seguidos (Am y F comparten A y C).
- **Posición cerrada / abierta**: las voces lo más juntas posible / repartidas en más de una octava.
- **Drop 2**: bajar una octava la 2.ª voz desde arriba de un voicing cerrado (muy usado en piano y guitarra de jazz).
- **Tresillo / 3+3+2**: el patrón de 8 semicorcheas agrupadas en 3, 3 y 2, la base del dembow.
- **Contratiempo**: los golpes en las partes débiles (el "y" de cada tiempo).
- **Anticipación**: un golpe justo antes del tiempo fuerte que adelanta lo que viene (el último golpe de "Dembow con
  huecos").
- **Préstamo modal / acorde prestado**: un acorde de otro modo con la misma tónica.
- **Mediante cromática**: dos acordes con las fundamentales a una 3.ª y notas fuera de la escala (I – III mayor).

### Errores comunes

- **"No suena nada" en modo Progresión.** Hace falta una tecla abajo: una nota larga en el Piano Roll de Tidechart (o
  mantener una tecla). Sin nota, la progresión avanza en silencio (lo ves en el paso encendido).
- **"Suena otro acorde del que esperaba".** La progresión se cuenta desde el compás 1 de la canción. Si tu sección
  empieza en el compás 6 y la progresión dura 4 compases, el compás 6 es su paso 2. Haz que las secciones empiecen en
  múltiplos del largo de la progresión, o ajusta las duraciones.
- **Dembow que no cuadra con la batería.** Los patrones son de 4/4, con 16 semicorcheas por compás. En otro compás no
  van a cuadrar. Y si el swing de FL está activo, la batería se corre y los acordes no.
- **Usar el estado fundamental en un pad largo.** Cada cambio salta y el pad "late". Para pads y keys, conducción de
  voces; el estado fundamental, para plucks con mucha pegada o cuando el bajo de la mano izquierda ya lo hace el 808.
- **Llenar la progresión de prestados.** Un prestado colorea porque es la excepción. Con tres en cuatro acordes ya no
  hay tonalidad que colorear: la canción suena perdida. Uno o dos por progresión, en el sitio donde quieres el efecto.
- **Abierto en graves.** Con la tecla en C3 o más abajo, el drop 2 deja una 3.ª grave que embarra con el bajo. Súbelo
  de octava o usa voicing cerrado.

## Fase 5 — Detectar la tonalidad: ¿dónde está la casa?

Hasta aquí tú le decías a Tidechart la tonalidad. Ahora puede **escucharla**: le das play a una melodía o a unos
acordes (o tocas en el teclado) y te dice en qué tonalidad y modo están, qué tan segura está, **por qué**, y qué otras
lecturas hay. Con **Usar** la pone en la paleta. Sirve para trabajar sobre algo que ya existe: un sample, una melodía
que se te ocurrió, una canción que quieres estudiar.

### Conceptos aprendidos

- **El histograma de notas.** Lo primero es contar cuánto suena cada una de las 12 notas (sin importar la octava),
  pesado por la **duración**: una nota larga cuenta más que una de paso. El bajo cuenta doble, porque suele decir la
  fundamental. Es la fila de barras de TONALIDAD.
- **El perfil de una tonalidad.** En una tonalidad no suenan todas las notas igual: la **tónica** es la que más, después
  la **5.ª** y la **3.ª** (la tríada de tónica), después el resto de la escala, y lo que está fuera casi nada. Ese dibujo
  es el "perfil". Lo midieron Krumhansl y Kessler (1982) preguntando a oyentes cuánto "encajaba" cada nota después de
  escuchar una tonalidad; Tidechart usa sus valores medios por papel (tónica, 5.ª, 3.ª, escala, fuera), así sirve para
  cualquiera de los 10 modos.
- **Correlación.** Para cada una de las 120 tonalidades (12 tónicas × 10 modos) se mide cuánto se parece tu histograma a
  su perfil: ¿suben y bajan juntos? Es la **correlación de Pearson** (1 = mismo dibujo, 0 = nada que ver). Gana la que
  más se parece. Es el método de Krumhansl y Schmuckler, el clásico de la detección de tonalidad.
- **El problema de las relativas.** **La menor y Do mayor tienen las mismas 7 notas.** Peor: en i–♭VI–♭III–♭VII (Am F C
  G) el **Do suena más que el La** (está en Am, en F y en C). Con solo el histograma, la progresión del reggaetón sale
  como Do mayor (vi–IV–I–V). Lo que hace que la oigas en La menor no es *qué* notas suenan sino **dónde se apoya** la
  música: empieza y vuelve a Am.
- **El voto de "casa".** Por eso Tidechart escucha además **la nota más grave al empezar cada frase** (cada 8 compases
  desde el compás 1, y en cada vuelta del loop de FL). Cada frase es un voto para esa nota; si los votos caen en la
  tónica de una candidata, esta suma puntos. Con FL parado (tocando en vivo), vota la nota más grave de lo primero que
  tocas. Son los puntos naranjas sobre las barras.
- **Misma tónica, otro modo.** Lo que suele quedar segundo es el modo vecino con la misma tónica: La menor contra La
  frigio, que se diferencian en **una sola nota** (Si contra Si♭). Si esa nota sonó, decide; si nunca sonó, las dos
  son igual de posibles, y Tidechart te lo dice ("lo que las separa es B contra B♭").
- **Confianza.** *Clara*, *probable* o *dudosa* según cuánto le gana la primera a la segunda. Dudosa no quiere decir
  error: quiere decir que faltan notas para decidir, o que de verdad es ambigua (y ahí decides tú, a oído).

### Qué hace cada control

- **Escuchar.** Mientras está encendido, Tidechart **deja pasar el MIDI tal cual** (como en bypass: oyes lo que tocas,
  la paleta y la progresión no suenan) y acumula lo que entra. Cada vez que lo enciendes empieza de cero. Al apagarlo,
  el resultado se queda en pantalla y vuelven los acordes.
- **El resultado.** La tonalidad, su confianza y, debajo, el porqué: las 3 notas que más sonaron con su papel (tónica,
  ♭2, 5...) y qué la separa de la segunda opción (si es su relativa, o si es otro modo con la misma tónica).
- **Usar.** Pone la tónica y el modo en la paleta (y en la progresión, que está en grados: se transporta sola).
- **o también.** Las 3 siguientes. Clic = usar esa.
- **Las barras.** Cuánto sonó cada nota. En color las de la escala detectada; la tónica en turquesa; gris lo de fuera.
  El punto naranja: votos de "casa".

### Ejercicio guiado: que adivine la del reggaetón

1. Rutea Tidechart a un piano como siempre. En el Piano Roll **de Tidechart** escribe a mano **Am – F – C – G**, un
   compás cada uno (tríadas en C5 y el bajo en C4: A, F, C, G). Loop de 4 compases.
2. Pulsa **Escuchar** y dale play desde el compás 1. Oyes los acordes tal cual (Tidechart no los cambia). A los pocos
   segundos aparece **A menor (eólico)**, *clara*, con un punto naranja sobre el La. Lee la explicación: Do mayor tiene
   las mismas notas; decide el bajo al empezar cada frase.
3. Mueve el loop para que empiece en el **C** (C – G – Am – F) y vuelve a encender Escuchar (empieza de cero). Ahora
   sale **Do mayor**: las mismas notas, otra casa. Así de importante es dónde empieza la frase para el oído.
4. Cambia los acordes a **Am – Dm – E – Am** (el E mayor: E G♯ B) y vuelve a escuchar: sale **La menor armónica** (el G♯
   es la sensible). Mira las barras: el G♯ ahora está en la escala y el G natural no sonó.
5. Ahora solo **Am – B♭** alternados (i–♭II): sale **La frigio** (el B♭ es el ♭2). Una sola nota cambia el modo.
6. Pulsa **Usar** y apaga Escuchar: la paleta queda en La frigio y ya puedes tocar sus acordes.

### Receta: sacar la tonalidad de un sample

Tienes un loop de guitarra o de piano (audio) y quieres acompañarlo. 1) Escucha el sample y tararea las notas
largas; tócalas en el teclado o escríbelas en el Piano Roll de Tidechart (no hace falta el ritmo exacto: notas
largas). 2) La nota en la que "descansa" el sample, ponla al principio y en el grave. 3) Escuchar → mira el resultado y
las alternativas. 4) Prueba la primera con **Usar**: toca los pads de la paleta encima del sample. Si un acorde choca,
prueba la alternativa (casi siempre el mismo modo con la otra nota que los separa). Por qué funciona: el sample tiene un
"perfil" como cualquier música; las notas largas y el grave pesan más, igual que para el oído.

### Reto (sin receta)

Escribe una melodía de 8 compases (sin acordes) que Tidechart detecte como **Re dórico**, no como Re menor ni como Do
mayor. Antes, piensa en papel qué notas tiene que tener. Pistas: 1) Re dórico y Do mayor tienen las mismas notas: ¿qué
hace que la casa sea Re? 2) Re dórico y Re menor se diferencian en una nota: ¿cuál, y cuánto tiene que sonar? 3) Que la
melodía empiece en una nota grave en el compás 1 y vuelva a ella.

### Teoría aplicada: tónica y "centro tonal"

Las 7 notas de una escala no dicen la tonalidad por sí solas: son el **material**. La tonalidad es el material más un
**centro**: la nota a la que todo vuelve. Lo que crea el centro es la **jerarquía**: la tónica en los tiempos fuertes,
al principio y al final de las frases, en el bajo, y sus vecinas (5.ª y 3.ª) sosteniéndola. Por eso con las mismas
notas de Do mayor puedes hacer música en La menor, en Re dórico o en Mi frigio: cambia **dónde te apoyas**. Y por eso el
reggaetón en menor funciona tan bien con loops de 4 acordes: el loop vuelve a la i en cada vuelta, y ese regreso
constante fija la casa aunque el ♭III suene tanto como la i.

### Análisis de canciones

- **"Despacito"**: Bm – G – D – A. Las notas de Si menor son las de Re mayor; el ♭III (D) suena tanto como la i. ¿Por
  qué la oímos en Si menor? Empieza en Bm y vuelve a Bm cada 4 compases. Escribe los acordes en el Piano Roll y pídeselo
  a Tidechart; luego empieza el loop en D y compara.
- **"Tusa"** (Karol G y Nicki Minaj): Fm – D♭ – A♭ – E♭ (i–♭VI–♭III–♭VII en Fa menor). La misma progresión que
  "Despacito" en otra tónica: la detección cambia la tónica, pero los grados (y la explicación) son los mismos.
- **"Oye cómo va"** (Tito Puente / Santana): Am7 – D7, un vamp de dos acordes. El F♯ del D7 lo hace **dórico**, no menor.
  Tócalo y mira qué nota marca Tidechart como la diferencia con La menor.
- Ejercicio: elige un perreo oscuro que te guste, escribe 4 compases de su bajo y la melodía principal en el Piano Roll
  y pídele la tonalidad. ¿Es menor natural o frigio? ¿Qué nota lo decide?

### Vocabulario

- **Tonalidad / centro tonal**: la nota "casa" de una música y la escala que la rodea.
- **Relativas**: dos tonalidades con las mismas notas y distinta tónica (La menor y Do mayor; Mi frigio y Do mayor).
- **Paralelas**: misma tónica, distinto modo (La menor y La mayor; La menor y La frigio).
- **Perfil tonal**: cuánto se espera que suene cada nota en una tonalidad.
- **Histograma de notas**: cuánto sonó cada una de las 12 notas.
- **Correlación**: qué tanto dos listas de números suben y bajan juntas (de −1 a 1).
- **Frase**: un tramo musical que se siente completo (en el pop y el reggaetón, 4 u 8 compases).

### Errores comunes

- **Escuchar solo la melodía.** Una melodía sola tiene pocas notas y a veces se queda en 5 (pentatónica): es
  ambigua de verdad entre varias tonalidades. Si puedes, dale también el bajo o los acordes.
- **Empezar el loop en cualquier sitio.** El voto de "casa" se toma en el compás 1 de cada frase de 8 compases (y en el
  inicio de cada vuelta del loop). Si tu loop empieza a mitad de la progresión, la casa que oye Tidechart es otra.
- **Creerle a ciegas.** El resultado es una propuesta con su porqué. Si dice *dudosa*, o si la alternativa te suena
  mejor, manda tu oído: toca la tónica propuesta encima de la música; si suena a "final", es la casa.
- **Dejar Escuchar encendido.** Mientras escucha, Tidechart no hace acordes (el MIDI pasa tal cual). Si de pronto "no
  suenan los acordes", mira si dice ESCUCHANDO en SUENA AHORA.
- **Mezclar dos canciones.** Lo escuchado se acumula. Si cambias de sample o de sección (o la canción modula), apaga y
  vuelve a encender Escuchar para empezar de cero.
