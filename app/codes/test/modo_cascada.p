(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")


( TEST DIDACTICO: modo cascada BCASC y cita ,_

  Como se corre: el archivo es una pila de capas separadas
  por lineas de puntos. Se aplican EN ORDEN, de a una por
  guardado sobre tmp/live.p, dejando que el hot-reload
  aplique cada capa a la sesion viva. Como BCASC queda
  encendido desde la capa 1, cada carga reemplaza solamente
  los bloques que esa capa define; todo lo demas sigue
  sonando con el programa de su propia carga.

  Idea que el test hace audible:
    variables =:    siempre vivas: cualquier bloque, retenido
                    o nuevo, lee el valor actual del slot
    funciones : ;   su CODIGO se inlinea al compilar el
                    bloque que las usa; un bloque retenido
                    conserva el cuerpo de su propia carga
    cita ,_ nom     compila a llamada en runtime: ese uso
                    ejecuta siempre la definicion vigent
  Guion de capas y que escuchar en cada una:
    1  base: BCASC on, voz y una recursion con cita que
       imprime 120 al terminar la carga
    2  solo s nuevo: la cita mantiene voz al dia
    3  solo funcion nueva: el s retenido la adopta por cita
    4  funcion y consumidor nuevos: cuerpo inlineado nuevo
    5  variable fijada en a: vive hasta la proxima carga
    6  contraste: canal 0 citado, canal 1 inlineado
    7  y 8 redefinen voz: canal 0 salta, canal 1 congelado
    9  voz con estado por buffer: la cita lo persigue
   10  s vacio: silencio por definicion vacia
)

...
( ---- capa 1: la base ---------------------------------------
  Enciende BCASC para toda la sesion: a partir de la PROXIMA
  carga, cada archivo reemplaza solo los bloques que define.
  Define voz a 220 Hz y fact, una recursion que se cita a si
  misma con ,_ : sin cita la autoreferencia explotaria en la
  expansion; con cita se resuelve en runtime, con tope de
  16 niveles de profundidad. El .: imprime 120, o sea 5!,
  al terminar la carga: es la recursion andando.
)
{a
  22050 SR  2 CH  1 BCASC
  0 VINT  1 SCOPE  5 STYPE
  1 d -1 * VMIN VMAX
  : voz 220 sine ;
  : fact d 1 > ?[ d 1 - ,_ fact * ]? ;
  5 ,_ fact .:
}
( El s de la base: canal 1 la voz citada, canal 0 su inverso.
  Al citar, este bloque no envejece: cualquier capa futura
  que redefina voz se escucha aca sin reenviar nada.
)
{s ,_ voz d 1 OUT -1 * 0 OUT }

...
( ---- capa 2: reemplaza solamente s -------------------------
  El a queda retenido: no se redefine. Pero ojo, el ciclo de
  recarga lo vuelve a correr igual: se reimprime el 120 de
  fact en stderr, que es el teardown+setup de siempre.
  Este s nuevo sigue citando voz: mismo tono, otro patron:
  una puerta por bits de t que suena a tremolo cuadrado.
)
{s ,_ voz  t 13 >> 2 % OUT }

...
( ---- capa 3: redefine solo la funcion ----------------------
  No toca s: el bloque retenido de la capa 2 sigue sonando
  y, como usa cita , _, adopta los 303 Hz al instante.
  Escuchar: el salto de altura sin reenviar el consumidor.
  En stderr puede aparecer el aviso conservador de bcasc
  listando voz: avisa por los usos inlineados retenidos que
  no adoptan el cambio; aca no hay tales usos, y la cita lo
  desmiente en el audio.
)
{a : voz 303 sine 1 *  ; }

...
( ---- capa 4: funcion y consumidor nuevos -------------------
  Ahora voz se construye con la variable freq. El inlineado
  copia el codigo, o sea la carga de freq mas el sine, y no
  el valor: por eso freq seguira viva y mutable mas adelante.
  El s nuevo usa voz SIN cita: queda inlineado tal como esta
  definida en esta carga. Ambos canales suenan igual.
)
{a 200 =: freq   : voz freq sine ; }
{s voz d 0 OUT 1 OUT }

...
( ---- capa 5: variable fijada en a ---------------------------
  El a corre una sola vez por carga: freq queda como foto
  del instante de carga, un valor fijo hasta el proximo
  guardado. Comparar con la capa 9, donde b mueve su estado
  buffer a buffer.
)
{a t 400 % 100 + =: freq  }

...
( ---- capa 6: el contraste central ---------------------------
  Canal 0 con cita ,_ : escucha siempre la voz vigente.
  Canal 1 sin cita: inlinea el cuerpo de voz de ESTA carga
  y se congela ahi: las proximas redefiniciones no lo tocan.
  De aca en mas, cada redefine de voz se escucha como un
  desacuerdo entre canales: es la epoca de cada uso sonando
  al mismo tiempo.
)
{s
  ,_ voz 0 OUT
  voz 1 OUT
}

...
( ---- capa 7: redefinicion que solo la cita adopta -----------
  voz pasa a 452 Hz: el canal 0 salta, el canal 1 sigue con
  el cuerpo congelado en la capa 6. El aviso de bcasc lista
  voz en stderr: es exactamente este congelamiento el que
  anuncia.
)
{a : voz 452 sine ; }

...
( ---- capa 8: vuelta a 220, mismo efecto ---------------------
  Canal 0 vuelve a 220 Hz por cita; canal 1 imperturbable
  desde la capa 6. Si el desacuerdo se oye, el test pasa.
)
{a : voz 220 sine ; }

...
( ---- capa 9: voz con estado por buffer ----------------------
  La nueva voz mezcla xt y freq: xt lo actualiza b en cada
  buffer con un contador lento, y freq se fija en 210.
  Canal 0 citado: persigue la forma nueva y su patron.
  Canal 1 congelado en codigo pero VIVO en datos: como su
  cuerpo inlineado lee la variable freq, el cambio a 210 Hz
  si lo atraviesa; lo que no recibe es la forma nueva de voz.
  Escuchar: mismo tono en ambos canales, distinta forma.
)
{a : voz xt freq 1 / d 2 nr r % + sine ; }

...
{b t 12 % =: xt    210 =: freq }

...
( ---- capa 10: silencio por bloque vacio ---------------------
  Un marcador presente pero sin contenido es una definicion
  valida que reemplaza al bloque por nada: silencio
  inmediato en los dos canales. Es la forma de apagar un
  contexto sin borrar el archivo ni salir del modo cascada.
  Para cerrar el test y volver al reemplazo completo en una
  capa futura: 0 BCASC.
)
{s }
