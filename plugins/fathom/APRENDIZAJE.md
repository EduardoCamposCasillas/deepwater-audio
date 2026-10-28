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

## Fase 2 — La campana: subir o bajar una zona

Ya ecualiza: una banda **campana** (en inglés *peaking* o *bell*) sube o baja una zona alrededor de una frecuencia y
deja el resto como estaba. Es la forma más usada de un EQ: casi todo lo que se hace en una mezcla con un EQ es una
campana, o varias.

### Conceptos aprendidos

- **Cómo se construye.** El filtro de estado variable (SVF, el mismo de Undertow) separa la señal en graves, una banda
  alrededor del corte y agudos. La campana es la señal original **más** un poco de esa banda (o **menos**, para
  cortar). Cuánto sumar sale de la ganancia; qué tan angosta es la banda, del Q. Es la fórmula del prototipo analógico
  de un EQ de consola, pasada a digital sin perder la estabilidad aunque muevas las perillas en pleno audio.
- **La curva es el sonido.** La línea blanca no es un dibujo aproximado: se calcula con los mismos números que usa el
  audio. Los tests comprueban que lo que mide un seno al pasar por Fathom coincide con la curva en menos de 0.0001 dB.
- **Q y ancho de banda.** El Q dice lo angosta que es la campana; el **ancho en octavas** (lo muestra el panel) es lo
  mismo dicho en musical. Q 0.7 ≈ 2 octavas (ancha, suave), Q 1.41 = 1 octava, Q 4.3 ≈ 1/3 de octava, Q 10 ≈ 1/7 de
  octava (quirúrgica). El ancho se mide donde la campana vale la mitad de su ganancia: una campana de +12 dB con Q 1.41
  vale +6 dB media octava a cada lado del centro.
- **Simétrica.** Subir +6 dB y bajar −6 dB con la misma frecuencia y Q son exactamente inversas: si pones dos Fathom
  seguidos así, la señal sale igual que entró. Y en la escala de octavas la campana es simétrica (mismo dB una octava
  abajo que una arriba), salvo muy cerca de 20 kHz (ver "aplastamiento" en Errores comunes).
- **Sin clics.** Mover la frecuencia, la ganancia o el Q de golpe haría clic (el filtro "recuerda" las muestras
  anteriores, que de pronto no cuadran con los ajustes nuevos). Fathom no salta: recorre el cambio en 20 ms, la
  frecuencia en octavas, la ganancia en dB y el Q en "veces". Apagar la banda baja su ganancia a 0 dB en 20 ms.
- **0 dB = intacto.** Con la ganancia en 0 dB (o la banda apagada) la señal sale **bit a bit** igual: no hay "color"
  escondido por tener el plugin puesto.

### Qué hace cada control (y qué muestra la pantalla)

- **FRECUENCIA** (10 Hz–30 kHz): dónde está el centro. Mueve el nodo "1" a izquierda y derecha. La franja de abajo te
  dice en qué zona estás (SUB, BARRO, PRESENCIA...) y el panel, qué nota de FL es (sirve para cazar la nota que
  retumba en un 808).
- **GANANCIA** (±30 dB): cuánto sube o baja el centro. Mueve el nodo arriba o abajo; la zona rellena es lo que la
  banda cambia. En el analizador, la SALIDA (azul) se separa de la ENTRADA (gris) justo con la forma de la curva.
- **Q** (0.025–40): lo angosta que es. Q bajo = una colina ancha que cambia el "color" general; Q alto = una aguja que
  toca una sola resonancia. El panel lo traduce a octavas.
- **ON**: comparar con y sin la banda (sin clic).
- **Lectura del mouse**: ahora dice también "curva +x dB": cuánto cambia el EQ el volumen en esa frecuencia exacta.

### Ejercicio de escucha guiado en FL Studio

1. **Ver que la curva es el sonido.** En un canal pon el 3xOsc con la forma de onda de ruido (o cualquier sample de
   ruido) y Fathom en su mixer. Toca una nota larga. Sube GANANCIA a +12 dB: la SALIDA azul toma la forma de la curva,
   montada sobre la ENTRADA gris. Mueve FRECUENCIA y Q y mira cómo la siguen.
2. **Ancho vs. angosto.** Sobre un loop de dembow completo: GANANCIA +6 dB en 2 kHz con Q 0.7 y luego con Q 8.
   ¿Cuál cambia más el "carácter" de toda la mezcla? (la ancha). ¿Cuál suena a "un tono" agregado? (la angosta).
3. **Q alto suena (resuena).** Pon solo el kick o un rim/clap. Campana de +18 dB con Q 20 en 1 kHz: el golpe empieza a
   "cantar" una nota. Una campana angosta y alta no solo sube volumen: hace que esa frecuencia **dure** más (por eso
   las subidas se hacen anchas). Baja a +4 dB y Q 1: el golpe vuelve a ser golpe.
4. **Con el volumen igualado.** Haz una subida de +4 dB en 3 kHz con Q 1 sobre la voz. Activa y desactiva ON: ¿mejor?
   Ahora baja SALIDA hasta que con ON y sin ON suene igual de fuerte (unos −1 a −1.5 dB) y vuelve a comparar. Muchas
   veces la "mejora" se achica a la mitad: parte era solo volumen. (En la Fase 6 llega el auto gain que lo hace solo.)

### Receta: "barrer y cortar" una resonancia del 808

1. Pon Fathom en el 808 (después del Riptide si lo saturas). Busca la zona que suena a "cartón" o "caja", que suele
   estar en BARRO o MEDIOS (200–800 Hz).
2. GANANCIA +10 dB, Q 6. Reproduce y mueve FRECUENCIA despacio por esa zona: en algún punto algo "silba" o se pone
   feísimo. Ese es el candidato.
3. Ahora invierte: GANANCIA −4 a −6 dB, mismo Q. Compara con ON/OFF **dentro del beat**, no en solo.
4. Por qué funciona: el 808 distorsionado genera armónicos que se acumulan en los medios-graves; restar una zona
   angosta limpia sin quitarle peso (el sub queda intacto) y le deja sitio al kick y a la voz. **Restar antes que
   sumar**: un corte angosto rara vez se oye como "EQ"; una subida angosta casi siempre sí.

### Reto sin receta

Haz que la voz principal se entienda mejor sobre los acordes **sin subirle el volumen ni tocar la voz**: solo con una
campana en el canal de los acordes. Pistas (si no sale): ¿en qué zona vive la claridad de la voz? (PRESENCIA); ¿qué le
pasa a esa zona en los acordes si la bajas 2–3 dB con un Q ancho (1–1.5)?; compara con ON/OFF con todo sonando, no en
solo; si los acordes se oyen "lejanos", te pasaste.

### Vocabulario

- **Campana / peaking / bell**: la forma de esta fase. En los EQ de consola (SSL, Neve) es la banda "MID".
- **Realce (boost) / corte (cut)**: subir o bajar. "Un corte de 3 dB en 400" = campana de −3 dB en 400 Hz.
- **Q**: lo angosta que es la campana. "Q alto" = angosta. Viene de "factor de calidad" de los circuitos resonantes.
- **Ancho de banda (en octavas)**: el mismo dato que el Q, en musical. "Un tercio de octava" es angosto.
- **Resonancia**: una frecuencia que sobresale y dura (el "boing" de una caja, el silbido de un 808 saturado).
- **Barrido (sweep)**: subir mucho una campana angosta y moverla para encontrar lo que molesta (la receta de arriba).
- **Quirúrgico**: un corte muy angosto que solo toca una resonancia; lo contrario de un EQ "musical" (ancho).

### Errores comunes (y cuándo NO usarlo)

- **Barrer para siempre.** Con +10 dB y Q 6, TODO suena feo en alguna parte: el barrido sirve para encontrar algo que
  ya oías, no para inventar problemas. Si no oías nada raro antes, no cortes.
- **Subidas angostas y altas.** Hacen resonar (ejercicio 3). Para dar carácter, subidas anchas y suaves (Q 0.5–1.5,
  +1 a +4 dB).
- **Ecualizar en solo.** Una voz "delgada" en solo puede ser justo la que se entiende en la mezcla. Decide con todo
  sonando.
- **Comparar sin igualar el volumen** (ejercicio 4).
- **Usar una campana donde va otra forma.** Para quitar los graves que sobran de una voz o un hi-hat, una campana
  ancha en 60 Hz no sirve: hace falta un corte de graves (Fase 3). Para "aire" en los agudos, un shelf (Fase 3).
- **Cuando el problema no es fijo.** Si una resonancia aparece solo en algunas notas del 808 o solo cuando la voz
  canta fuerte, un corte fijo le quita también cuando no hace falta: eso es un EQ dinámico (Fases 9 y 11).
- **El aplastamiento cerca de 20 kHz.** A 44.1 o 48 kHz, una campana en 15 kHz sale un poco más angosta del lado de
  arriba que la de un EQ analógico (es un límite de la forma de digitalizarla). La curva lo muestra tal cual es. En la
  Fase 7 se corrige; mientras, a 88.2/96 kHz casi no pasa.

## Fase 3 — Todas las formas y 24 bandas

Ya no es "una campana": cada una de las **24 bandas** puede tomar diez formas, y las bandas van **en serie** (lo que
una cambia, la siguiente lo recibe; en la curva, sus dB se suman). La campana sirve para una zona; las demás formas
resuelven lo que la campana no puede: quitar todo lo que está debajo de algo, dar "aire" a todo lo de arriba,
inclinar el sonido completo o eliminar un solo tono.

### Conceptos aprendidos

- **Shelf (estante).** Sube o baja *todo* lo que está debajo (shelf de graves) o encima (shelf de agudos) de la
  frecuencia, y se queda plano: son las perillas BASS y TREBLE de un equipo de sonido. La frecuencia marca el **punto
  medio** de la subida: un shelf de agudos de +6 dB en 8 kHz vale +3 dB justo en 8 kHz y casi +6 dB arriba de 15 kHz.
  Con **Q 0.71** sube lo más suave posible; con Q más alto aparece un pequeño "contragolpe" (baja un poco antes de
  subir), que algunos EQ de consola tienen a propósito porque suena más definido.
- **Tilt shelf.** Un balancín: alrededor de la frecuencia, lo que los agudos suben lo bajan los graves (GANANCIA +6 =
  graves −3, agudos +3). Cambia el color sin cambiar mucho el volumen total.
- **Flat tilt.** Una **pendiente pareja** de 20 Hz a 20 kHz, sin ninguna zona marcada (10 dB de GANANCIA = 1 dB por
  octava). Pone la mezcla un poco más "oscura" o más "brillante" de forma natural. Por dentro son 16 shelves diminutos,
  uno por octava, cuyos escalones se funden en una rampa (el mismo truco que convierte ruido blanco en rosa).
- **Cortes (low cut / high cut) y pendiente.** Un corte de graves quita todo lo que está debajo de la frecuencia (el
  famoso *high-pass* o *low cut*); el de agudos, lo de arriba. La **PENDIENTE** dice qué tan rápido cae: con 6 dB/oct,
  una octava más abajo suena a la mitad (suave, casi no se nota que hay filtro); 12 y 24 dB/oct son los más usados;
  48–96 dB/oct, una pared. En la frecuencia del corte vale −3 dB.
- **Butterworth y resonancia.** Los cortes de Fathom son *Butterworth* (lo más plano posible antes del corte). Con **Q
  0.71** es Butterworth puro; con Q más alto aparece un **pico justo antes del corte** (el "golpe" de un filtro de
  sintetizador). En un corte de graves de un bajo, Q 1–1.5 refuerza la nota más grave que queda; Q 0.5 hace la caída
  más redonda.
- **Notch.** Quita **una sola frecuencia por completo** (−100 dB y más): un zumbido de la corriente, una resonancia
  que silba siempre en el mismo tono. El Q dice lo angosto del hueco. A diferencia de una campana de −30 dB, el notch
  no tiene "fondo": en el centro, nada.
- **Pasa banda.** Deja solo la zona alrededor de la frecuencia (en el centro, 0 dB; lejos, nada). Es el efecto
  "teléfono" o "radio vieja" (pasa banda en 1–2 kHz con Q 1).
- **All-pass.** No cambia el **volumen** de ninguna frecuencia (la curva queda plana) sino la **fase**: retrasa unas
  frecuencias respecto de otras. Solo, casi no se oye; sumado con otra señal (dos micros, un kick en capas, una señal
  en paralelo) cambia cómo se suman. Es la primera pista de que un EQ hace algo más que subir y bajar volumen (la
  fase vuelve en la Fase 10).
- **Cambiar de forma sin clic.** No se puede "convertir" un corte en un shelf: Fathom funde la banda a cero en 20 ms,
  cambia la forma y la vuelve a subir. Una banda apagada no gasta CPU.

### Qué hace cada control (y qué muestra la pantalla)

- **Selector de banda** (Banda 1 … 24, las encendidas con ●): qué banda muestra el panel. Un **clic sobre un nodo** del
  visor hace lo mismo. Cada banda tiene su color y empieza en su propia frecuencia (de 30 Hz a 16 kHz), así al
  encender varias no quedan una encima de otra.
- **ON**: enciende la banda (aparece su nodo de color y su relleno).
- **FORMA**: una de las diez. Las perillas que esa forma no usa se atenúan (GANANCIA en los cortes, Q en el flat tilt).
- **PENDIENTE**: solo en los cortes (de 6 a 96 dB/oct). Mira cómo la pared se vuelve vertical.
- **FRECUENCIA / GANANCIA / Q**: lo mismo de la Fase 2, con el significado de cada forma; el texto de la izquierda lo
  traduce (punto medio del shelf, corte a −3 dB, dB/oct del flat tilt…).
- **Visor**: la línea blanca es el resultado **total**; cada banda encendida rellena con su color lo que ella sola sube o
  baja (la elegida, más marcada). Los nodos de cortes, notch, pasa banda y all-pass van en la línea de 0 dB; los de las
  formas con ganancia, sobre su curva (en un shelf, en el punto medio).

### Ejercicio de escucha guiado en FL Studio

1. **Pendientes.** Ruido blanco (3xOsc con la forma de ruido) en un canal con Fathom. Banda 1: corte de graves en 500
   Hz. Pasa por 6, 12, 24, 48 y 96 dB/oct mirando la SALIDA azul: con 6 casi no se siente "filtro"; con 96 parece que
   el sonido se acaba en una pared. Ahora lo mismo sobre un loop de dembow con el corte en 100 Hz.
2. **El corte de graves en la voz.** La voz sola, corte de graves de 24 dB/oct. Súbelo desde 20 Hz despacio: hasta
   ~80–120 Hz (voz masculina) casi no cambia el timbre, pero desaparecen golpes del micro y ruido grave. Sigue
   subiendo: en algún punto la voz se vuelve "delgada". Quédate un poco antes de ese punto y compara dentro del beat
   con ON/OFF.
3. **Shelf vs. campana para el "aire".** En la voz: shelf de agudos +3 dB en 10 kHz (Q 0.71) contra una campana de +3
   dB en 10 kHz (Q 1). La campana suena a "un brillo en un lugar"; el shelf, a "más abierto". Iguala el volumen con
   SALIDA antes de decidir (el shelf sube más energía: bájale ~0.5 dB).
4. **Resonancia del corte.** En el 808: corte de graves en 30 Hz, 24 dB/oct. Q 0.71 contra Q 2: con Q 2 la nota más
   grave que sobrevive "empuja" más (el pico antes del corte se ve en la curva). Más peso, pero también más riesgo de
   retumbar.
5. **Notch vs. campana.** Un tono fijo (3xOsc, seno en A5 = 440 Hz): campana de −30 dB, Q 10 en 440 Hz → todavía se
   oye un poco. Cambia a NOTCH: desaparece. Sube el Q del notch a 40: sigue desapareciendo, y lo demás casi no se toca.
6. **El all-pass "no hace nada"… hasta que sí.** El mismo kick en dos canales del mixer y Fathom en uno solo, con
   all-pass en 80 Hz, Q 0.7. Cada canal solo suena igual que antes; los dos juntos pierden golpe grave: la fase
   desalineada hace que se resten. Por eso la fase importa con capas.

### Receta: limpiar el dembow con cortes y shelves

1. **Hi-hats y percusión aguda:** corte de graves de 12–24 dB/oct en 250–500 Hz. En solo no suenan distinto, pero
   dejan de sumar barro y sub que no aportan nada.
2. **Voz:** corte de graves de 24 dB/oct en 80–120 Hz (masculina) o 120–180 Hz (femenina), y shelf de agudos +2 dB en
   10–12 kHz (Q 0.71) para el aire. El autotune agrega artefactos agudos: el shelf los sube menos que una campana
   angosta y alta.
3. **808:** corte de graves de 24 dB/oct en 25–30 Hz (Q 0.71–1): lo que hay debajo casi ningún sistema lo reproduce y
   le roba headroom al master.
4. **Kick:** si se pelea con el 808, *no* cortes los dos en el mismo sitio. Elige quién manda en el sub: el kick con un
   shelf de graves −2 dB en 60 Hz y el 808 intacto, o al revés.
5. **Bus de acordes / pads:** corte de graves de 12 dB/oct en 150–250 Hz para que no tapen al 808; si suenan lejanos,
   un tilt shelf +2 dB en 2 kHz en vez de tres campanas.

Por qué funciona: cada instrumento ocupa más espectro del que necesita. Los cortes quitan lo que **no aporta pero
suma** (energía que se acumula en el bus y en el master), y los shelves cambian el carácter de una zona entera sin
crear picos que resuenen.

### Reto sin receta

En el master de un beat tuyo, haz que suene "más caro" usando **solo un flat tilt y un corte**, sin campanas. Pistas
(si no sale): una mezcla equilibrada cae suave hacia los agudos (pon una referencia comercial con Fathom en su canal y
mira la inclinación del analizador); ¿tu beat necesita más o menos inclinación que la referencia?; con el flat tilt
basta ±0.3–0.6 dB/oct (GANANCIA ±3–6); el corte va muy abajo (20–25 Hz, 12 dB/oct); compara con el volumen igualado.

### Vocabulario

- **Shelf / estante (low shelf, high shelf)**: sube o baja una zona entera hasta el extremo. "Un shelf de aire" =
  shelf de agudos suave en 10–12 kHz.
- **Tilt**: inclinar todo el espectro alrededor de un punto (la perilla TILT de Riptide hace esto).
- **Low cut / high-pass (HPF)**: corte de graves. **High cut / low-pass (LPF)**: corte de agudos. "High-pass" dice qué
  *pasa*; "low cut", qué *quita*: son lo mismo.
- **Pendiente (slope)**: dB por octava de la caída. "Un HPF de 24" = corte de graves de 24 dB/oct.
- **Butterworth**: el corte más plano antes de la frecuencia de corte; el "estándar".
- **Resonancia**: el pico justo antes del corte; en sintetizadores, el "squelch" del filtro del acid.
- **Notch**: un hueco total y angosto. "Notchear 60 Hz" = quitar el zumbido de la corriente.
- **Pasa banda (band-pass, BPF)**: solo la zona del medio; el "efecto teléfono" de los intros.
- **Fase**: en qué momento llega cada frecuencia. Un all-pass la cambia sin tocar el volumen.

### Errores comunes (y cuándo NO usarlo)

- **Cortar graves "por costumbre" en todo.** Un low cut en cada canal a 100 Hz deja la mezcla delgada, y sumados los
  cortes acumulan giros de fase en los graves. Corta lo que *sobra*, pista por pista, escuchando en contexto.
- **96 dB/oct para todo.** Las pendientes extremas suenan más a "filtro", giran más la fase cerca del corte y pueden
  "anillar" con Q alto. Úsalas cuando de verdad necesitas una pared (quitar el sub de una voz pegada a un 808); para lo
  demás, 12–24 dB/oct.
- **Resonancia alta en el corte de graves del 808.** El pico suma volumen justo en la nota más grave: el limitador del
  master trabaja de más y en el club retumba. Q 0.71–1, salvo que busques ese golpe a propósito.
- **Shelf de agudos con Q alto para "aire".** El contragolpe crea un pico antes de la subida que puede sonar áspero;
  para aire, Q 0.5–0.71.
- **Notch para resonancias que cambian.** Si el tono que molesta cambia con las notas (un 808 con glide), un notch fijo
  falla la mitad del tiempo: eso es dinámica espectral (Fase 11).
- **Flat tilt grande.** Con ±2–3 dB/oct ya es un cambio enorme (20–30 dB de un extremo al otro). En mezcla y master,
  pequeño.
- **Comparar sin igualar el volumen** (siempre): un shelf de agudos casi siempre "gana" la comparación porque suena más
  fuerte.

## Fase 4 — Editar sobre el visor: mover la curva con las manos

El sonido no cambia en esta fase: cambian las **manos**. Hasta ahora cada ajuste pasaba por el panel (elegir banda,
encender, girar tres perillas). Ahora la curva se toca directamente donde se ve el problema: sobre el analizador. Eso
cambia la forma de trabajar: en vez de "pensar en números", miras un pico del espectro, pones un nodo encima y lo mueves
**escuchando**. Los números siguen ahí (en la etiqueta junto al nodo y en el panel) para aprender qué hiciste.

### Conceptos aprendidos

- **El nodo es la banda.** Su posición horizontal es la FRECUENCIA, la vertical es la GANANCIA y la rueda es el **Q**
  (lo ancho). En un shelf, el nodo está en el punto medio de la subida: subir el nodo 1 dB sube la meseta 2 dB (el nodo
  sigue al mouse y la meseta sube el doble). En cortes, notch, pasa banda y all-pass no hay ganancia: solo se mueven en
  horizontal.
- **Mover en octavas.** Arrastrar a la derecha sube la frecuencia por octavas, no por Hz: el mismo movimiento de mouse
  va de 100 a 200 Hz que de 5 a 10 kHz. Por eso, al mover **varias bandas juntas**, la forma de la curva se conserva
  (una a 100 Hz y otra a 1 kHz siguen a una década de distancia).
- **Ajuste fino.** Con **Shift** el mouse mueve diez veces menos. Para el master o una voz, donde ±0.5 dB ya se oye.
- **Un solo eje.** Con **Alt** el nodo solo se mueve en la dirección en que más empujaste: cambias la frecuencia sin
  tocar la ganancia (o al revés). Evita "ensuciar" un ajuste que ya estaba bien.
- **Banda libre / en uso.** Una banda está *libre* si está apagada y como nace; el doble clic crea en la primera libre.
  Apagada pero con ajustes propios está *en uso*: su nodo se ve **hueco** y se puede seguir editando (sirve para
  comparar con y sin ella con doble clic). **Supr** la borra: vuelve a estar libre.
- **Frecuencia = nota.** El teclado bajo el visor pone cada nota en su frecuencia (nombres de FL: C5 = Do central,
  A5 = 440 Hz). Un clic en una tecla lleva la banda justo a esa nota; "Afinar a la nota más cercana" (clic derecho)
  corrige una banda que quedó a unos cents. Un 808 en G2 de FL vibra a 49 Hz: una campana afinada a G2 le da peso **a
  esa nota** sin tocar las vecinas.
- **Deshacer = probar sin miedo.** Cada edición (arrastre, rueda, crear, borrar, una perilla del panel) es un paso de
  Ctrl+Z. La automatización de FL no entra al historial: deshacer no pelea con FL. El historial sigue ahí aunque cierres
  la ventana del plugin.

### Qué hace cada control (y qué muestra la pantalla)

- **Doble clic en el vacío**: crea una campana ahí, con la ganancia de la altura del clic (encima de la línea de 0 dB
  sube, debajo baja). En los extremos (bajo 25 Hz o sobre 16 kHz) crea un corte, que es lo que casi siempre va ahí.
- **Arrastrar un nodo**: frecuencia y ganancia. **Shift** = fino, **Alt** = un solo eje. Junto al nodo se lee la banda,
  la forma, los Hz, la nota y los dB/Q.
- **Rueda sobre un nodo**: el Q (hacia arriba = más angosto). Con Shift, fino.
- **Doble clic en un nodo**: apagar o encender (el nodo queda hueco): el ON/OFF más rápido para comparar una banda.
- **Ctrl+clic**: agregar o quitar un nodo de la selección. **Arrastrar en el vacío**: un rectángulo que elige todos los
  nodos de dentro. **Ctrl+A**: todas las bandas en uso. **Esc**: solo la del panel. Arrastrar, la rueda y el menú
  actúan sobre todas las elegidas (anillo blanco); el panel muestra la que tocaste último (anillo grueso).
- **Clic derecho** en un nodo: apagar/encender, forma, pendiente, afinar a la nota, borrar. En el vacío: crear
  cualquier forma ahí.
- **Teclado del eje**: clic en una tecla = la banda del panel a esa nota exacta. Arriba se lee la nota y sus Hz.
- **DESHACER / REHACER** (o Ctrl+Z / Ctrl+Y): todo lo que editaste en la GUI.

### Ejercicio de escucha guiado en FL Studio

1. **Barrer para encontrar.** Un loop de voz (o un pad) con Fathom. Doble clic arriba del 0 dB a ~1 kHz para crear una
   campana de unos +8 dB. Gira la rueda hasta un Q de 5–8 (angosta). Ahora **arrastra lentamente** de 200 Hz a 5 kHz
   con Alt (solo horizontal). En algún punto algo "silba" o "suena a caja" mucho más que en otros: esa es una
   resonancia. Suelta ahí.
2. **Y cortar lo que encontraste.** Sin mover la frecuencia (Alt + arrastrar hacia abajo), baja la campana a −3/−5 dB.
   Abre un poco el Q (rueda hacia abajo) hasta que el "silbido" desaparezca sin que la voz pierda cuerpo. Doble clic en
   el nodo para comparar con y sin. Iguala con SALIDA antes de decidir.
3. **Afinar al 808.** Mira el piano roll de FL: ¿en qué nota está el 808 del beat? (digamos A2 de FL = 55 Hz). Crea una
   campana en los graves y haz clic en la tecla A2 del teclado del eje: la banda queda en 55 Hz exactos. Sube +2 dB con
   Q 2. Toca notas distintas del 808: la que coincide con la banda suena más gorda que las otras. Así se entiende por qué
   se "afina" el EQ del bajo a la tonalidad de la canción (y por qué a veces es mejor no hacerlo).
4. **Mover el grupo.** Crea tres bandas en un pad: −3 dB en 300 Hz, +2 dB en 3 kHz, +2 dB en 10 kHz. Haz un rectángulo
   para elegir las tres y arrástralas juntas media octava a la derecha y luego a la izquierda: el "carácter" de la curva
   es el mismo, pero se oye en otra zona. Ctrl+Z para volver.

### Receta: limpiar una voz con autotune en dos minutos

1. **Corte de graves**: doble clic bien a la izquierda (bajo 25 Hz) crea un corte; arrástralo a ~90–120 Hz y en el clic
   derecho → Pendiente elige 24 dB/oct.
2. **Barro**: doble clic en ~300 Hz debajo del 0 dB, −2 dB, Q ~1 (casi una octava y media). Shift para dejarlo fino.
3. **Resonancia**: el barrido del ejercicio 1 entre 1 y 4 kHz; donde silbe, −3 dB con Q 4–6.
4. **Aire**: clic derecho en el vacío a ~11 kHz → Crear aquí → Shelf agudos; sube el nodo +1.5 dB (la meseta sube +3).
5. **Comprobar**: compara todo con el BYPASS con el volumen igualado (si con el EQ suena más fuerte, baja la SALIDA hasta
   que suene igual de fuerte). Para revisar una sola banda, doble clic en su nodo.

Por qué funciona: el orden (quitar lo que sobra abajo → el barro → la resonancia → sumar al final) es "restar antes de
sumar". Al quitar primero, muchas veces ya no hace falta sumar nada, y lo que sumas no realza basura.

### Reto sin receta

En un kick del dembow, encuentra la frecuencia de su **golpe grave** y la de su **click** (el ataque) usando solo el
barrido con una campana angosta; después, deja el kick con más golpe y un poco menos de click **con dos bandas como
máximo**. Pistas (si no sale): el golpe vive entre 50 y 100 Hz y el click entre 2 y 5 kHz; con Q alto (8+) y +10 dB el
barrido se oye mucho más claro; si el kick tiene tono, afina el golpe a su nota con el teclado; el click se baja mejor
con una campana ancha (Q ~1) que con una angosta; compara con el volumen igualado.

### Vocabulario

- **Barrido (sweep)**: subir una campana angosta y moverla para encontrar una frecuencia molesta. "Barre los medios y
  corta lo que silbe."
- **Resonancia**: una frecuencia que suena mucho más que sus vecinas (la caja de un tambor, un cuarto mal tratado, el
  "nasal" de un micro). En el analizador se ve como un pico angosto que no se mueve con las notas.
- **Nodo / punto**: el control de una banda sobre la curva ("agarra el punto 3 y bájalo").
- **Ajuste fino**: el modificador que hace el movimiento más lento (Shift en casi todos los plugins).
- **Afinar el EQ**: poner una banda en la frecuencia exacta de una nota (la tónica del bajo, la fundamental del kick).
- **Undo / redo**: deshacer / rehacer.

### Errores comunes (y cuándo NO usarlo)

- **Dejar el barrido subido.** El barrido de +10 dB es para *encontrar*, no para dejarlo así: un pico angosto y alto
  suena horrible. Siempre termina en un corte (o en nada).
- **Cortar todo lo que encuentras barriendo.** Con +10 dB y Q 8, *cualquier* frecuencia suena fea. Corta solo lo que
  también molesta con la campana en 0 dB, escuchando la mezcla completa y no el instrumento solo.
- **Mirar en vez de escuchar.** El visor ayuda a ubicar, pero el oído decide. Prueba ajustar con los ojos cerrados y
  luego mira qué hiciste.
- **Afinar el bajo "a la fuerza".** Si el 808 cambia mucho de nota, una campana afinada a una sola nota hace que unas
  notas suenen más fuertes que otras. Úsalo cuando la línea de bajo se queda casi siempre en la tónica.
- **Mover grupos sin escuchar cada banda.** Mover tres bandas a la vez es rápido, pero cada una puede caer en una zona
  donde ya no hace falta. Después de mover el grupo, revisa cada una con doble clic (on/off).
- **Comparar sin igualar el volumen** (siempre).


## Fase 5 — El analizador profesional: aprender a leer el espectro

El sonido no cambia en esta fase: cambian los **ojos**. El analizador de las fases anteriores mostraba "algo"; ahora se
puede ajustar para que muestre lo que necesitas en cada momento (el balance de una mezcla, la nota que retumba del
808, el golpe del kick) y se puede usar como herramienta: congelarlo para medir, guardar los picos, ver otra pista por
el sidechain y **agarrar un pico** para poner una banda justo encima.

### Conceptos aprendidos

- **La música "cae" hacia los agudos.** Casi todo lo que suena tiene más energía en graves que en agudos: un espectro
  sin compensar baja de izquierda a derecha y los agudos parecen "faltar" aunque estén bien. La **inclinación** suma
  unos dB por octava hacia los agudos (1 kHz no se mueve) y endereza esa caída: con **4.5 dB/oct** una mezcla
  equilibrada se ve casi plana, así que una zona con de más o de menos salta a la vista. Es solo la vista: no cambia el
  sonido. Ojo: los números del analizador (izquierda) quedan "inclinados" también (un pico a 8 kHz marca 13.5 dB más
  de lo que mediría sin inclinación).
- **Ver frecuencia o ver tiempo: no se puede todo.** La FFT mira un trozo de audio. Un trozo largo separa mejor las
  frecuencias (con 16384 puntos los bins están a 2.7 Hz: se distinguen G1 = 49 Hz y G#1 = 52 Hz), pero mezcla todo lo
  que pasó en esos 0.37 s (un golpe de kick se ve "embarrado" en el tiempo). Un trozo corto (2048: 46 ms) sigue cada
  golpe, pero en graves cada bin mide 21.5 Hz y dos notas vecinas son la misma mancha. Es el **compromiso
  tiempo-frecuencia** (el mismo "principio de incertidumbre" de la física). La **resolución** elige de qué lado estás.
- **Balística: cuánto tarda la línea.** La línea sube hacia lo nuevo con una constante de tiempo (lo que tarda en
  recorrer el 63 %) y baja a tantos dB por segundo. **Lenta**: un promedio estable, el "balance tonal" (sirve para
  comparar tu mezcla con una canción de referencia). **Rápida**: ves cada nota y cada golpe (sirve para encontrar qué
  resuena y cuándo).
- **Retener los picos.** Una línea fina guarda el máximo de cada frecuencia. Muestra lo que pasó aunque ya no suene:
  la nota del 808 que llega más alto que las demás, el pico de la "s" de la voz. Con **2 s** ves lo reciente; con
  **Siempre**, todo lo que sonó (BORRAR PICOS para empezar de nuevo).
- **Qué es un pico "de verdad".** Un pico vale la pena cuando se **despega** de lo que lo rodea (su **prominencia**:
  cuánto sube sobre el valle más alto a sus lados). Un bulto pequeño en la ladera de una montaña no es un pico; una
  aguja angosta sobre un fondo parejo, sí. Fathom marca los picos que se despegan al menos 6 dB.
- **Agarrar un pico.** La FFT da el nivel en bins separados; un tono que cae entre dos bins se ve más bajo y corrido.
  Para poner la banda **justo** encima, Fathom ajusta una parábola por los tres bins más altos del pico y toma su
  vértice: la frecuencia queda a una fracción de Hz de la real (lo comprueban los tests con un seno de 87.31 Hz).
- **El sidechain para mirar otra pista.** El enmascaramiento (dos sonidos en la misma zona se tapan) se ve cuando
  pones los dos espectros uno encima del otro: el 808 en azul (SALIDA) y el kick en ámbar (SIDECHAIN). Donde se
  cruzan, uno de los dos tiene que ceder.

### Qué hace cada control (y qué muestra la pantalla)

Todo está en la columna **ANALIZADOR** de la derecha y se guarda con el proyecto (menos CONGELAR).

- **ENTRADA / SALIDA / SIDECHAIN**: qué espectros se ven (gris, azul, ámbar). Si activas SIDECHAIN sin haberlo
  conectado, el visor dice "SIDECHAIN SIN CONECTAR".
- **INCLINACIÓN**: 0 (crudo), 3, **4.5** (por defecto) o 6 dB/oct.
- **RANGO**: cuántos dB cubre la escala de la izquierda: 48 o 72 para ver con detalle lo fuerte (master), 96 (por
  defecto) o 120 para ver también el ruido de fondo y las colas de reverb.
- **RESOLUCIÓN**: Baja (2048), **Media** (4096, por defecto), Alta (8192), Máxima (16384).
- **VELOCIDAD**: de Muy lenta (cae 6 dB/s) a Muy rápida (240 dB/s). Media = la de las fases anteriores.
- **PICOS**: No / 2 s / Siempre, y **BORRAR PICOS**.
- **CONGELAR**: el espectro se queda quieto (el audio sigue). Arriba a la izquierda dice "CONGELADO".
- **Agarrar un pico**: pasa el mouse cerca de un pico del espectro azul: aparece un círculo con sus Hz y su nota. Clic
  y arrastra: nace una campana angosta (Q 4) justo en el pico; hacia abajo lo cortas, hacia arriba lo realzas. Mientras
  arrastras, el espectro se congela para que el pico no se te escape. Es un solo Ctrl+Z.
- **Imán (Ctrl mientras arrastras un nodo)**: el nodo salta al pico más cercano. Suelta Ctrl para moverlo libre.

### Ejercicio de escucha guiado en FL Studio

1. **Inclinación.** Pon Fathom en el master de un beat terminado (sin bandas). Mira el espectro con INCLINACIÓN en 0:
   cae hacia la derecha. Cambia a 4.5: casi plano. Ahora pon una canción de reggaetón comercial que te guste en otra
   pista y mírala igual: compara las dos "formas". Si la tuya tiene una joroba en 200-400 Hz que la referencia no
   tiene, ya sabes dónde está el barro. (Iguala el volumen de las dos antes de escuchar.)
2. **Resolución con el 808.** En la pista del 808, PICOS = Siempre, VELOCIDAD = Rápida. Resolución **Baja**: toca el
   patrón del 808: los graves son una mancha. Cambia a **Máxima** (BORRAR PICOS): ahora cada nota del 808 es una aguja
   separada, y la línea de picos te dice cuál llega más alto. ¿Coincide con la nota que se oye más fuerte?
3. **Rápido vs. lento.** En la pista de la voz, VELOCIDAD = Muy rápida: ves cada sílaba y las "eses" saltando a
   5-9 kHz. Muy lenta: ves la "forma promedio" de la voz. Las dos son verdad; cada una sirve para otra cosa.
4. **Agarrar la resonancia.** En un loop de percusión o una voz, CONGELA cuando veas una aguja que sobresale en los
   medios. Pasa el mouse por encima, agárrala y arrastra hacia abajo −4 dB. Descongela y escucha con y sin la banda
   (doble clic en el nodo). ¿Desapareció el "silbido" sin perder cuerpo?

### Receta: tallar el 808 contra el kick del dembow (con el sidechain)

1. El ruteo es el mismo que el de Ebb: kick y 808 en pistas propias del Mixer. **Selecciona la pista del kick**; en
   la pista del **808**, clic derecho en la flecha de envío (el triangulito de abajo) → **Sidechain to this track**.
   Abre Fathom en el 808, el **engranaje** del envoltorio de FL → **Processing** → en **Connections**, la entrada
   **Sidechain** (el segundo puerto de entrada) = la pista del kick. El kick no suena dos veces.
2. En Fathom: SALIDA (el 808) y SIDECHAIN (el kick), INCLINACIÓN 4.5, RESOLUCIÓN Alta, PICOS = 2 s.
3. Mira dónde está el pico grave del kick (ámbar, entre 50 y 100 Hz) y dónde el del 808. Si caen en la misma zona,
   se tapan: el kick "no pega".
4. Agarra en el espectro del 808 el punto que está justo bajo el pico del kick y bájalo −2 a −4 dB con Q 2-4 (más
   ancho que la campana con que nace: rueda hacia abajo).
5. Escucha los dos juntos, con y sin la banda, con el volumen igualado. El kick debe "salir" sin que el 808 pierda peso.

Por qué funciona: no hace falta quitarle graves al 808, solo un hueco angosto donde vive el golpe del kick. El oído
escucha el 808 "completo" (sus otras notas y armónicos siguen ahí) y el kick gana su lugar. Es tallar espacio, no
ecualizar a ciegas. (En la Fase 9, ese hueco podrá abrirse solo cuando suena el kick.)

### Reto sin receta

En una voz con autotune, encuentra las **dos resonancias más molestas** usando solo el analizador (congelar, picos
retenidos, agarrar) y córtalas con un máximo de −4 dB cada una. Después decide si dejaste la voz mejor o solo
distinta. Pistas (si no sale): los picos que aparecen en TODAS las notas (siempre en la misma frecuencia) son
resonancias del micro o del cuarto; los que se mueven con las notas son armónicos de la voz y no se cortan; mira con
VELOCIDAD rápida y PICOS 2 s para verlos aparecer; un corte de −3 dB en la resonancia correcta se oye más que −8 dB en
la equivocada; compara siempre con el volumen igualado.

### Vocabulario

- **Inclinación (tilt) del analizador**: compensar la caída natural del espectro para leerlo "plano". "Ponle 4.5 al
  analizador y compara con la referencia."
- **Resolución (tamaño de FFT)**: cuántos puntos analiza la FFT; más = más detalle en graves, menos = más rápido.
- **Balística**: cómo sube y baja un medidor (rápido, lento). Los vúmetros y los medidores de pico tienen balísticas
  distintas.
- **Retención de picos (peak hold)**: guardar el máximo de cada frecuencia.
- **Prominencia**: cuánto se despega un pico de su alrededor.
- **Balance tonal**: la forma promedio del espectro de una mezcla ("esta mezcla está oscura", "le sobra medio").
- **Pista de referencia**: una canción comercial que se usa para comparar la tuya.
- **Sidechain**: mandar el audio de una pista a un plugin de otra para que lo "vea" (aquí, para mirarlo; en Ebb, para
  que lo controle).

### Errores comunes (y cuándo NO usarlo)

- **Ecualizar con los ojos.** El analizador dice dónde mirar, no qué hacer. Un pico en el espectro puede ser
  justamente el carácter del sonido (la fundamental de la voz, el golpe del kick). Agarra un pico, pero decide
  escuchando.
- **Querer un espectro "plano".** Con inclinación 4.5 una mezcla equilibrada se ve *casi* plana, no plana. Cada género
  tiene su forma (el reggaetón lleva mucho más sub que el rock). Compara con referencias del mismo género.
- **Comparar con distinta configuración.** Si cambias la inclinación, la resolución o la velocidad, la forma cambia.
  Para comparar dos pistas usa los mismos ajustes en las dos.
- **Resolución máxima para todo.** Con 16384 puntos el analizador llega tarde a los golpes: los transitorios se ven
  más bajos de lo que son. Úsala para notas graves, no para percusión.
- **Cortar todos los picos que aparecen.** Los armónicos de un instrumento son picos y son el instrumento. Corta solo
  lo que también molesta al oído, sobre todo lo que se queda quieto mientras las notas cambian.
- **Dejar el sidechain conectado "por si acaso".** No cambia el sonido, pero es una ruta más en el proyecto; quítala
  cuando termines de comparar.

## Fase 6 — Escuchar y comparar: que el oído decida (sin trampas)

Hasta aquí Fathom servía para **cambiar** el sonido. Esta fase es para **decidir** si el cambio sirve. El oído se
engaña fácil: lo más fuerte parece mejor, lo que acabas de tocar parece necesario, y después de diez minutos ya no
recuerdas cómo sonaba antes. Las herramientas de esta fase quitan esas trampas: escuchar solo la zona que tocas,
comparar a volumen igual, alternar entre dos versiones y bajar todo el EQ a la mitad para ver si te pasaste.

### Conceptos aprendidos

- **El sesgo del volumen, en serio.** Una diferencia de 0.5–1 dB no se oye como "más fuerte": se oye como "más claro",
  "más grande", "mejor". Casi cualquier realce sube el volumen total, así que al comparar con el BYPASS el EQ siempre
  "gana". Por eso Fathom tiene **AUTO GAIN**: compensa el volumen que mueve la curva para que la comparación sea justa.
- **Cómo se calcula el auto gain (sin escuchar la música).** Medir la señal en vivo y corregirla bombearía con la
  música. Fathom lo calcula de la **curva**: imagina una mezcla típica (la misma energía en cada octava, el *ruido
  rosa*), le aplica la curva y mide cuánto cambia su sonoridad con el mismo filtro que usan los medidores de **LUFS**
  (el filtro "K": casi no cuenta lo que hay bajo 40 Hz y pesa un poco más lo que hay sobre 2 kHz). Por eso:
  - un shelf de agudos de +3 dB en 1 kHz compensa −2.3 dB (sube la mitad del espectro que el oído pesa más);
  - un shelf de graves de +3 dB en 200 Hz compensa solo −0.5 dB (el oído pesa poco los graves);
  - una campana angosta de +6 dB (Q 8) en 1 kHz compensa −0.2 dB (sube una franja que pesa poco en el total);
  - un corte de graves en 30 Hz compensa +0.03 dB: nada (el oído casi no cuenta esa zona).
  La compensación solo cambia cuando cambia la curva: no bombea ni cambia la dinámica.
- **Escuchar la zona (SOLO).** Ver un pico en 340 Hz no te dice si es *eso* lo que suena feo. Con SOLO dejas de oír la
  mezcla y escuchas solo la franja que toca la banda (un pasa banda con su frecuencia y su ancho). Es la técnica clásica
  de "realzar y barrer" pero sin realzar: pones la banda angosta, barres la frecuencia y cuando el sonido "chilla" o
  "zumba", la encontraste. En un corte de graves, SOLO te hace escuchar **lo que vas a quitar** (¿se va algo del bombo?).
- **La escala de ganancia.** Después de un rato ecualizando, casi siempre hay de más. La ESCALA multiplica los dB de
  todas las bandas: 50 % = la misma forma a la mitad. Si a 70 % suena igual de bien, quédate en 70 %: menos EQ = menos
  efectos secundarios (fase, resonancias, sonido "procesado").
- **Polaridad.** Invertir la polaridad pone la onda al revés (lo que subía, baja). Una pista sola suena idéntica
  invertida; lo que cambia es **cómo se suma con otra parecida**. Dos micrófonos del mismo instrumento, un kick en
  capas, un 808 con un sub aparte: si están "al revés" entre sí, los graves se cancelan y suenan flacos. Invertir uno
  de los dos y escuchar cuál suena **más lleno** es la prueba.
- **Balance.** En una pista estéreo, "panear" en realidad es balance: al girar a la derecha baja el lado izquierdo y el
  derecho se queda igual. Fathom lo hace así (no sube el lado al que giras).
- **Comparar A/B.** La memoria auditiva dura segundos. Para elegir entre dos ideas hay que poder saltar de una a la
  otra al instante, con todo lo demás igual. A y B son dos EQ completos dentro del mismo plugin; cambiar no hace clic.

### Qué hace cada control (y qué muestra la pantalla)

- **SOLO** (panel de la banda, amarillo; también en el menú del clic derecho de un nodo: "Escuchar su zona"): escuchas
  solo la zona de la banda del panel. Si eliges otra banda con el SOLO encendido, pasa a esa. Campana, notch, pasa
  banda y all-pass = la franja de su frecuencia y su Q; shelf o corte de graves = todo lo de abajo; de agudos = todo lo
  de arriba. Los tilts no tienen zona (el visor lo avisa). Arriba a la izquierda del visor dice "SOLO: BANDA n". No se
  guarda con el proyecto y se apaga al cerrar la ventana (para que nunca te quede un EQ "en solo" sin saberlo).
- **ESCALA** (columna SALIDA, 0–200 %, doble clic = 100 %): la intensidad de todas las bandas. La curva y los nodos se
  mueven; la etiqueta de un nodo muestra su ganancia y, con la escala distinta de 100 %, lo que de verdad suena
  ("+6.0 dB → +3.0 dB"). Arrastrar un nodo sigue al mouse igual.
- **AUTO GAIN**: encendido, compensa el volumen; debajo dice cuánto ("−2.1 dB"). No mueve la curva del visor (la curva
  sigue mostrando la forma del EQ).
- **BALANCE** (C, L 1–100, R 1–100; doble clic = centro).
- **Ø POLARIDAD**: invierte la salida, con un fundido de 20 ms (sin clic).
- **GANANCIA**: la salida de siempre (±24 dB).
- **A / B / A → B** (cabecera): clic en la letra apagada = pasar a ese lado. La primera vez, B nace como copia de A.
  "A → B" copia el lado activo al otro. Cada lado tiene su propio DESHACER. El lado que no está activo se guarda con el
  proyecto.

### Ejercicio de escucha guiado en FL Studio

1. **La trampa del volumen.** En el master de un beat, crea una campana ancha (Q 0.7) de +2 dB en 3 kHz. Alterna con
   BYPASS: "suena mejor". Ahora enciende AUTO GAIN (dice −0.7 dB) y alterna otra vez. ¿Sigue sonando mejor,
   o solo era más fuerte? Repite con los ojos cerrados (pídele a alguien que alterne por ti).
2. **Barrer con SOLO.** En la voz: campana Q 8, ganancia 0 dB, SOLO encendido. Arrastra el nodo despacio de 200 Hz a
   5 kHz escuchando solo esa franja. Vas a oír zonas "normales" y una o dos que suenan a caja, a nariz o que silban.
   Detente en la peor, apaga el SOLO y bájala −3 dB. Compara con AUTO GAIN encendido.
3. **¿Qué quita mi corte?** En el hi-hat, un corte de graves en 400 Hz a 24 dB/oct. SOLO: escuchas lo que se va. Si
   oyes "nada" o solo un golpe sordo, el corte está bien; si oyes el cuerpo del hi-hat, bájalo.
4. **La escala.** Después de ecualizar una pista con 4–5 bandas, baja la ESCALA a 0 % y súbela despacio. ¿En qué
   punto deja de mejorar? Muchas veces es antes del 100 %.
5. **Polaridad de las capas.** Si tu kick tiene dos capas (un golpe y un sub) en pistas distintas, pon Fathom en una
   de ellas y alterna Ø POLARIDAD con las dos sonando: una de las dos posiciones suena con más pecho. Esa es la buena.

### Receta: decidir entre dos EQ del 808 con A/B

1. En el 808, con el kick sonando (sidechain al analizador, como en la Fase 5), en **A** haz la versión "hueco": una
   campana de −3 dB y Q 3 justo donde vive el golpe del kick (por ejemplo 62 Hz, un B1 de FL).
2. Pasa a **B** (nace igual que A). Apaga la campana y en su lugar sube un shelf de graves de +2 dB en 45 Hz y una
   campana de +2 dB y Q 1.5 en 900 Hz (los armónicos que hacen que el 808 se oiga en un celular).
3. Enciende **AUTO GAIN** en los dos lados (es un ajuste más de cada lado: si lo enciendes antes de pasar a B, B nace
   con él).
4. Pon el loop y alterna A y B cada 4 compases. Escucha el kick, no el 808: ¿en cuál pega más el kick sin que el 808
   pierda peso?
5. Quédate con la ganadora (si quieres empezar otra variación desde ella: "A → B" o "B → A").

Por qué funciona: comparas dos ideas completas, al instante y al mismo volumen. Sin A/B, comparas una idea con el
recuerdo de la otra, y el recuerdo siempre pierde.

### Reto sin receta

En el bus de la percusión del dembow (timbal, hi-hats, shakers), usa SOLO para encontrar **una sola zona** que, al
bajarla −2 a −4 dB, haga que la voz se entienda mejor sin que la percusión pierda energía. Decide con A/B (A = sin la
banda, B = con ella) y AUTO GAIN encendido. Pistas (si no sale): la voz se entiende por los 2–4 kHz; busca con SOLO qué
instrumento de percusión tiene más energía justo ahí; Q entre 1.5 y 3, no más angosto; si al bajar la zona la
percusión "se va para atrás", es demasiado: prueba −2 dB; compara siempre en el contexto de la mezcla completa, no con
el bus solo.

### Vocabulario

- **Sesgo del volumen (loudness bias)**: preferir lo más fuerte creyendo que es lo mejor.
- **Igualar el volumen (level matching / gain matching)**: comparar dos versiones a la misma sonoridad. Lo hacen los
  ingenieros de mastering antes de cualquier decisión.
- **LUFS**: unidad de sonoridad percibida (la que usan Spotify y YouTube para normalizar); el auto gain usa su mismo
  filtro de peso.
- **Solo / escuchar la banda**: oír solo la zona que toca un filtro.
- **Realzar y barrer (boost and sweep)**: subir una banda angosta y moverla para encontrar resonancias.
- **Polaridad**: el signo de la onda (+/−). Se confunde con "fase", pero invertir la polaridad es igual en todas las
  frecuencias; la fase de un EQ cambia según la frecuencia.
- **Balance**: el reparto entre izquierda y derecha de una pista estéreo.
- **A/B**: comparar dos versiones saltando de una a otra.

### Errores comunes (y cuándo NO usarlo)

- **Dejar el SOLO puesto al exportar.** Fathom lo apaga al cerrar la ventana justamente por eso, pero con la ventana
  abierta suena en solo: mira el aviso amarillo del visor antes de renderizar.
- **Ecualizar en solo.** El SOLO es para **encontrar**; la decisión se toma escuchando la mezcla. Una voz ecualizada en
  solo suele quedar perfecta sola y perdida en la mezcla.
- **Usar el AUTO GAIN como volumen final sin pensarlo.** Es para comparar. Cuando decidas, puedes dejarlo encendido o
  apagarlo y ajustar la GANANCIA a mano según lo que pida la mezcla.
- **Confiar en el auto gain con cortes extremos.** Es un cálculo para una mezcla "típica": en un bajo solo, cortar los
  graves baja la sonoridad mucho más de lo que estima. Ahí iguala a oído (o con `scripts/level_match.py`).
- **Invertir la polaridad "porque sí".** En una pista sola no hace nada; solo tiene sentido cuando se suma con otra
  pista parecida.
- **Comparar A/B con distinto volumen.** Sin AUTO GAIN (o sin igualar a mano), el lado más fuerte gana siempre.
- **Balancear con el EQ en vez de con el mixer.** El BALANCE de Fathom es para corregir un desbalance de la propia
  pista; la ubicación en la mezcla se hace con el paneo del mixer de FL.

## Fase 7 — Respuesta analógica: que los agudos no se "aplasten"

Un EQ digital trabaja con muestras: a 44.1 kHz solo existen las frecuencias hasta 22.05 kHz (**Nyquist**, la mitad
de la frecuencia de muestreo). El analógico no tiene ese techo. La forma clásica de pasar un filtro analógico a
digital (la **bilineal**, la que usaba Fathom hasta la Fase 6) mete *todo* el eje analógico, de 0 a infinito, entre 0 y
Nyquist: abajo casi no se nota, pero arriba todo se aprieta. Esta fase agrega la **respuesta analógica**: las bandas
suenan como su prototipo analógico también cerca de Nyquist, sin latencia. El botón ANALÓGICA (cabecera) alterna
entre las dos para que lo veas y lo oigas.

### Conceptos aprendidos

- **El "aplastamiento" (cramping) de la bilineal.** La bilineal "dobla" el eje de frecuencias: lo que en el analógico
  está en el infinito, en el digital cae justo en Nyquist. Consecuencias que se ven en el visor con ANALÓGICA apagado:
  - una **campana aguda** (12 kHz a 44.1) sale más angosta que la que pediste y su lado de arriba se cae a 0 dB justo
    en 22 kHz (en el analógico seguiría arriba): suena menos "aire" del que esperabas;
  - un **shelf de agudos** aprieta su subida: empieza más tarde (debajo de su frecuencia sube menos que el analógico)
    y llega antes a la meseta (encima sube más), así que la subida es más brusca de lo que dibujaste;
  - un **corte de agudos** se hunde a −∞ en Nyquist (más empinado de lo pedido en la última octava).
  Con 96 kHz el efecto es mucho menor (Nyquist está en 48 kHz, lejos de lo audible); con 44.1/48 kHz, que es lo normal
  en FL, se nota de ~8 kHz para arriba.
- **Dos soluciones.** (1) **Sobremuestrear** (procesar a ×2 o ×4 para alejar Nyquist): funciona, pero cuesta ×4 de
  CPU en 24 bandas y los filtros del sobremuestreo agregan latencia o desfase. (2) **Diseñar el filtro "igualado"
  (matched)**: a la misma frecuencia de muestreo, elegir los números del filtro para que su volumen en cada frecuencia
  sea el del analógico. Fathom usa (2), la idea de Martin Vicanek ("Matched Second Order Digital Filters", 2016):
  - las **resonancias** (los polos) se copian tal cual del analógico (z = e^(s·T): la misma frecuencia y el mismo
    amortiguamiento, sin doblar el eje);
  - el resto (los ceros) se elige para que el volumen coincida con el analógico en unos puntos clave: en DC, en la
    frecuencia de la banda y en Nyquist. La campana usa "DC, el pico y que el pico no se corra".
  Lo demás no cambia: es el mismo filtro (el SVF) con otros números, así que sigue siendo estable al moverlo, sin
  clics, y la curva del visor sigue siendo la respuesta REAL del audio.
- **Cuánto mejora (medido contra el analógico, de 20 Hz a 20 kHz).** A 96 kHz, una campana se equivoca 0.06 dB en
  vez de 2.4 dB, y un shelf 0.3 dB en vez de 3 dB. A 44.1 kHz con la banda en 15–20 kHz (lo más difícil: Nyquist queda a
  menos de media octava) el error baja de 13–17 dB a unos 2.5 dB. Un corte de agudos de 96 dB/oct en 15 kHz ya no se
  hunde 120 dB de más al llegar a Nyquist.
- **Lo que no se puede.** Encima de Nyquist no hay nada: una banda en 25 kHz a 44.1 kHz se queda en el tope (0.49·fs),
  igual que antes. Y la fase es la de un filtro "de fase mínima" con ese volumen: casi la del analógico, no idéntica
  arriba (la fase "natural" es la Fase 10).
- **En graves no cambia nada.** Bajo ~2–5 kHz la bilineal ya era casi exacta: las dos respuestas suenan igual. Por eso
  la diferencia está en las bandas agudas, el aire y los cortes de agudos.

### Qué hace cada control (y qué muestra la pantalla)

- **ANALÓGICA** (cabecera, a la izquierda de A/B; encendido por defecto): encendido = respuesta analógica; apagado = la
  bilineal clásica de las Fases 2-6. Cambia las 24 bandas a la vez, con un fundido de 20 ms entre los dos diseños (sin
  clic, sin hueco). Es un parámetro: entra al DESHACER, al A/B y a la automatización, y se guarda con el proyecto.
  Un proyecto de las Fases 1-6 se abre con ANALÓGICA encendido (suena distinto solo en las bandas agudas); para oírlo
  como antes, apágalo.
- **La curva** del visor cambia con el botón: con una campana de +12 dB en 12 kHz vas a ver que la bilineal "dobla" su
  lado de arriba hacia 0 dB y la analógica lo deja seguir.

### Ejercicio de escucha guiado en FL Studio

Pon el proyecto en 44.1 o 48 kHz (Opciones → Audio: la frecuencia de muestreo).
1. **Ver el aplastamiento.** En una pista vacía (sin audio) crea una campana de +12 dB, Q 1, en 12 kHz. Alterna
   ANALÓGICA mirando la curva: el lado derecho de la campana cambia; el izquierdo, casi nada. Repite con la campana en
   3 kHz: casi no cambia (por eso solo importa arriba).
2. **Oír el aire.** En una voz o en el bus de los platillos: shelf de agudos +4 dB en 10 kHz, Q 0.71. Con A/B: A con
   ANALÓGICA encendido, B apagado (todo lo demás igual) y AUTO GAIN encendido en los dos. Alterna: con la analógica la
   subida es gradual (ya se nota un poco en 7–9 kHz, como en un EQ de consola); con la bilineal se concentra más
   arriba, más brusca. Es sutil: escúchalo con audífonos, no en las bocinas de la laptop.
3. **El corte de agudos.** En el master de un beat: corte de agudos de 48 dB/oct en 16 kHz. Mira el analizador de
   salida con ANALÓGICA encendido y apagado: con la bilineal, lo último del espectro se desploma; con la analógica cae
   con la pendiente pedida.
4. **El control.** Mismo ejercicio 2 pero con el shelf en 300 Hz (de graves): no deberías oír ninguna diferencia. Si la
   "oyes", es tu expectativa (por eso conviene alternar a ciegas).

### Receta: aire en la voz sin que se vuelva áspera

1. En la voz (después del autotune), shelf de agudos +3 dB en 12 kHz, Q 0.71, con ANALÓGICA encendido.
2. Si suena áspera, el problema casi nunca es el aire (12 kHz+), sino los 5–8 kHz (las "eses"): baja una campana de
   −2 dB, Q 2, donde el analizador marque las eses (agarra el pico con el mouse, Fase 5).
3. Compara con AUTO GAIN y A/B contra la voz sin EQ.

Por qué funciona: el "aire" de una voz se oye como una subida suave y ancha. Con la respuesta analógica el shelf
tiene la forma que dibujaste (gradual, empieza a sumar desde varios kHz antes); con la bilineal la subida se aprieta
contra Nyquist y suele hacer falta más ganancia para el mismo efecto, y más ganancia arriba es más aspereza. Y la
aspereza real casi siempre vive más abajo (las eses): se corrige ahí, no quitando el aire.

### Reto sin receta

En el bus de los hi-hats del dembow, logra que brillen más **sin subir los 6–8 kHz** (donde ya están fuertes), usando
solo bandas por encima de 10 kHz. Compara tu resultado con ANALÓGICA encendido y apagado: ¿con cuál necesitaste menos
ganancia para el mismo brillo? Pistas (si no sale): una campana ancha (Q 0.5–0.7) en 14–16 kHz o un shelf en 12 kHz;
mira en el visor dónde empieza a subir la curva en cada modo; con la bilineal la subida empieza más tarde y es más
angosta; iguala el volumen con AUTO GAIN antes de decidir.

### Vocabulario

- **Nyquist**: la mitad de la frecuencia de muestreo; la frecuencia más alta que existe en un audio digital (22.05 kHz
  a 44.1 kHz).
- **Transformada bilineal**: la forma clásica de convertir un filtro analógico a digital; exacta en graves, aprieta los
  agudos.
- **Cramping ("aplastamiento")**: la deformación de la bilineal cerca de Nyquist (campanas más angostas, que caen a
  0 dB en Nyquist).
- **Filtro igualado (matched)**: un filtro digital diseñado para que su volumen coincida con el del analógico.
- **Polos y ceros**: los "números" de un filtro; los polos dicen dónde resuena y cuánto, los ceros dónde tira hacia
  abajo o empuja hacia arriba.
- **Sobremuestreo (oversampling)**: procesar a una frecuencia de muestreo más alta (Riptide lo usa para la
  saturación).
- **"Analog-matched", "zero latency"**: así llaman muchos EQ profesionales a esta respuesta (por ejemplo, el modo de
  latencia cero de los EQ de FabFilter apunta a lo mismo).

### Errores comunes (y cuándo NO usarlo)

- **Esperar que cambie el sonido en graves o medios.** Ahí las dos respuestas son iguales: si suena distinto, es el
  volumen o la expectativa.
- **Juzgar la diferencia sin igualar el volumen.** Con la bilineal, una campana aguda sube un poco menos de energía:
  la analógica puede parecer "mejor" solo por estar más fuerte. Compara con AUTO GAIN y A/B.
- **Usarlo para "arreglar" agudos que no existen.** Si la grabación no tiene nada sobre 16 kHz (un MP3, un sample
  viejo), ninguna respuesta va a crear aire: un shelf solo sube el ruido.
- **Cuándo apagarlo:** casi nunca. Tiene sentido si quieres que un proyecto viejo (Fases 1-6) suene exactamente como
  sonaba, o para comparar y aprender qué hace la bilineal.

## Fase 8 — Estéreo por banda: ecualizar el centro y el ancho por separado

Hasta ahora cada banda tocaba los dos lados igual. Pero una mezcla estéreo son dos cosas a la vez: lo que está en el
**centro** (voz, kick, 808, caja: suena igual en los dos parlantes) y lo que está **a los lados** (reverbs, dobles,
pads, guitarras abiertas: suena distinto en cada uno). Esta fase deja elegir, banda por banda, dónde actúa: en los
dos lados (**Estéreo**), en uno (**Izquierda** / **Derecha**), solo en el centro (**Mid**) o solo en el ancho
(**Side**). Y el analizador puede medir el centro, el ancho o los dos.

### Conceptos aprendidos

- **Mid y side.** Con los dos canales L y R se arman otras dos señales: M = (L + R) / 2 (lo que tienen en común) y
  S = (L − R) / 2 (en lo que difieren). Se vuelve atrás sin perder nada: L = M + S, R = M − S. Un sonido paneado al
  centro (L = R) tiene S = 0: el side no lo "ve". Un sonido solo en un lado aparece mitad en M y mitad en S. Una
  reverb ancha vive sobre todo en S.
- **Cómo lo hace Fathom.** Una banda mid filtra M y le suma a los dos lados lo que CAMBIÓ (d = M filtrada − M):
  L + d, R + d. Una side hace lo mismo con S pero al revés en R: L + d, R − d. Es exactamente lo mismo que convertir a
  M/S, filtrar y volver, pero si la banda no cambia nada (0 dB), d = 0 y el audio pasa intacto, bit a bit. Las bandas
  siguen en serie: puedes tener una mid y una side en la misma frecuencia, una subiendo y la otra bajando.
- **Cambiar de canal sonando** es como cambiar de forma: el filtro pasa a ver otra señal, así que la banda se funde a
  silencio en 20 ms, cambia y vuelve (sin clic).
- **Auto gain en estéreo.** Una banda side cambia menos la sonoridad que una estéreo (en una mezcla típica el side
  tiene unos 6 dB menos de energía que el centro). Fathom lo calcula con la respuesta real de cada canal sobre una
  "mezcla típica" (el side 6 dB por debajo del mid). Por eso una campana ancha de +6 dB en 2 kHz compensa −2.8 dB en
  estéreo, −2.4 en mid, −1.6 en un solo lado y solo −0.7 en el side.
- **En mono** no hay side: una banda side no hace nada, y izquierda, derecha y mid tocan el único canal.

### Qué hace cada control (y qué muestra la pantalla)

- **CANAL** (panel de la banda, y "Canal" en el menú del nodo con clic derecho, también para varias bandas elegidas):
  Estéreo / Izquierda / Derecha / Mid / Side. El nodo lleva su letra arriba a la derecha (L, R, M, S; estéreo, sin
  letra). Entra al DESHACER, al A/B y a la automatización.
- **Las curvas.** Si todas las bandas son estéreo, la curva blanca de siempre. Si hay bandas en un solo canal, una
  curva por canal con su letra al borde derecho: **M** blanca y **S** violeta (si hay bandas mid o side), **L** celeste
  y **R** rosada (si hay bandas izquierda o derecha). Cada una son las bandas estéreo más las de ese canal. La lectura
  de arriba muestra el valor de cada una bajo el mouse. (Si mezclas L/R con M/S, las curvas son una guía: una banda
  izquierda también mueve un poco el mid).
- **SOLO** de una banda mid o side se escucha en su canal: la zona del centro (en los dos lados) o la del ancho (sin
  nada en el centro). Una izquierda se escucha solo a la izquierda.
- **CANALES** (columna del analizador, arriba de INCLINACIÓN): **Mid (L+R)** = lo de siempre; **Side (L−R)** = la
  entrada, la salida y el sidechain medidos en el side (aviso violeta "ANALIZADOR: SIDE"); **Mid + Side** = todo en mid
  y además el side de la salida en una línea violeta encima. Se guarda con el proyecto.

### Ejercicio de escucha guiado en FL Studio

Usa una mezcla o un beat con reverb y algo abierto (pads, coros, una guitarra doblada).
1. **Ver dónde está el ancho.** Analizador en Mid + Side. Mira la línea violeta: ¿dónde está por debajo del relleno
   azul y dónde se acerca? En los graves debería estar muy abajo (el 808 y el kick van al centro); en los agudos, más
   cerca (reverb, hi-hats abiertos, platillos).
2. **Oír solo el ancho.** Crea una campana ancha (Q 0.5) en 3 kHz, CANAL = Side y SOLO: escuchas solo lo que es
   distinto entre los lados en esa zona: reverb, dobles, el ambiente. Cambia a Mid con SOLO: ahora el centro (la voz
   principal, la caja).
3. **Abrir sin tocar la voz.** Shelf de agudos +3 dB en 8 kHz en Side, con AUTO GAIN. Alterna con BYPASS: la mezcla se
   abre (más ancha y brillante a los lados), pero la voz del centro queda igual. Ahora el mismo shelf en Estéreo: la voz
   también se vuelve más brillante (y quizá más áspera).
4. **Firmeza en mono.** Corte de graves de 12 dB/oct en 150 Hz en Side. En FL, pon el master en mono (el botón de mono
   del mixer) y compara con y sin la banda: sin ella, los graves "anchos" se cancelan en mono y cambian de volumen; con
   ella, el bajo es el mismo en mono y en estéreo.

### Receta: el bus de la música del reggaetón (sin la voz)

1. **Graves al centro:** corte de graves en Side, 120–150 Hz, 12 dB/oct. El 808 y el kick quedan firmes en cualquier
   sistema (club, celular, mono).
2. **Barro del ancho:** si los pads o la reverb ensucian 250–400 Hz, una campana −2 dB, Q 1, en Side (no en el centro:
   ahí está el cuerpo de la caja).
3. **Espacio para la voz:** campana −1.5 a −2 dB, Q 1, en 2–4 kHz en Mid: abre un hueco en el centro, justo donde va a
   ir la voz, sin quitarle brillo a lo que está a los lados.
4. **Aire a los lados:** shelf de agudos +2 dB en 10 kHz en Side.
5. Compara con AUTO GAIN y A/B; mira el analizador en Mid + Side antes y después.

Por qué funciona: la voz principal y el bajo van al centro y necesitan su espacio ahí; lo que da sensación de tamaño
está a los lados. Ecualizar cada parte por separado deja hacer cambios grandes en una sin dañar la otra.

### Reto sin receta

Tienes un pad estéreo que suena ancho y bonito solo, pero en la mezcla tapa la voz. Haz que deje de tapar la voz
**sin que el pad pierda su ancho** (no vale bajarle el volumen al pad). Pistas (si no sale): ¿la voz está en el centro
o a los lados? Mira el pad con el analizador en Mid y en Side: ¿cuánto del pad está en el centro en 1–4 kHz? Prueba
una campana en Mid sobre el pad y deja el Side intacto; escucha con SOLO dónde vive lo que molesta.

### Vocabulario

- **Mid / Side (M/S)**: el centro (L + R) / 2 y el ancho (L − R) / 2 de una señal estéreo.
- **Imagen estéreo / ancho**: qué tan abierto suena algo entre los dos parlantes. Más side = más ancho.
- **Compatibilidad mono**: que la mezcla suene bien sumada a un solo canal (celulares, algunos clubes). Lo que está
  solo en el side desaparece en mono.
- **Cancelación de fase**: dos señales iguales con polaridad opuesta se anulan; es lo que le pasa al side al sumar en
  mono (L + R: el side se cancela).
- **Ejemplo real**: en el reggaetón comercial el 808, el kick y la voz principal están casi 100 % en el mid; los coros,
  las reverbs y los sintes de fondo le dan el ancho. En mastering es común cortar los graves del side (debajo de
  100–200 Hz) para que los clubes y el mono funcionen.

### Errores comunes (y cuándo NO usarlo)

- **Subir mucho el side** para "ensanchar": suena grande en audífonos, pero en mono (celular) esa parte desaparece y
  la mezcla cambia de balance. Revisa siempre en mono.
- **Ecualizar en M/S una pista mono** (una voz, un kick sin estéreo): no tiene side; una banda side no hace nada y una
  mid es lo mismo que una estéreo.
- **Cortes profundos en el side de una voz doblada**: los dobles viven en el side; quitarlos ahí cambia el arreglo, no
  solo el tono.
- **Usar L / R para "arreglar" un paneo**: si algo está corrido, se arregla con el paneo. L / R sirve para tomas
  realmente distintas de cada lado (dos micrófonos, una guitarra doblada con un lado más oscuro).
- **Juzgar sin igualar el volumen**: un side más brillante suena "mejor" por ser más fuerte; usa AUTO GAIN y A/B.

## Fase 9 — EQ dinámico: bandas que se mueven solas

Un EQ normal es una foto: si cortas 4 dB en 3 kHz, corta 4 dB **siempre**, aunque la voz solo se ponga chillona en
las notas altas. Lo demás del tiempo le estás quitando presencia sin necesidad. Un EQ dinámico corta (o sube) **solo
cuando hace falta**: cada banda escucha su propia zona y se mueve según cuánto suena. Es un compresor que mira y toca
una sola franja del espectro.

### Conceptos aprendidos

- **Estático vs. dinámico vs. compresor multibanda.** El EQ estático cambia el tono siempre igual. El compresor
  multibanda parte la señal en bandas fijas (con crossovers) y comprime cada una: bandas anchas, pensado para controlar
  el nivel. El EQ dinámico es una **campana (o un shelf) con detector**: la forma, el ancho y la frecuencia son los de
  un EQ, sin partir la señal; ideal para un problema puntual (una resonancia, las eses, un grave que se acumula).
- **El detector.** Cada banda dinámica escucha su **zona**: la misma que oyes con SOLO (un pasa banda con su frecuencia
  y su Q en una campana; lo de abajo en un shelf de graves; lo de arriba en uno de agudos; los tilts escuchan todo).
  Mide el nivel de esa zona con un **RMS corto** (dos ciclos de lo más grave de la zona, de 1 a 20 ms: lo justo para que
  no ondule con cada vuelta del seno) más 3 dB, para que un seno lea su pico, como el medidor de FL.
- **Umbral y rango.** Con la zona debajo del **UMBRAL**, la banda queda en su **GANANCIA** (lo normal: 0 dB). Cuando la
  zona lo pasa, la banda se aleja hacia el **RANGO**, **1 dB por cada dB que la zona pasa el umbral**, hasta llegar al
  rango. Con rango −6 la zona queda "pegada" al umbral hasta que hace falta cortar más de 6 dB. Alrededor del umbral hay
  una **rodilla suave** de 6 dB: la banda entra de a poco, no salta al cruzarlo. Un rango positivo **sube** la zona
  cuando suena fuerte (que algo aparezca solo en los golpes).
- **Ataque y liberación sobre la ganancia.** El ataque es lo que tarda la **banda** en llegar al 63 % de su corte; la
  liberación, lo que tarda en volver. Se aplican a la ganancia en dB, no al nivel. Se probó primero el seguidor de
  envolvente de la librería (que suaviza el nivel en lineal) y apareció un problema que vale la pena entender: con el
  umbral muy por debajo de la señal, el nivel lo cruza casi al instante y una banda con 10 ms de ataque cortaba 12 dB
  en menos de 1 ms (un clic). Suavizando la ganancia, "10 ms" significa 10 ms siempre, esté el umbral a 3 o a 30 dB.
- **Umbral automático.** El umbral se pone solo 3 dB por encima del **nivel típico** de la zona (el promedio de los
  últimos ~1.5 s, sin contar los silencios). Lo sostenido no se toca; solo actúan los momentos en que la zona suena más
  fuerte que de costumbre. Si toda la pista sube (el coro más fuerte que la estrofa), el umbral la sigue.
- **Sidechain.** El detector puede escuchar **otra pista** (el sidechain) en vez de la propia. Así una banda de la pista
  A se mueve cuando suena la pista B: tallar espacio solo cuando hace falta.
- **Cómo se mueve el filtro sin clics.** La ganancia nueva llega cada 16 muestras (0.36 ms a 44.1 kHz). Recalcular el
  diseño del filtro en cada muestra para 24 bandas costaría mucho; en cambio se calcula en ese punto y en las 16
  muestras siguientes se interpolan los coeficientes desde los de antes. Los tests muestran la diferencia: aplicando cada
  valor de golpe, la señal sale entre 7 y 15 veces más "puntiaguda".
- **Lo que NO se mueve**: el auto gain compensa la ganancia propia de la banda, no la dinámica (compensar cada
  movimiento haría "respirar" el volumen), y el SOLO escucha la zona de siempre.

### Qué hace cada control (y qué muestra la pantalla)

La fila nueva del panel de la banda (debajo de la de siempre):

- **DINÁMICO**: enciende la dinámica de la banda. Solo las formas con ganancia (campana, shelves, tilts); en los cortes,
  el notch, el pasa banda y el all-pass se atenúa. También está en el menú del nodo (clic derecho → "EQ dinámico",
  para varias bandas elegidas a la vez).
- **El medidor** (debajo de DINÁMICO): la barra es el nivel de la zona ahora (de −60 a +6 dB); gris debajo del umbral,
  del color de la banda lo que lo pasa. La raya blanca es el umbral (con AUTO, el que calculó: "auto −17.2 dB"). A la
  derecha, cuánto se está moviendo la banda en este momento.
- **DETECTOR**: Banda (escucha la propia pista) o Sidechain (otra pista; en FL, "Sidechain to this track" y el puerto en
  Processing). Sin sidechain conectado aparece el aviso "DINÁMICA CON SIDECHAIN: SIN CONECTAR" y la banda no se mueve.
- **AUTO** (debajo de UMBRAL): el umbral automático. La perilla UMBRAL se atenúa: no se usa.
- **RANGO** (−30 a +30 dB, −6 por defecto): hasta dónde se mueve, sumado a la GANANCIA. La escala de ganancia de la
  salida también lo escala (al 50 %, la mitad).
- **UMBRAL** (−60 a +6 dB, −20 por defecto), **ATAQUE** (0.5–300 ms, 10 por defecto) y **LIBERACIÓN** (5 ms–3 s, 150
  por defecto).
- **En el visor**: el nodo se queda donde descansa la banda (su ganancia) y una **línea vertical** baja (o sube) hasta
  donde la lleva el rango, con una raya al final. Cuando se mueve, la curva y el relleno de la banda son los de
  **ahora** (lo que suena), la curva en reposo queda **punteada** y un **punto blanco** sobre la línea marca dónde está.
  La etiqueta del nodo dice "din −6.0 dB (ahora −2.3 dB)".
- Todo entra al DESHACER, al A/B y a la automatización. Un proyecto de las Fases 1-8 suena igual (la dinámica nace
  apagada). Latencia: 0.

### Ejercicio de escucha guiado en FL Studio

Usa una voz con autotune que tenga eses marcadas (o un hi-hat brillante) y AUTO GAIN encendido.
1. **Encontrar el problema.** Campana en 7 kHz, Q 4, SOLO, y barre la frecuencia entre 5 y 10 kHz hasta que las eses
   suenen más fuertes. Apaga SOLO.
2. **Corte estático.** GANANCIA −6 dB, sin dinámica. Las eses bajan, pero escucha las vocales: la voz entera perdió
   brillo y suena más apagada.
3. **Corte dinámico.** GANANCIA 0 dB, DINÁMICO encendido, RANGO −6, ATAQUE 1 ms, LIBERACIÓN 60 ms. Baja el UMBRAL hasta
   que el medidor se pinte de color solo en las eses. Mira la línea del nodo y el punto blanco: se mueve solo en cada
   "s". Compara con el paso 2 (A/B): las eses bajan igual, pero las vocales conservan su brillo.
4. **Ataque.** Sube el ATAQUE a 30 ms: el principio de cada "s" se escapa antes de que la banda corte. Ahí está la
   diferencia entre agarrar un golpe y domar lo que sigue.
5. **AUTO.** Enciende AUTO y canta (o pon) una parte más fuerte y una más suave: el umbral sigue al nivel y la banda
   actúa parecido en las dos.

### Receta: el 808 y el kick del dembow sin pelearse

El kick y el 808 viven en los mismos 50–100 Hz. Cortar ese grave del 808 todo el tiempo lo deja flaco; cortarlo solo
cuando pega el kick deja que cada uno tenga su momento.
1. En la pista del 808, Fathom; en FL, manda el kick como sidechain a esa pista ("Sidechain to this track") y elige el
   puerto del sidechain en Processing.
2. Analizador con SIDECHAIN visible: busca el pico del kick (normalmente 50–70 Hz). Campana en esa frecuencia, Q 1.5.
3. DINÁMICO, DETECTOR = Sidechain, RANGO −6, ATAQUE 2 ms, LIBERACIÓN 80 ms (lo que dura el golpe del kick).
4. UMBRAL: bájalo hasta que el medidor se pinte en cada golpe del kick.
5. Compara con BYPASS: el kick atraviesa el 808 en cada golpe y el 808 conserva todo su peso entre golpes.

Por qué funciona: en vez de dejarle al kick un hueco permanente (que el oído nota como un 808 sin cuerpo), el hueco
aparece solo los ~80 ms en que el kick lo necesita. Es la misma idea del sidechain de volumen de Ebb, pero solo en la
zona donde chocan: el resto del 808 (sus armónicos, lo que se oye en el celular) no se agacha.

### Reto sin receta

Un 808 largo resuena de más en una sola nota de la melodía (una nota retumba y las otras no). Haz que esa nota quede
pareja con las demás **sin bajarle los graves a las otras notas**. Pistas (si no sale): ¿en qué frecuencia retumba?
(agarra el pico en el analizador mientras suena esa nota y mira su nota de FL). ¿Qué pasa si la campana es estática?
¿Y si es dinámica con el umbral justo encima del nivel de las otras notas? Prueba AUTO: ¿la nota que retumba pasa el
nivel típico?

### Vocabulario

- **EQ dinámico**: una banda de EQ cuya ganancia depende del nivel de su zona.
- **Umbral (threshold)**: el nivel desde el que empieza a actuar.
- **Rango (range)**: cuánto puede moverse como máximo.
- **Rodilla (knee)**: la zona alrededor del umbral donde la acción entra de a poco.
- **Ataque / liberación (attack / release)**: lo que tarda en actuar y en soltar.
- **RMS**: el nivel "promedio" de una señal (la raíz del promedio de los cuadrados); se parece más a lo que oímos que el
  pico.
- **De-esser**: un EQ dinámico (o compresor) que solo mira las eses (5–9 kHz) de una voz.
- **Sidechain**: usar una señal para controlar el procesamiento de otra.
- **Ejemplo real**: en las voces de reggaetón con autotune casi siempre hay un de-esser (las eses se vuelven ásperas
  con la compresión y el autotune); en el master, los ingenieros usan bandas dinámicas en 200–400 Hz para que el barro
  que se acumula en los coros no se quede todo el tema.

### Errores comunes (y cuándo NO usarlo)

- **Umbral demasiado bajo**: la banda actúa todo el tiempo y es un EQ estático con más pasos (y que "respira"). El
  medidor debería pintarse de color solo en los momentos problema.
- **Ataque y liberación muy cortos en graves**: con 0.5 ms y 5 ms en 60 Hz, la banda sigue cada ciclo del 808 y lo
  distorsiona. En graves, ataques de 5–20 ms y liberaciones de 100 ms o más.
- **Rango enorme**: −20 dB en una campana angosta deja un hueco que suena a "filtro" cuando entra. Empieza con −3 a −6.
- **Usarlo para lo que está mal siempre**: si una resonancia suena en todas las notas, un corte estático es más simple y
  más transparente.
- **Juzgar sin igualar el volumen**: el corte dinámico baja el nivel en los momentos fuertes y "suena más suave"; usa
  A/B con AUTO GAIN y escucha el detalle, no el volumen.
- **Sidechain sin conectar**: la banda no se mueve (mira el aviso en el visor).

## Fase 10 — Modos de fase: qué cambia un EQ además del volumen

La curva dice cuánto sube o baja cada frecuencia. Pero un filtro también **retrasa** cada frecuencia un poco distinto:
eso es la **fase**. Casi nunca se oye en una pista sola; se oye cuando esa señal se suma con otra parecida (capas del
mismo sonido, micrófonos de una batería, un bus) o en el golpe de un kick, que es la suma de muchas frecuencias que
tienen que llegar juntas. Fathom ahora tiene tres maneras de tratar la fase, con la misma curva.

### Conceptos aprendidos

- **Fase mínima (CERO LATENCIA).** Los filtros de las bandas (el SVF, como un EQ analógico) responden al instante, pero
  alrededor de lo que tocan unas frecuencias salen un poco antes que otras. Una campana de +6 dB en 100 Hz "corre" un
  poco los graves respecto de los medios. Es lo que hacen todas las consolas y casi todos los EQ: no tiene nada de malo.
- **Fase lineal.** Un filtro **FIR simétrico** con la misma magnitud: todas las frecuencias se retrasan **lo mismo**
  (la mitad del largo del filtro). La forma de onda no se deforma; solo llega más tarde y FL compensa ese retardo
  (por eso Fathom le **informa la latencia** y el bypass la conserva). Se diseña por **muestreo en frecuencia**: se
  pide la magnitud en una grilla de frecuencias, una FFT inversa da la respuesta al impulso, se la centra y se le pone
  una **ventana de Hann** (que la apaga suave en los bordes: cortarla de golpe dejaría ondulaciones en la curva).
- **Convolución por FFT particionada.** Aplicar un FIR de 4096 muestras "a mano" son 4096 multiplicaciones por
  muestra. En frecuencia la convolución es un producto: con la FFT es cientos de veces más barato. Para no esperar
  todo el filtro antes de tener salida, se parte en trozos de 512 muestras; cada bloque de entrada se transforma una
  vez y se multiplica por cada trozo (contra la entrada de hace 1, 2, 3... bloques). La latencia es la mitad del FIR
  más un bloque. Esta pieza quedó en la librería (`Convolver.h`): la reverb la va a usar.
- **El precio de la fase lineal: pre-eco (pre-ringing).** Una campana angosta "suena" un rato (resuena) después de un
  golpe. En fase mínima ese sonido va **después**; en fase lineal, como el filtro es simétrico, la mitad va **antes**
  del golpe: un "chorro" o "aspirado" que se oye antes del kick. Medido con una campana −12 dB, Q 8, 100 Hz: la energía
  antes del golpe es −38 dB en lineal, −74 dB en natural y nada en cero latencia.
- **La calidad es la resolución en graves.** Un FIR de N muestras no puede dibujar detalles más finos que ~fs/N Hz. Una
  campana de +9 dB, Q 4 en 40 Hz llega a +2.9 dB con calidad BAJA (2048), +4.7 MEDIA, +6.6 ALTA, +8.0 MUY ALTA y +8.7
  MÁXIMA (32768). Por eso la curva del visor en fase lineal es la del **FIR de verdad**: vas a ver la campana "aplastada"
  en los graves si la calidad no alcanza.
- **Fase natural.** La magnitud **y** la fase de un EQ analógico. El filtro "matched" de la Fase 7 ya tiene la magnitud
  analógica casi hasta Nyquist, pero su fase se aparta en los agudos (medido: hasta 120° con una campana en 12 kHz y un
  corte de agudos a 44.1 kHz). Fathom le suma después un **FIR corto de corrección** = analógico ÷ digital: lejos de
  Nyquist vale 1 (no hace nada) y cerca corrige lo que falta. Como es suave, alcanzan 256 muestras: 3.6 ms de latencia
  y casi sin pre-eco. Medido contra el prototipo analógico: menos de 0.07 dB y 0.4° en todo el rango.
- **Mid/side y L/R en fase lineal.** Como en el auto gain, cada frecuencia es una matriz 2×2 sobre (L, R): con bandas
  mid o side hacen falta cuatro FIR (L→L, R→L, L→R, R→R). En fase lineal el mid y el side se filtran sin cambiarles la
  fase; en cero latencia el mid filtrado vuelve con la fase de la banda y se suma un poco distinto con el side intacto.
- **EQ dinámico en fase lineal.** El detector escucha la entrada retrasada hasta el centro del FIR (así la ganancia
  cambia cuando llega el golpe, no medio filtro antes) y el FIR se rediseña con la ganancia nueva unas decenas de veces
  por segundo, fundiéndose de uno a otro. Escucha la entrada del EQ, no lo que dejaron las bandas anteriores.
- **Cambiar de modo sin clic.** Cada modo tiene otra latencia: no hay forma de que la salida quede "pegada". El modo
  nuevo arranca por su cuenta sin oírse (se llena su filtro) y después se funde del viejo al nuevo en 20 ms con una
  curva en S (si el fundido fuera una recta, quedaría un quiebre al empezar). FL recibe la latencia nueva.
- **Los FIR se diseñan en otro hilo.** Diseñar uno grande cuesta milisegundos; en el hilo de audio sería un corte. El
  audio pide el diseño y sigue; cuando llega, lo funde. Al renderizar (offline) se diseña ahí mismo: el render sale
  siempre igual. Y la memoria (15-35 MB) recién se reserva cuando eliges natural o lineal: con cero latencia, Fathom
  sigue igual de liviano en cada pista.

### Qué hace cada control (y qué muestra la pantalla)

Al final de la columna de SALIDA:

- **FASE**: Cero latencia (por defecto: un proyecto de las Fases 1-9 suena igual) / Natural / Lineal.
- **CALIDAD** (solo en lineal): Baja 35 ms, Media 58 ms (por defecto), Alta 104 ms, Muy alta 197 ms, Máxima 383 ms de
  latencia a 44.1/48 kHz (a 88.2/96, el doble de muestras: el mismo tiempo).
- Debajo, **la latencia** que agrega el modo ("sin latencia" / "latencia 58 ms"). FL la compensa sola en la mezcla.
- **En el visor**: con fase lineal o natural aparece "FASE LINEAL" / "FASE NATURAL" arriba a la izquierda y la curva
  blanca es la del FIR que suena (en lineal con poca calidad, mira los graves). Con curvas por canal (L/R o M/S), esas
  curvas siguen siendo las de las bandas (lo avisa el texto).
- **ANALÓGICA** se atenúa en fase natural: ahí las bandas son siempre las analógicas.
- CPU (5 bandas, estéreo, 44.1 kHz): cero latencia 0.3 % de un núcleo, natural 0.9 %, lineal de 0.7 % (baja) a 2.1 %
  (máxima). Todo entra al DESHACER, al A/B y a la automatización (pero no automatices el modo: cada cambio mueve la
  latencia).

### Ejercicio de escucha guiado en FL Studio

Usa un kick con buen "golpe" (un dembow con kick seco sirve) y AUTO GAIN encendido.
1. **Pre-eco.** Campana en 60 Hz, −12 dB, Q 8 (angosta y profunda). En CERO LATENCIA escucha el golpe. Pasa a LINEAL,
   calidad MÁXIMA: antes de cada golpe aparece un "fffum" grave, como si el kick empezara antes. Ese es el pre-eco.
   Baja a calidad BAJA: el pre-eco se acorta, pero mira la curva: la campana ya no llega a −12 dB.
2. **Natural.** Lo mismo en NATURAL: el golpe suena como en cero latencia (sin pre-eco). La diferencia con cero
   latencia está en los agudos; para oírla, pasa a un hi-hat con un shelf de +6 dB en 10 kHz y alterna NATURAL/CERO
   LATENCIA con A/B: es sutil (la fase arriba de 10 kHz), y es normal no oír nada.
3. **Partir y volver a sumar: la prueba de nulo** (donde la fase lineal brilla). Pon el mismo bajo en TRES canales del
   mixer. A: Fathom con DOS cortes de graves de 12 dB/oct en 150 Hz (dos en serie = un Linkwitz-Riley de 24 dB/oct, el
   corte que suma plano). B: DOS cortes de agudos de 12 dB/oct en 150 Hz. C: Fathom sin bandas y con Ø POLARIDAD. A + B
   ya es el bajo partido en dos y vuelto a juntar; C le resta el original. En CERO LATENCIA no se anulan: queda un resto
   alrededor de 150 Hz (el volumen de A + B es el de antes, pero las dos mitades salen giradas de fase). Pasa A y B a
   LINEAL con la misma calidad (FL compensa la latencia de cada canal): silencio. Partir en fase lineal no le cambia
   nada al sonido; por eso se usa para procesar graves y agudos por separado y volver a sumarlos. (Con cortes
   Butterworth de 24 dB/oct, A + B tendría +3 dB en 150 Hz en los dos modos: no son complementarios.)

### Receta: el bus de batería con fase lineal

En un bus donde suenan kick, snare y percusión juntos, cortar graves con fase mínima corre la fase de lo que queda
cerca del corte y puede "ablandar" el golpe del kick cuando se suma con su propio sub en otra pista.
1. Fathom en el bus de batería, FASE = LINEAL, calidad MEDIA (58 ms; FL la compensa).
2. Corte de graves 24 dB/oct en 35 Hz (lo que no suena en ningún parlante).
3. Campana ancha −2 dB, Q 1, en 400 Hz (el "cartón" del bus).
4. Compara con CERO LATENCIA con A/B y AUTO GAIN: el kick conserva el mismo frente de onda.

Por qué funciona: con fase lineal, quitar graves no corre en el tiempo lo que queda; el kick del bus y el sub que va
por otro lado siguen alineados. Y con cortes anchos y suaves, el pre-eco casi no existe (lo produce lo angosto y
empinado).

### Reto sin receta

Tienes un 808 con un corte de graves de 96 dB/oct en 30 Hz y te parece que el golpe del principio se "ensucia". ¿Es
el corte o es la fase? Averígualo sin cambiar la curva. Pistas (si no sale): ¿qué cambia entre CERO LATENCIA, NATURAL y
LINEAL si la curva es la misma? ¿En cuál aparece algo ANTES del golpe? ¿Qué pasa si bajas la pendiente a 24 dB/oct en
cada modo?

### Vocabulario

- **Fase**: cuánto se retrasa cada frecuencia al pasar por el filtro.
- **Fase mínima**: el filtro que responde al instante (todo IIR "normal", como un EQ analógico).
- **Fase lineal**: todas las frecuencias se retrasan lo mismo; la forma de onda no cambia.
- **FIR / IIR**: filtro de respuesta finita (una lista de números que se multiplica por la entrada) / infinita (con
  realimentación: el SVF).
- **Convolución**: aplicar un FIR (multiplicar y sumar con la entrada pasada); por FFT, en bloques.
- **Latencia y PDC**: el retardo que agrega un plugin; FL lo compensa en las demás pistas (Plugin Delay Compensation).
- **Pre-eco (pre-ringing)**: lo que un filtro de fase lineal hace sonar ANTES del golpe.
- **Ejemplo real**: en el mastering de música electrónica y urbana es común un EQ de fase lineal en el master para
  cortes anchos (y nunca para campanas angostas en graves); en las baterías grabadas con muchos micrófonos, la fase
  lineal evita que el EQ de un micrófono cambie cómo se suma con los otros.

### Errores comunes (y cuándo NO usarlo)

- **Fase lineal para todo**: en una pista sola, casi nunca suena mejor que cero latencia, y agrega latencia, CPU y
  pre-eco. Úsala cuando la señal se suma con otra parecida (capas, paralelo, M/S, buses).
- **Campanas angostas y cortes empinados en graves con fase lineal**: ahí el pre-eco es más largo y más audible. Para
  eso, cero latencia o natural.
- **Calidad baja con graves finos**: la curva te lo muestra; si la campana no llega, sube la calidad (o usa otro modo).
- **Fase lineal mientras grabas o tocas en vivo**: 58-383 ms de retardo se sienten. Para grabar, cero latencia.
- **Automatizar el modo de fase**: cada cambio mueve la latencia y FL tiene que reacomodarse; elige el modo y déjalo.
- **Esperar "mejor sonido" de la fase natural en graves**: cambia la fase solo en los agudos; en una pista de 808 suena
  igual que cero latencia (y está bien: el analógico y el digital ya coinciden ahí).

## Fase 11 — Dinámica espectral: cortar solo la frecuencia que sobra

El EQ dinámico de la Fase 9 mueve la banda **entera**: si una campana en 3 kHz baja porque una nota de la voz silba en
3.4 kHz, baja toda su zona, también lo que estaba bien. Una banda **espectral** decide frecuencia por frecuencia: la
campana marca DÓNDE mirar y hasta cuánto puede cortar (el RANGO); cada frecuencia corta según **su propio** exceso sobre
el UMBRAL. Es la herramienta para lo que cambia con las notas: la resonancia que aparece en una sola nota del 808, el
armónico que chilla en una vocal con autotune, la "o" de una voz que se llena de 400 Hz y la "i" que no.

### Conceptos aprendidos

- **STFT (transformada de Fourier de tiempo corto).** La señal se corta en tramos de 2048 muestras (46 ms a 44.1 kHz)
  que se solapan un 75 %: uno nuevo cada 512 muestras (11.6 ms, el "hop"). Cada tramo pasa por una ventana de Hann y una
  FFT: 1025 **bins**, cada uno una franja de 21.5 Hz. Es lo mismo que hace el analizador, pero aquí además se **toca**
  cada bin y se vuelve al tiempo.
- **El nivel de un bin.** 4·|X|/N es la amplitud de un seno que cae en ese bin: el mismo "pico de un seno" que mide la
  Fase 9 (un seno de −12 dBFS lee −12.04). Pero un **ruido** reparte su energía en cientos de bins y cada uno lee mucho
  menos que el ruido entero: el umbral de una banda espectral se compara con cada franja sola. Por eso suele ir más
  abajo que el de una dinámica normal.
- **La ventana reparte un seno en 3-4 bins.** El del centro y sus vecinos (6 dB más abajo). Si cada bin decidiera solo,
  el centro bajaría todo y los costados casi nada: el tono bajaría menos de lo que pide. Cada bin mira también a sus dos
  vecinos (el máximo de los tres) y todo el seno recibe la misma ganancia: con 6 dB sobre el umbral, el tono baja 6 dB.
- **Cuánto corta cada bin.** La curva de la Fase 9 (rodilla de 6 dB, 1 dB por dB hasta el rango) y ataque/liberación,
  pero de cada bin y una vez por tramo. Después se escala por la forma de la banda: en el centro de la campana el bin
  puede moverse todo el rango; en los flancos, lo que la campana movería ahí; lejos, nada. Si todos los bins van a fondo,
  la banda es exactamente la dinámica normal con todo el rango.
- **Volver al tiempo: overlap-add.** FFT inversa de cada tramo, otra ventana de Hann y se suman los tramos solapados.
  Con dos ventanas de Hann y un hop de N/4, las ventanas al cuadrado suman 1.5 en todas partes: sin cambios, la suma es
  la señal. La segunda ventana suaviza el borde de cada tramo: un cambio de ganancia de un tramo al siguiente se funde a
  lo largo de 46 ms, sin clic.
- **El precio: latencia y lentitud.** Un tramo necesita 2048 muestras antes de salir: **46 ms de latencia** (FL la
  compensa; se suma a la del modo de fase). Y la ganancia cambia como mucho cada 11.6 ms y "ve" 46 ms a la vez: para un
  transitorio (el ataque de un snare, una "t"), la dinámica normal es más rápida. A cambio, la espectral es **casi un
  lookahead**: el tramo que decide la ganancia es el mismo que se procesa.
- **Resolución en graves.** 21.5 Hz por bin y un seno ocupando ~86 Hz: debajo de ~150 Hz dos armónicos cercanos se
  mezclan. En el 808 funciona para la zona de 100-400 Hz (los armónicos que "zumban"), no para separar el sub de su
  primer armónico.
- **El detector escucha lo que llega a la banda.** Lo que sale de las bandas, menos la curva estática de la propia banda
  (así una campana de +3 dB espectral no se dispara con su propia subida), o el sidechain (retrasado como lo que sale de
  los modos de fase, para que los dos tramos sean del mismo instante).
- **Umbral automático, versión espectral.** Sigue el nivel típico de la zona entera (su energía, como el RMS de la Fase
  9) + 3 dB: solo actúan las frecuencias que, ellas solas, suenan más fuerte que toda la zona de costumbre. Lo que es
  "siempre así" se queda.
- **Bit a bit cuando no hace nada.** Mientras ninguna frecuencia pasa el umbral, la salida es la entrada retrasada 46 ms
  tal cual (no la suma de tramos, que se le parece hasta el redondeo). Medido en los tests: idéntica muestra a muestra.

### Qué hace cada control (y qué muestra la pantalla)

- **ESPECTRAL** (junto a DINÁMICO, en el panel de la banda; también "Dinámica espectral" en el menú del nodo, que
  enciende la dinámica de paso): la dinámica de la banda pasa a decidir por frecuencia. Solo campanas y shelves (los
  tilts inclinan todo: no tienen una zona que vigilar). Los demás controles de la dinámica significan lo mismo, por
  frecuencia: RANGO = cuánto puede moverse cada una; UMBRAL = desde qué nivel de UNA frecuencia; ATAQUE/LIBERACIÓN = de
  cada frecuencia (de a tramos de 11.6 ms: menos de 10 ms de ataque ya es "lo más rápido posible"); DETECTOR = la banda o
  el sidechain; AUTO = el nivel típico de la zona.
- **El medidor** muestra la frecuencia más fuerte del centro de la zona contra el umbral ("· por frecuencia") y, a la
  derecha, lo que más se mueve ahora alguna frecuencia.
- **En el visor**: la curva de la banda muestra lo que mueve cada frecuencia AHORA: "dientes" donde corta, con la curva
  en reposo punteada; la curva blanca total los incluye. El punto blanco sobre la línea del rango marca la frecuencia que
  más se movió. La etiqueta del nodo dice "espectral −6.0 dB (ahora −4.2 dB)".
- **La latencia** de la columna de SALIDA suma los 46 ms en cuanto una banda es espectral. Encender la primera o apagar
  la última cambia la latencia: hay un fundido de 20 ms entre la salida sin y con retraso (un instante de las dos
  mezcladas, sin clic), como al cambiar de modo de fase.
- CPU (5 bandas dinámicas sobre ruido, estéreo, 44.1 kHz): 5 espectrales ≈ 5 normales (3.4 % de un núcleo); a 96 kHz,
  6.3 %. Entra al DESHACER, al A/B y a la automatización (pero no automatices ESPECTRAL: cada cambio mueve la latencia).

### Ejercicio de escucha guiado en FL Studio

Una voz con autotune que tenga notas largas (o un pad con acordes que cambian) y AUTO GAIN encendido.
1. **Encontrar la resonancia que va y viene.** Campana Q 2, SOLO, barre de 1 a 5 kHz: busca una franja que se pone
   chillona solo en algunas notas. Mira el analizador: un pico angosto que aparece y desaparece.
2. **Dinámica normal.** DINÁMICO, rango −6, umbral hasta que el medidor lo pase solo en esas notas. Escucha con SOLO
   apagado: cuando corta, toda la franja baja (la voz se "nubla" un instante en esas notas).
3. **Espectral.** Enciende ESPECTRAL (baja el umbral ~10 dB: ahora se compara con una frecuencia sola). Mira la curva:
   aparecen dientes angostos justo donde está el armónico, que se mueven con las notas. Compara con A/B: la nota chillona
   se calma y el resto de la voz queda igual de clara.
4. **El límite.** Pon la misma banda espectral en el sub del 808 (60 Hz, Q 1): el corte se oye "borroso" (21 Hz por
   franja no alcanzan para separar el sub de su armónico) y tarda en entrar en los golpes. Ahí va la dinámica normal.

### Receta: la voz con autotune que silba en algunas notas

1. Campana Q 1.5 en la zona del silbido (barre con SOLO; en voces suele estar entre 2.5 y 5 kHz), ganancia 0 dB.
2. DINÁMICO + ESPECTRAL, rango −6 dB, ataque 10 ms, liberación 120 ms.
3. AUTO encendido (el umbral sigue al nivel típico de esa zona: si la voz sube en el estribillo, el EQ no corta más).
4. Mira los dientes: si corta en todas las notas, la zona está demasiado ancha o el rango demasiado hondo; si no corta
   nunca, apaga AUTO y baja el umbral a mano.

Por qué funciona: la resonancia de una voz con autotune está en un armónico que cambia de frecuencia con la nota. Una
campana fija (estática o dinámica) tendría que cubrir toda la zona por donde se mueve; la espectral corta solo el
armónico de turno, dentro de esa zona, y deja intactos los demás.

### Reto sin receta

Un pad de acordes con mucho medio-grave se pelea con la voz, pero solo en algunos acordes (los que tienen notas en
300-500 Hz). Haz que el pad le deje lugar a la voz SOLO donde la voz tiene sus armónicos y SOLO mientras canta. Pistas
(si no sale): ¿qué escucha el detector con DETECTOR = Sidechain? ¿La voz tiene su energía en una franja o en armónicos
separados? ¿Qué ventaja tiene que el corte sea por frecuencia y no de toda la zona?

### Vocabulario

- **STFT**: la FFT de tramos cortos y solapados de la señal (el "espectrograma" es su dibujo).
- **Bin**: cada franja de frecuencia de una FFT (fs / N Hz de ancho).
- **Hop**: cada cuántas muestras empieza un tramo nuevo.
- **Overlap-add (OLA)**: volver al tiempo sumando los tramos solapados.
- **Leakage (fuga espectral)**: la energía de un seno que la ventana reparte en los bins vecinos.
- **Supresor de resonancias**: la familia de procesadores que bajan por frecuencia solo lo que sobresale. **Ejemplo
  real**: en voces de trap y reggaetón con autotune fuerte es común un supresor de resonancias después del autotune para
  quitar el "silbido" metálico en las notas altas sin oscurecer la voz.

### Errores comunes (y cuándo NO usarlo)

- **El mismo umbral que la dinámica normal**: aquí se compara con una frecuencia sola; con un umbral alto no corta
  nunca. Empieza con AUTO o mira el medidor.
- **Rangos enormes en zonas anchas**: con −12 dB en una campana de 3 octavas, cada nota con armónicos fuertes "agujerea"
  el sonido y puede sonar acuoso (cortes por franjas que cambian cada tramo). Mejor rango moderado (−3 a −6) y zona
  justa.
- **Transitorios**: una "t", el ataque del snare o el golpe del kick son más rápidos que un tramo de 46 ms; para eso, la
  dinámica normal (o un compresor).
- **Graves bajo ~150 Hz**: la resolución no alcanza; el corte se oye borroso. Dinámica normal o una campana estática.
- **Mientras grabas o tocas en vivo**: 46 ms de latencia se sienten.
- **En el master "por las dudas"**: un supresor que corta todo el tiempo aplana el carácter de la mezcla; primero busca
  la resonancia con SOLO y úsalo solo donde hace falta.

## Fase 12 — Igualar y dibujar: de una curva a bandas

Dos formas nuevas de decirle al EQ qué hacer sin mover nodos uno por uno. **IGUALAR** escucha una referencia (una
canción cuyo sonido te gusta) y tu pista, y propone la curva que acerca tu balance tonal al de la referencia. **DIBUJAR**
te deja trazar a mano la forma que imaginas. En las dos, Fathom convierte esa curva en bandas normales (campanas y
shelves) que después retocas como cualquier otra: no es una "caja negra", es un punto de partida.

### Conceptos aprendidos

- **El espectro promedio (LTAS).** El analizador muestra el ahora: 93 ms que saltan con cada nota y cada golpe. Para
  hablar del "color" de una mezcla hace falta lo contrario: el promedio de muchos segundos, donde las notas se reparten y
  queda el balance (cuánto grave, cuánto medio, cuánto brillo). Fathom suma la **potencia** de cada franja de una FFT de
  186 ms (8192 muestras: 5.4 Hz por franja, buen detalle en graves) en cada cuadro mientras APRENDER está encendido, y
  divide por cuántos cuadros sumó. Los trozos de menos de −70 dBFS (silencios, colas) no cuentan.
- **Suavizado en fracciones de octava.** El promedio todavía tiene picos en las notas que más se repiten en esa canción
  (la tonalidad, la nota del 808). Antes de comparar, cada punto se reemplaza por la potencia media de la ventana de, por
  ejemplo, 1/3 de octava a su alrededor. Con 1 octava solo queda el balance grueso; con 1/6 la curva empieza a copiar
  detalles de la otra canción (que no tienen nada que ver con tu tema).
- **La curva de igualar = referencia − tu pista**, en dB, punto por punto: positiva donde a tu pista le falta, negativa
  donde le sobra. Se le resta su **promedio entre 50 Hz y 10 kHz**: una referencia masterizada 8 dB más fuerte no debe
  convertirse en "+8 dB en todas partes". Igualar es sobre el color, no sobre el volumen (para el volumen, AUTO GAIN y tu
  oído). Donde alguno de los dos no tiene energía real (80 dB bajo su punto más fuerte, o arriba de 0.45·fs) la curva no
  pide nada: restar dos ruidos de fondo daría números sin sentido.
- **De una curva a bandas: ajuste de curvas.** Ninguna fórmula dice "esta curva son estas 5 campanas", porque la
  respuesta de una banda no es lineal en sus parámetros (mover la frecuencia desplaza toda la campana). Fathom lo resuelve
  como un problema de **mínimos cuadrados no lineales**, en dos pasos:
  1. **Codicioso**: busca dónde la curva se aleja más de lo que ya suman las bandas (el "residuo") y pone ahí una banda
     con una estimación a ojo: la altura del residuo como ganancia y el ancho a media altura como Q (es la definición del
     Q de una campana). En los extremos prueba también un shelf, y se queda con la opción que más reduce el error.
  2. **Levenberg-Marquardt** afina todas las bandas juntas: mide cuánto cambia la curva al mover un poquito cada
     frecuencia, ganancia y Q (con la respuesta exacta de la banda: los mismos coeficientes que suenan), resuelve hacia
     dónde moverlas para bajar el error y acepta el paso solo si baja de verdad. Su parámetro λ alterna entre "saltar
     directo a la solución" (rápido cuando está cerca) y "dar un pasito cuesta abajo" (seguro cuando está lejos).
  Se repite hasta el máximo de BANDAS, hasta que la curva se cumple (0.3 dB) o hasta que otra banda mejoraría menos del
  2 %. Medido: dos campanas y dos shelves se reconstruyen exactos (error 0.000 dB, 4 bandas, ~20 ms); una campana sola
  sale con su frecuencia, ganancia y Q al 0.1 %.
- **Lo no dibujado se deja como está.** En DIBUJAR, donde no trazaste nada la meta es la curva que ya tienes: las bandas
  nuevas solo cambian lo que dibujaste (si no, una campana ancha podría "derramarse" sobre zonas que no tocaste).
- **Sumar o reemplazar.** SUMAR: las bandas nuevas van en las libres y solo agregan lo que falta respecto de lo que ya
  hacen las tuyas. REEMPLAZAR: las tuyas se borran y las nuevas hacen toda la curva (en DIBUJAR, también reconstruyen lo
  que no dibujaste).

### Qué hace cada control (y qué muestra la pantalla)

- **IGUALAR** y **DIBUJAR** (arriba del visor, junto a la escala): entran a cada herramienta; el panel de abajo cambia al
  de la herramienta. El mismo botón otra vez, SALIR o Esc vuelven a la edición normal.
- **REFERENCIA · APRENDER** (y "del sidechain" / "de la entrada"): suma el espectro de la referencia mientras está
  encendido; el botón cuenta los segundos. **TU PISTA · APRENDER**: lo mismo con lo que entra a Fathom ANTES del EQ (lo
  que ya hacen tus bandas no ensucia lo aprendido). Pueden aprender a la vez. **BORRAR**: empieza de cero.
- **CANTIDAD**: cuánto de la diferencia se pide (100 % = el balance de la referencia). **SUAVIZADO**: 1 oct, 1/2, 1/3
  (por defecto) o 1/6: cuánto detalle copia.
- **BANDAS**: el máximo de bandas (usa menos si alcanzan). **SUMAR / REEMPLAZAR**: ver arriba (las bandas de un solo
  canal, L/R/M/S, no se cuentan: las herramientas crean bandas estéreo). **CREAR BANDAS**: las crea como un solo paso de
  DESHACER y quedan elegidas todas (arrastrar una las mueve juntas; la rueda cambia el Q de todas; la escala de ganancia
  de SALIDA las agranda o achica juntas).
- **En el visor (IGUALAR)**: la línea **ámbar** es la referencia aprendida y la **gris punteada** tu pista (los dos
  suavizados, en la escala del analizador); la **naranja punteada** es la curva que se pide (en la escala del EQ); la
  **verde** es la curva total que quedaría con las bandas nuevas. Los nodos siguen editables.
- **En el visor (DIBUJAR)**: arrastrar traza (naranja, en la escala del EQ); clic derecho o Alt + arrastrar borra; Supr
  borra todo el dibujo. Al soltar aparece la verde.
- El panel dice cuántas bandas usaría la vista previa y cuánto se aleja todavía de lo pedido ("se aleja 0.8 dB como
  mucho"). La vista previa se calcula al soltar el mouse o al apagar APRENDER (con 24 bandas tarda ~0.2 s).
- Lo aprendido y lo dibujado viven mientras el proyecto está abierto (aunque cierres la ventana de Fathom); no se guardan
  en el proyecto: lo que queda son las bandas.

### Ejercicio de escucha guiado en FL Studio

Elige una canción de reggaetón cuyo sonido te guste (la referencia) y un beat tuyo en el que estés trabajando. Pon
Fathom en un bus que tenga todo el beat (o en el master, antes del limitador) con AUTO GAIN encendido.
1. **Mandar la referencia.** Carga la canción en una pista del mixer de FL, saca su salida del master (para no oírla
   mezclada) y mándala a la pista de Fathom como sidechain (con la pista de la referencia elegida, clic derecho en la
   flecha de la pista de Fathom → "Sidechain to this track"). En Fathom, IGUALAR; abajo, la REFERENCIA debe decir "del
   sidechain".
2. **Aprender.** Reproduce el coro (o la parte más llena) y enciende APRENDER en los dos. 20-30 s bastan. Apágalos.
   Mira: ¿dónde está la ámbar por encima de la gris? Eso es lo que le falta a tu beat.
3. **Leer la curva naranja antes de crear nada.** ¿Pide más sub (20-60 Hz)? ¿Menos barro (250-500)? ¿Más aire (10 kHz+)?
   Di en voz alta qué crees que vas a oír.
4. **Crear y comparar.** CANTIDAD 100 %, BANDAS 8, CREAR BANDAS. Alterna BYPASS con AUTO GAIN encendido (el volumen
   igualado): ¿se parece más el color a la referencia? Ahora baja la escala de ganancia de SALIDA a 50 %: muchas veces
   suena mejor (acerca el color sin copiarlo).
5. **Suavizado.** Ctrl+Z, vuelve a IGUALAR, cambia a 1/6 oct: la naranja se llena de picos (las notas de la otra
   canción). Escucha si eso suma o solo "ensucia". Vuelve a 1 oct: solo el balance grueso.

### Receta: un beat que suena "chico" al lado de la referencia

1. Fathom en el bus del beat (no después del limitador del master). AUTO GAIN encendido.
2. IGUALAR, referencia por sidechain, aprende 20-30 s de la parte más llena de los dos (coro con el beat completo).
3. SUAVIZADO 1/3 oct, CANTIDAD 60-70 %, BANDAS 6, SUMAR.
4. CREAR BANDAS y revísalas una por una con SOLO: borra las que corrijan algo que es del arreglo y no de la mezcla (si la
   referencia tiene una guitarra en 2 kHz y tu beat no, una campana de +5 dB en 2 kHz no va a inventar la guitarra).
5. Compara con BYPASS a volumen igualado. Si suena más "grande", deja; si suena "procesado", baja la escala de SALIDA.

Por qué funciona: el "tamaño" de un beat de reggaetón está casi siempre en el balance de los extremos (sub del 808 y
aire de los hi-hats) contra los medios. La referencia ya resolvió ese balance; igualar te dice cuánto te alejas y crea
las bandas anchas que lo corrigen, a una fracción (CANTIDAD) para no perder tu carácter.

### Reto sin receta

Dibuja a mano la curva de "voz de teléfono" (o de radio vieja) para un efecto de transición en la intro: sin graves, sin
agudos, un poco nasal. Hazlo con 4 bandas como mucho y que suene creíble. Pistas (si no sale): ¿entre qué frecuencias
vive el sonido de un teléfono? ¿Qué pasa con la verde si el dibujo baja más rápido de lo que una campana o un shelf
pueden bajar? ¿Qué forma de banda, de las que no crea DIBUJAR, haría esa caída mejor? Después compáralo con hacerlo a
mano con un pasa banda.

### Vocabulario

- **LTAS (long-term average spectrum)**: el espectro promedio de muchos segundos; el "balance tonal" de una mezcla.
- **Balance tonal**: cuánto pesan los graves, los medios y los agudos entre sí (independiente del volumen).
- **Suavizado en fracciones de octava**: promediar cada punto con sus vecinos dentro de 1/N de octava.
- **EQ de igualación (match EQ)**: un EQ que copia el balance tonal de una referencia. **Ejemplo real**: al masterizar
  reggaetón y trap es común comparar con 2-3 temas del mismo estilo y usar un match EQ a 30-60 % como punto de partida,
  nunca al 100 %.
- **Mínimos cuadrados**: elegir los parámetros que hacen más chica la suma de los errores al cuadrado.
- **Levenberg-Marquardt**: el método clásico para mínimos cuadrados no lineales (mezcla de Gauss-Newton y descenso).
- **Residuo**: lo que todavía falta: la meta menos lo que ya se consiguió.

### Errores comunes (y cuándo NO usarlo)

- **Igualar al 100 % y listo**: la referencia tiene otro arreglo, otros instrumentos, otra tonalidad. Igualar copia el
  balance, no la canción: usa CANTIDAD 50-70 % y escucha.
- **Comparar partes distintas**: el coro de la referencia contra la intro de tu beat da una curva enorme y falsa. Aprende
  partes equivalentes (las dos llenas).
- **Igualar sin el volumen igualado**: la curva no cuenta el volumen, pero tu oído sí. AUTO GAIN encendido al comparar.
- **Referencia con otro master**: una referencia limitada muy fuerte tiene los graves aplastados por el limitador; tu
  beat sin master no se le va a parecer en los graves solo con EQ. Mejor una referencia menos exprimida.
- **Corregir con EQ lo que es de arreglo**: si falta una guitarra, ninguna campana la crea. Borra esas bandas.
- **Dibujar paredes**: una caída vertical de 30 dB no la hace ninguna campana o shelf; la verde muestra hasta dónde se
  llega. Para cortar, un corte de graves/agudos con su pendiente (créalo a mano).
- **Aprender sin audio**: APRENDER necesita la ventana de Fathom abierta y audio sonando (los silencios no cuentan).
  Igualar no agrega latencia: lo que crea son bandas normales.

## Fase 13 — Color analógico: el "calor" de un EQ de hardware

Un EQ de hardware (un Pultec, el EQ de una consola Neve o SSL) no es solo sus filtros: la señal pasa por amplificadores
(bulbos, transistores) y transformadores que no son perfectamente rectos. Con poca señal no se nota; con la señal
fuerte redondean un poquito los picos y suman armónicos. Eso es el "color" o el "calor". Fathom lo agrega con el selector
**COLOR** de la columna de SALIDA: **Limpio** (lo de siempre), **Sutil** y **Cálido**.

### Conceptos aprendidos

- **Saturación = una curva que se dobla.** Un amplificador ideal es una recta: salida = entrada. Uno real se dobla al
  acercarse a su límite. Fathom usa f(x) = tanh(d·x + β), normalizada para que cerca de cero sea exactamente la recta
  (con poca señal no cambian ni el volumen ni la curva del EQ). **d** es cuánto se dobla (el drive del circuito) y **β**
  corre el punto de trabajo, como el bias de un bulbo: la curva queda asimétrica (los picos positivos se doblan antes que
  los negativos).
- **Armónicos impares y pares.** Una curva simétrica solo crea armónicos impares (3.º, 5.º: el sonido se pone "hueco",
  hacia cuadrada). Una asimétrica crea también los pares (2.º = la octava, 4.º = dos octavas): se oyen "llenos" o
  "cálidos" porque son notas de la misma armonía. Los impares crecen con el cuadrado del drive y del nivel; los pares,
  más despacio. Por eso con poca señal domina el 2.º.
- **Depende del nivel.** Es la idea más importante: el color NO es un filtro. Cada 6 dB más de señal, más o menos el
  doble de armónicos. Medido con un seno de 1 kHz (distorsión = armónicos / fundamental):
  - SUTIL: −12 dBFS → −57 dB (0.15 %), −6 dBFS → −49 dB, 0 dBFS → −39 dB (1.1 %).
  - CÁLIDO: −12 dBFS → −37 dB (1.4 %), −6 dBFS → −31 dB (casi todo pares), 0 dBFS → −24 dB (6 %).
- **El transformador ensucia más los graves.** El núcleo de un transformador se satura con el flujo magnético, que crece
  cuanto más baja es la frecuencia. Fathom lo imita con un **énfasis**: antes de la curva sube los graves (por debajo de
  ~100 Hz: +3 dB en SUTIL, +6 dB en CÁLIDO) y después los baja exactamente lo mismo. Con poca señal los dos filtros se
  anulan (respuesta plana); con nivel, los graves llegan más fuertes a la curva y se saturan antes. Un bajo de 60 Hz a
  −6 dBFS en CÁLIDO: −23.5 dB de distorsión, 7 dB más que un 1 kHz igual de fuerte. Es la idea del énfasis de la cinta
  de Riptide, al revés (allá eran los agudos).
- **Sobremuestreo (lo de Riptide).** Los armónicos de un 15 kHz caen en 30, 45 kHz...: más allá de Nyquist (22.05 kHz a
  44.1) no desaparecen, se reflejan como tonos inarmónicos (aliasing). La curva se calcula a ×4 a 44.1/48 kHz y a ×2 a
  88.2/96: lo reflejado de un 15 kHz a 0 dBFS queda bajo −130 dB. Cuesta latencia (los filtros de subida y bajada son FIR
  de fase lineal): 75 muestras (1.7 ms) a 44.1 kHz, 44 a 48, 13 a 88.2 y 11 a 96. FL la compensa sola.
- **Dónde va.** Después de las bandas (y de los modos de fase y la dinámica espectral), antes de la ganancia de SALIDA,
  como los amplificadores de salida de un EQ de hardware. Consecuencia: subir una banda empuja el color (una campana de
  +6 dB en el 808 lo satura más); la GANANCIA de salida no (es un ajuste limpio después).
- **Filtro DC.** Una curva asimétrica corre un poquito la onda hacia arriba o abajo (componente de 0 Hz). Un high-pass de
  2 Hz lo quita (en 20 Hz baja 0.04 dB: no se ve en la curva).
- **Sin clics.** Pasar de LIMPIO a un color cambia la latencia: la cadena arranca sin oírse, se llena y después se funde
  en 20 ms de la salida sin retraso a la coloreada y retrasada. Entre SUTIL y CÁLIDO, el drive, el bias y el énfasis van
  en rampa de 20 ms. En LIMPIO la cadena se detiene: no gasta CPU y la salida es bit a bit la de antes.

### Qué hace cada control (y qué muestra la pantalla)

- **COLOR** (columna de SALIDA, debajo de CALIDAD): Limpio / Sutil / Cálido. Se automatiza y entra al deshacer y al A/B.
- La **latencia** de abajo suma la del color (a 44.1 kHz y con fase de cero latencia: "latencia 1.7 ms").
- **La curva del EQ no cambia** con el color: la curva es el filtro, y el color no es un filtro. Lo que sí cambia es el
  analizador de SALIDA: con un tono fuerte (o un 808 sostenido) aparecen picos nuevos en 2×, 3×, 4× su frecuencia.
- En BYPASS la original sale retrasada lo mismo (se compara sin saltos de tiempo).

### Ejercicio de escucha guiado en FL Studio

1. **Ver los armónicos.** En un canal, un 808 largo y limpio, una sola nota (por ejemplo A2, 55 Hz; la lectura de nota
   del visor te la confirma). Fathom sin bandas, COLOR en Limpio. En el analizador: un pico en la fundamental y poco más.
2. Pasa a **Cálido**: aparecen picos en 110, 165, 220 Hz (2×, 3×, 4×). Escucha en audífonos y después en el parlante del
   teléfono o de una laptop: con Cálido el 808 se **oye** en parlantes chicos aunque no reproduzcan los 55 Hz. El oído
   reconstruye la fundamental a partir de sus armónicos.
3. **El nivel manda.** Baja 12 dB lo que llega a Fathom (el volumen del sampler o un Fruity Balance antes) y sube 12 dB
   la GANANCIA de salida de Fathom para escuchar igual de fuerte: los armónicos casi desaparecen. Mismo ajuste, otro
   resultado: así funciona el hardware (gain staging).
4. **A ciegas, con el volumen igualado.** En una voz o un bus de batería a nivel normal (picos en −6 dBFS), alterna Limpio
   y Sutil sin mirar (que alguien lo cambie, o cámbialo varias veces con los ojos cerrados). ¿Lo distingues? Muchas veces
   no: Sutil se siente más que se oye (más "densidad", transientes un pelo más redondos). Está bien que sea así.

### Receta: el 808 que desaparece en el teléfono

1. Fathom en el canal del 808, AUTO GAIN encendido.
2. Corte de graves de 24 dB/oct en ~25 Hz (lo que no suena en ningún parlante).
3. Campana +3 dB, Q 1, en la fundamental del 808 (agarra el pico del analizador).
4. COLOR en **Cálido**. En el analizador, armónicos en 2× y 3× la fundamental. Si el 808 ya llega fuerte (picos cerca de
   0 dBFS), basta así; si llega bajo, súbelo ANTES de Fathom (no con la salida).
5. Compara en el parlante del teléfono con BYPASS: con color, la nota se sigue oyendo aunque el teléfono no reproduzca
   los 50 Hz.

Por qué funciona: el 2.º y el 3.º armónico de un 808 (100-200 Hz) caen donde los parlantes chicos sí suenan, y el oído
"rellena" la fundamental (fundamental ausente). El énfasis de graves hace que justo el 808 sea lo que más se colorea, y
la campana lo empuja un poco más hacia la curva.

### Reto sin receta

En el bus de la batería del dembow (kick, snare, hi-hats), consigue que suene más "pegado" y con más cuerpo sin que se
note el EQ: ninguna campana de más de 2 dB. Pistas (si no sale): ¿qué color le corresponde a un bus que ya suena bien?
¿Qué pasa con el color si subes 3 dB la entrada del bus? ¿Y si pones un shelf grave de +2 dB (que va antes del color)?
Compara siempre con el volumen igualado: AUTO GAIN compensa las bandas pero no el color (usa la GANANCIA de salida a
oído o el medidor de FL).

### Vocabulario

- **Saturación**: la curva de un circuito que se dobla con nivel; redondea picos y suma armónicos.
- **THD (distorsión armónica total)**: los armónicos sobre la fundamental, en % o dB. Un EQ de consola: 0.01-1 %; un
  Pultec empujado: unos pocos %.
- **Armónicos pares / impares**: múltiplos 2×, 4×... (octavas: "cálido") o 3×, 5×... ("áspero", "hueco").
- **Gain staging**: cuidar a qué nivel llega la señal a cada etapa. **Ejemplo real**: en las consolas SSL muchos
  ingenieros de hip hop suben la entrada del bus para que "pegue" más; el mismo ajuste con la señal baja no hace nada.
- **Transformador**: dos bobinas sobre un núcleo de hierro; satura antes en los graves. Parte del "peso" en los graves de
  un Neve o de un Pultec viene de ahí.
- **Fundamental ausente**: el oído escucha la nota de una serie de armónicos aunque la fundamental no suene.
- **Aliasing**: armónicos que pasan de Nyquist y se reflejan como tonos inarmónicos.

### Errores comunes (y cuándo NO usarlo)

- **Juzgar que más color = mejor.** Cálido comprime un poquito los picos y suma armónicos: suena "más" aunque no sea
  mejor. Compara siempre con el volumen igualado.
- **Ponerlo en todas las pistas.** Sutil en 20 pistas se suma: el conjunto se pone opaco y menos definido. Úsalo en los
  buses o en las pistas que lo pidan.
- **Esperar color con la señal baja.** Con picos en −20 dBFS ni Cálido hace mucho. Es a propósito (así es el hardware):
  sube el nivel ANTES de Fathom.
- **En pistas que deben quedar limpias**: un sub muy puro para un club (los armónicos le quitan definición al sub-bajo
  en un sistema grande), una voz susurrada muy expuesta, o un master que ya satura en el limitador.
- **Cuando la latencia importa**: grabando en vivo con Fathom en el canal de entrada, 1.7 ms suman. Graba en Limpio.
- **Para corregir**: el color no arregla un balance tonal; primero las bandas, después el color.

## Fase 14 — Otras instancias: ver dónde chocan las pistas

Ecualizar una pista sola es la mitad del trabajo: en la mezcla, el problema casi siempre es que DOS pistas quieren la
misma zona. Con Fathom en varias pistas del mixer, el botón **INSTANCIAS** (arriba del visor) muestra la salida de las
otras sobre la tuya, marca en coral dónde chocan y deja mover las bandas de una de ellas desde esta ventana, sin
cambiar de pista.

### Conceptos aprendidos

- **Enmascaramiento.** Un sonido fuerte tapa a uno más débil que está cerca en frecuencia: el débil no desaparece del
  archivo, desaparece del oído. Por eso un 808 y un kick en la misma nota se "comen", o una voz se pierde detrás de los
  acordes: no falta volumen, sobra la otra pista en esa zona.
- **Bandas críticas (ERB).** La cóclea separa las frecuencias en filtros; dos sonidos dentro del mismo filtro compiten,
  en filtros distintos se oyen los dos. El ancho de esos filtros se mide como ERB (Glasberg y Moore): unos 35 Hz en
  100 Hz (casi media octava), 133 Hz en 1 kHz y 1.1 kHz en 10 kHz (1/6 de octava). Consecuencia práctica: **en graves
  todo choca con todo** (un kick en 60 Hz y un 808 en 45 Hz están en la misma banda crítica); en agudos hay más lugar.
- **Cómo mide Fathom un choque.** Para cada pista, primero el espectro promediado unos segundos (un choque es algo que
  pasa en la canción, no en un golpe), llevado a bandas críticas (lo más fuerte de cada una: el componente que tapa).
  Después, cuánto importa cada zona PARA ESA PISTA: su nivel comparado con su zona principal, con la inclinación de
  4.5 dB/oct del analizador. Choque = las dos pistas importantes a la vez en la misma zona. Una pista mucho más suave
  también choca si esa zona es lo principal de ella: es justamente la que queda tapada.
- **Quién tapa a quién.** En la zona del choque, la diferencia de nivel entre las dos. Pasando el mouse por la zona
  coral, la lectura de arriba dice "choca con 808 (tú +3.0 dB)": tu pista está 3 dB más fuerte ahí (tapa a la otra), o
  "(808 +5.0 dB)": la otra te tapa.
- **Tallar espacio.** Si las dos pistas necesitan la zona, una cede: un corte en una (el "hueco") y, si hace falta, una
  subida suave en la otra. Al cortar en esta instancia, el choque se va apagando en unos segundos (el promedio lento):
  medido con 100 Hz en las dos pistas, cortar 20 dB en una baja el choque de 1.00 a 0.35 y la zona deja de marcarse.
- **Cómo se ven entre sí.** FL carga cada plugin una vez por proceso y todas sus instancias comparten una lista. Si un
  Fathom corre "bridged" (otro proceso), no ve ni es visto. Con la ventana cerrada, una instancia solo manda su espectro
  cuando alguna otra está mirando: si nadie mira, no gasta nada.

### Qué hace cada control (y qué muestra la pantalla)

- **INSTANCIAS** (arriba del visor): abre la lista en lugar del panel de la banda. Tus nodos se siguen editando.
- **ESTA INSTANCIA**: el nombre con que te ven las demás ("Voz", "808", "Bus drums"). Vacío = el de la pista del mixer si
  FL lo manda, o "Fathom N". Se guarda con el proyecto.
- **CHOQUES · MARCAR / SENSIBILIDAD**: marcar o no los choques; Baja = solo donde las dos tienen su zona principal (lo
  grave de verdad), Alta = también lo secundario.
- **La lista**: color, nombre, si suena, **VER** (su salida como una línea de su color y sus choques con esta), **EDITAR**
  (sus nodos en tu visor: anillos chicos con su número; una a la vez) y **dónde choca** ("73-132 Hz (tú +2.5 dB)").
- **En el visor**: columnas coral = choque (más opacas, más choque); barra del color de la otra abajo = zona que cuenta;
  línea de su color = su salida; línea punteada de su color = la curva de la que editas. Arriba, "EDITANDO: NOMBRE".
- **Editar la otra**: arrastrar = su frecuencia y ganancia (Shift fino, Alt un eje), rueda = su Q, doble clic = apagar o
  encender, clic derecho = apagar o dejar de editarla. Lo que cambies se deshace con Ctrl+Z **en su ventana**, no aquí.
- Lo que eliges sigue al salir de INSTANCIAS (mientras el proyecto esté abierto; al abrirlo otra vez se elige de nuevo).

### Ejercicio de escucha guiado en FL Studio

1. **Ver el choque.** Fathom en el canal del kick (nómbralo "Kick") y en el del 808 ("808"), sin bandas. Reproduce el
   loop. En el Fathom del 808: INSTANCIAS → VER en "Kick". Aparece la línea del kick sobre la tuya y, en coral, la zona
   del sub donde chocan. Pasa el mouse: ¿quién está más fuerte?
2. **Escucharlo.** Solo el kick, después kick + 808. Fíjate en el golpe del kick: con el 808 sonando, ¿se oye igual de
   corto y definido? Lo que se pierde (el "punch" del primer ciclo) es el enmascaramiento.
3. **Tallar.** En el Fathom del 808, una campana de −4 dB, Q 1.5, justo en el centro del choque (la frecuencia de la
   lectura de arriba). Mira cómo la zona coral se achica en unos segundos. Escucha otra vez el golpe del kick.
4. **Desde la otra pista.** EDITAR en "Kick": aparecen sus nodos. Crea en la ventana del kick una campana en su
   fundamental (o mueve una que tenga) y ajústala desde la ventana del 808 mirando las dos líneas. Al terminar, cierra
   y abre la ventana del kick: el cambio está ahí, con su propio deshacer.
5. **Siempre con el volumen igualado.** AUTO GAIN encendido en las dos: cortar baja el volumen de la pista y "menos
   fuerte" puede parecer peor aunque la mezcla esté más clara.

### Receta: kick y 808 del dembow que no se pisan

1. Fathom en los dos, con nombre. En el del 808, VER y EDITAR el kick; sensibilidad Media.
2. Decide quién manda en el sub (normalmente el 808: es la nota; el kick aporta el golpe de 50-80 Hz y el "click" de
   2-5 kHz).
3. En el 808: campana −3 a −5 dB, Q 2, en la frecuencia del golpe del kick (el centro del choque).
4. En el kick: corte de graves suave (12 dB/oct) por debajo de la fundamental del 808 y una campana de +2 dB en su click
   (si hace falta, que no choque con la voz: VER también la voz). Las bandas se crean en la ventana del kick (desde
   aquí solo se mueven las que ya tiene); después se afinan desde la del 808, mirando las dos líneas.
5. Mira la zona coral: no tiene que desaparecer (los dos viven en graves), pero sí achicarse y dejar de ser una zona
   principal de los dos a la vez.

Por qué funciona: el oído no puede separar dos sonidos dentro de la misma banda crítica, así que se les da a cada uno
una banda distinta (el 808 la nota, el kick el golpe). Cortar donde la otra pista es fuerte quita lo que de todos modos
no se oía y deja libre lo que sí.

### Reto sin receta

Una voz con autotune y unos acordes de piano (o pad) que la tapan en el estribillo. Con Fathom en las dos y sin tocar el
volumen de ninguna, consigue que la voz se entienda. Pistas (si no sale): ¿en qué zona chocan (mira el texto de la
lista)? ¿Quién tiene que ceder ahí? ¿Basta con un corte en los acordes o hace falta una subida chica en la voz? ¿El
choque está en el centro de la mezcla o a los lados (prueba la banda del piano en Side)?

### Vocabulario

- **Enmascaramiento**: un sonido fuerte vuelve inaudible a uno más débil cercano en frecuencia. **Ejemplo real**: en un
  reggaetón con el 808 muy alto, el kick "desaparece" en un parlante grande aunque esté en la mezcla.
- **Banda crítica / ERB**: el ancho del filtro del oído en cada frecuencia; dentro de él los sonidos compiten.
- **Tallar (carving) / hacer espacio**: cortar en una pista lo que está tapando a otra en su zona principal.
- **Pista que manda**: la que se queda con una zona cuando dos la necesitan (el 808 en el sub, la voz en la presencia).
- **Instancia**: cada copia de un plugin en el proyecto (Fathom en el kick y Fathom en el 808 son dos instancias).

### Errores comunes (y cuándo NO usarlo)

- **Querer que no quede ningún choque.** Dos pistas en graves siempre comparten algo; el objetivo es que no tengan su
  zona PRINCIPAL en el mismo sitio, no que no se toquen.
- **Cortar las dos.** Si cortas la misma zona en las dos pistas, el choque baja pero la mezcla queda hueca ahí. Elige
  quién manda.
- **Mirar sin escuchar.** El coral es una pista para el oído, no un veredicto: una pista que "choca" en 8 kHz puede ser
  justo el brillo que se busca. Decide escuchando, con el volumen igualado.
- **Juzgar un choque en un golpe.** El promedio tarda unos segundos: después de un cambio, deja sonar el loop antes de
  mirar.
- **Esperar ver un Fathom "bridged".** Si una instancia corre en otro proceso no aparece; quita el "bridged" en FL.
- **Olvidar dónde está el deshacer.** Lo que editas de otra instancia se deshace en SU ventana.
