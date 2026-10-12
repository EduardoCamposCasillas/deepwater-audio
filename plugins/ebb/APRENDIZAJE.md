# APRENDIZAJE — Ebb (ducker para sidechain)

Cuaderno de mezcla y diseño sonoro del ducker. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — El reloj: tempo, compás y dónde va la canción

Ebb todavía no le hace nada al sonido: la señal pasa intacta. Lo que aprende en esta fase es **saber en qué momento
musical está**, porque un ducker es, antes que nada, un reloj: tiene que bajar el volumen *justo* cuando entra el
kick. Si no sabe dónde está el kick, todo lo demás da igual.

### Conceptos aprendidos

- **Tempo (BPM).** Negras por minuto. Una negra dura `60 / BPM` segundos: a 95 BPM (tempo típico de reggaetón),
  631.6 ms. Todo lo rítmico de una mezcla (el ducking, el release de un compresor, el pre-delay de una reverb, un
  delay) se puede medir en esa unidad.
- **Compás (time signature).** Cuántos tiempos hay en un compás y qué figura es el tiempo. 4/4 = 4 negras (casi todo el
  reggaetón, el trap y el house); 3/4 = 3 negras (vals); 6/8 = 6 corcheas agrupadas de 3 en 3 (se siente "en 2", con
  balanceo); 7/8 = 7 corcheas (cojo, irregular: muy usado en música experimental).
- **Subdivisiones.** Negra = 1 tiempo en 4/4; corchea = 1/2; semicorchea = 1/4. Un compás de 4/4 tiene **16
  semicorcheas**: son los 16 pasos (*steps*) del Channel Rack de FL. El dembow se escribe en esa cuadrícula.
- **PPQ (posición en negras).** Cómo el host le dice al plugin dónde va la canción: "llevamos 37.5 negras desde el
  inicio". Con eso y el compás se saca todo lo demás: en 4/4, 37.5 negras = compás 10 (empieza en 36), tiempo 2,
  tercera semicorchea.
- **El PlayHead (transporte).** En cada bloque de audio (unos 5–20 ms) FL le pasa a Ebb el tempo, el compás, la
  posición y si está reproduciendo. Ebb no lleva su propia cuenta: **se vuelve a sincronizar con FL en cada bloque** y
  entre dos reportes calcula cada muestra con `posición = inicio + n · BPM / (60 · frecuencia de muestreo)`. Por eso no
  se puede "desfasar" de FL aunque cambies el tempo o hagas loop.
- **Saltos.** Cuando FL hace loop (vuelve al inicio del patrón) o haces clic en la línea de tiempo, la posición salta.
  Ebb lo detecta comparando dónde esperaba estar con dónde dice FL que está. En la Fase 2 eso sirve para que una curva
  de ducking a medias no se arrastre al otro lado del salto.
- **Reloj libre.** Sin host (el Standalone) no hay quién informe la posición: Ebb cuenta solo a 120 BPM desde 0, y lo
  avisa en ámbar. Con FL parado, la posición se queda quieta donde FL la deja.

### Qué hace cada control (y qué muestra la pantalla)

- **BYPASS**: escuchar la señal original. Hoy no cambia nada (la señal ya pasa intacta), pero ya hace el fundido de
  20 ms que hará falta cuando Ebb baje volúmenes: cortar de golpe de una señal atenuada a la original sería un clic.
- **Estado**: ▶ Reproduciendo (sigue a FL), ■ Parado, ○ Reloj libre.
- **Compás . tiempo . semicorchea** (grande): dónde va la canción, contando desde 1. Las **luces** son los tiempos del
  compás (la primera, más brillante, es el "1"): deberían encenderse a la vez que el metrónomo de FL.
- **FL (B:S)**: la misma posición en el formato del contador de FL, compás:paso (paso = semicorchea del compás, 1–16
  en 4/4). Pon el contador de FL en Bar:Step:Tick para compararlos tal cual.
- **Tempo, Compás, Posición** (en negras, con 3 decimales), **Loop** (si FL tiene el loop activo), **Saltos**
  (cuántas veces la posición saltó estando en reproducción) y **Audio** (frecuencia de muestreo y tamaño del bloque
  que manda FL: el bloque cambia según el buffer de tu tarjeta en Options → Audio settings).

### Ejercicio guiado: comprobar que Ebb sigue a FL

1. En un proyecto nuevo, pon un **loop de batería** (o el patrón de dembow de la receta de abajo) y abre Ebb en un
   insert del Mixer (Ebb aparece bajo el fabricante **Deepwater**). Activa el **metrónomo** de FL.
2. Reproduce en **modo Pattern**. Mira las luces de Ebb: la primera se enciende con el "clic" agudo del metrónomo, las
   otras con los graves. El número grande avanza como el contador de FL, y la fila
   **FL (B:S)** coincide con él (pon el contador de la barra de FL en Bar:Step:Tick).
3. Cada vez que el patrón vuelve a empezar, **Saltos** sube 1: eso es un loop visto desde dentro del plugin.
4. Cambia el **tempo** con la canción sonando (de 95 a 100 BPM, arrastrando el número del tempo): la lectura de
   Tempo cambia, la posición sigue coincidiendo y Saltos **no** sube. Un cambio de tempo no es un salto.
5. Pasa a **modo Song**, con la canción parada haz clic en distintos puntos de la línea de tiempo: Ebb dice "Parado" y
   la posición salta a donde hiciste clic. Reproduce y haz clic a mitad de la canción: Saltos sube 1.
6. Cambia el **compás** del proyecto a 3/4 o 7/8 (en Options → Project general settings, o con un marcador de compás en
   la Playlist, según tu versión de FL): las luces pasan a 3 o 7 y la cuenta de tiempos se reinicia en cada compás.
7. Abre el **Standalone** (`Ebb.exe`): estado en ámbar, "Reloj libre", 120 BPM contando solo. Eso es lo que hará Ebb
   cuando no haya nadie que le diga el tempo.

### Receta: medir en tiempo musical (tabla de tu tempo)

Para que un efecto "respire" con la canción, sus tiempos deben medirse en notas, no en milisegundos al azar. La
cuenta es siempre `ms = 60000 / BPM × negras`:

| Figura | Negras | 90 BPM | 95 BPM | 100 BPM |
|---|---|---|---|---|
| Compás (4/4) | 4 | 2667 ms | 2526 ms | 2400 ms |
| Negra (1/4) | 1 | 667 ms | 632 ms | 600 ms |
| Corchea (1/8) | 0.5 | 333 ms | 316 ms | 300 ms |
| Tresillo de corchea (1/8 T) | 1/3 | 222 ms | 211 ms | 200 ms |
| Semicorchea (1/16) | 0.25 | 167 ms | 158 ms | 150 ms |

Uso en mezcla hoy mismo, con cualquier compresor: si quieres que el bajo "vuelva" entre dos kicks que están a una
negra, el release del compresor de sidechain no debe pasar de esa negra (≈600 ms a 100 BPM); si dura más, el bajo nunca
termina de volver antes del siguiente golpe y la mezcla pierde graves. Ebb, desde la Fase 2, medirá su curva
directamente en estas figuras.

El **dembow clásico** en la cuadrícula de 16 pasos (4/4): kick en los pasos 1, 5, 9 y 13 (cada tiempo) y la caja en
los pasos **4, 7, 12 y 15**. En la notación de Ebb, la caja cae en 1.1.4, 1.2.3, 1.3.4 y 1.4.3. Ese "boom — ch-boom —
chick" con la caja adelantada es lo que da el balanceo.

### Reto sin receta

Programa un dembow **experimental** en el Channel Rack: deja la caja en su sitio (4, 7, 12, 15) pero mueve el kick para
que NO caiga en cada tiempo (por ejemplo, quita uno y adelanta otro una semicorchea). Con Ebb abierto y la canción a
94 BPM:

1. Para la canción justo en cada golpe del kick (o avanza paso a paso) y anota en qué posición de Ebb cae cada uno.
2. Calcula cuántos milisegundos hay entre cada par de kicks consecutivos.
3. Responde: ¿hay alguna división fija (1/4, 1/8, 1/16) que dispare un ducking exactamente en todos tus kicks y en
   ningún otro sitio? ¿Qué pasaría si usaras 1/16?

Inténtalo y después pide pistas.

### Vocabulario

- **BPM**: negras por minuto. *En música:* el reggaetón clásico va a 88–100 BPM; el house, a 120–128; el trap, a
  130–150 (aunque se siente a la mitad).
- **Compás / time signature**: cuántos tiempos por compás y de qué figura. *En música:* casi todo el pop es 4/4; "Money"
  de Pink Floyd está en 7/4.
- **Negra, corchea, semicorchea**: 1, 1/2 y 1/4 de tiempo en 4/4. *En música:* los hi-hats del trap van en
  semicorcheas (y en fusas en los "rolls").
- **Tresillo**: tres notas en el espacio de dos. *En música:* el "flow" en tresillos de Migos ("Versace") o los hi-hats
  en tresillo del trap.
- **Paso (step)**: cada semicorchea del Channel Rack; 16 por compás de 4/4.
- **PPQ**: posición en negras desde el inicio de la canción; lo que el host le pasa a los plugins para que se
  sincronicen.
- **Transporte / PlayHead**: el reporte de reproducción del host (play/stop, tempo, posición, loop).
- **Dembow**: el patrón de batería base del reggaetón, que viene del dancehall jamaiquino ("Dem Bow" de Shabba Ranks,
  1990) y del reggae en español panameño. *En música:* "Gasolina" de Daddy Yankee lo usa en estado puro.
- **Sidechain** (adelanto de la Fase 2): una señal controla lo que le pasa a otra. *En música:* el bombeo de los pads en
  "One More Time" de Daft Punk.

### Errores comunes

- **Proyecto a un tempo y el sample a otro.** Ebb sigue el tempo del **proyecto**, no el del audio. Si arrastras un loop
  de 98 BPM a un proyecto de 95 sin estirarlo, sus golpes se van alejando de la cuadrícula y el ducking por tempo (Fase
  2) caerá fuera de lugar. Para eso existirán los modos MIDI (Fase 4) y audio (Fase 5).
- **Kicks humanizados o con swing.** Si el kick está desplazado unos milisegundos de la cuadrícula (a propósito, por
  groove), una curva por tempo lo pisará siempre un poco antes o después. Lo mismo: modo audio.
- **Fiarse de la pantalla al milisegundo.** La GUI se actualiza 30 veces por segundo y el audio que oyes sale con la
  latencia del buffer de la tarjeta: la lectura y el sonido pueden ir unos milisegundos desfasados. El audio, en cambio,
  es exacto a la muestra.
- **Pensar que "Parado" es un error.** Con FL parado no hay tiempo que avanzar: la posición se queda quieta a propósito.

### Retos completados

(Ninguno todavía.)
