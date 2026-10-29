# Pila LiveCoding ByteBeat

Motor de livecoding de audio en C: un lenguaje de pila (RPN, inspirado en Forth) que genera sonido muestra a muestra como función del tiempo, con osciladores, samples de disco, delay con feedback, grabación y un visualizador SDL2 (forma de onda, XY y espectrograma). Pensado para Linux con hot-reload: guardás el archivo desde el editor y el sonido cambia sin cortarse.

El binario se llama `pila-lc-bb`.

## Qué puede hacer

- Bytebeat clásico (enteros con wraparound int32 auténtico) y floatbeat (aritmética de punto flotante), conmutables en runtime.
- Osciladores: `sine`, `triangle`, `saw`, `square`, `noise`, y variantes band-limited con PolyBLEP (`sawpb`, `squarepb`).
- Samples en 16 slots: loop con wrapping, crossfade, pitch como instrumento, edición en memoria (gain, reverse, mix, trim a zero-crossing).
- Delay con feedback y filtros pasa-altos/pasa-bajos en el loop de realimentación.
- Procesamiento de salida: DC blocker, pasa-bajos, saturación tanh o clamp duro.
- Grabación de la salida a WAV o RAW (int16 o float32).
- Scope SDL2 con 6 modos: columnas por canal, XY (Lissajous), XY Fósforo, espectrograma por franjas y espectrograma intercalado, círculos de frecuencia.
- Livecoding: recarga por inotify al guardar, librerías cargadas desde el CLI, diccionario de funciones persistente durante la sesión.

## Requerimientos

- Linux (inotify para hot-reload, ALSA para audio).
- gcc con pkg-config.
- PortAudio: paquete `portaudio19-dev` (pkg-config lo encuentra como `portaudio-2.0`).
- SDL2: paquete `libsdl2-dev`.

En Debian/Ubuntu:

```
sudo apt install build-essential pkg-config portaudio19-dev libsdl2-dev
```

Opcional, para el flujo de livecoding con Emacs: `emacs` (el Makefile invoca `emacs27`; ajustá la variable `EDITOR` si usás otro nombre).

## Compilación

```
make                 # target all: binario pila-lc-bb (release optimizado)
make run             # compila y ejecuta con tmp/live.p (o CODE_FILE=ruta)
make run-environ     # vacia tmp/live.p y ejecuta con librerias set_8bit.p y defs.p
make run-strict      # depuracion iterativa: recompila, ejecuta, informa codigo y espera
make debug           # compila con -O0 -g y sanitizers ASan/UBSan
make debug-run       # compila en modo debug y ejecuta
make debug-inspect   # compila en debug y abre gdb con backtrace automatico
make clean           # elimina build/ y el binario; vacia tmp/live.p
make help            # lista los targets que tienen descripcion
make editar          # abre emacs con el modo del lenguaje en codes/
```

Variables de línea de comandos (combinables):

```
make CODE_FILE=codes/tema.p run   # otro archivo de codigo para run
make FAST=1                       # release con -Ofast (ver nota)
make DEBUG=1 run                  # cualquier target en modo debug
make DEBUG=1 CODE_FILE=x.p run    # combinadas
```

El build por defecto es release: `-O3 -march=native -flto`. El LTO importa porque el intérprete llama funciones chicas de otras unidades de compilación; sin LTO no se inlinean entre archivos y se paga en el hot path de `{s}`.

Notas:

- **Cambio de flags sin `clean`**: el build guarda un fingerprint de CFLAGS en `build/.cflags-stamp`; al pasar de release a `FAST=1` o `DEBUG=1` (o volver), todos los objetos se recompilan solos. No hace falta `make clean`.
- **`make FAST=1`** agrega `-Ofast` **con `-fno-finite-math-only`**: sin ese contra-flag, `-ffinite-math-only` elimina los guards `isfinite` de la red de seguridad contra NaN/Inf y un infinito en la salida vuelve a envenenar los filtros para siempre (medido: 11999/16000 muestras NaN). El modo FAST relaja exactitud float de último ulp (reasociación, recíprocos, FMA) pero mantiene intacto el contrato de no-finitos; un `#error` en `state.h` impide compilar con `-ffinite-math-only` activo.
Notas:
- `--checkscript` es el **validador puro**: carga (sin ejecutar `{a}`), informa diagnósticos y truncamiento, y termina siempre con exit 0 (limpio) o 1 (problemas). Es el que usa `make run-check` y cualquier cadena `&&` o CI.
- `--strict-load` es la **sesión viva estricta** (`make strict-run`): con carga inicial limpia corre como una sesión normal; con diagnósticos o truncamiento en la carga inicial, o truncamiento en un hot-reload, aborta con exit 1 después de una salida limpia (el recorder parchea su header, el scope cierra). Sin flag, el truncamiento solo avisa: en livecoding un aviso alcanza, pero para evaluar scripts independientes el fallo fatal es lo correcto.
- **`make DEBUG=1`** compila y linkea con `-O0 -g -fsanitize=address,undefined`. La detección de leaks de ASan viene apagada en `run` (`ASAN_OPTIONS=detect_leaks=0`): los leaks de una sola vez de ALSA/PulseAudio/PortAudio son ruido; ASan/UBSan siguen activos para los bugs reales.
- **`make run-strict`** es el compañero de la palabra `STRICT`: recompila y ejecuta el binario **directamente** (sin make anidado) para que el código de salida llegue intacto al loop: 0 = salida limpia (`bye`), 42 = STRICT detuvo la sesión con su reporte, 139 = crash, 134 = abort. Un error de compilación no saca del loop: avisa y espera la tecla. Ctrl+D sale del loop. Ojo: ejecuta siempre `tmp/live.p`, no honra `CODE_FILE`.
- **`make run-environ`** es el flujo de sesión con librerías persistentes: vacía `tmp/live.p`, recompila, y ejecuta cargando antes `codes/lib/set_8bit.p` y `codes/lib/defs.p`.
- **`make debug-inspect`** compila en debug y lanza gdb con `run` y `bt full` automáticos: backtrace completo en el instante en que el programa se detiene.
- **`make editar`** abre `emacs27` (variable `EDITOR` al inicio del Makefile) con `utils/pilalcbb-mode.el` y `utils/keybind.el` cargados, parado en `codes/`.
- **`make help`** imprime los targets que llevan comentario `##` en el Makefile.
- `CC`, `EDITOR` y `PKGS` (los nombres pkg-config de PortAudio y SDL2) se ajustan al principio del Makefile si tu sistema los llama distinto.


## Ejecución

```
./pila-lc-bb                              # usa ./tmp/live.p
./pila-lc-bb live.p                       # un archivo principal
./pila-lc-bb set_8bit.p defs.p live.p     # librerias + principal
./pila-lc-bb --help                       # ayuda
```

Reglas del CLI:

- **El último argumento es el archivo principal**: se carga al inicio y se monitorea con inotify para hot-reload.
- **Los anteriores son librerías**: se cargan una vez, en orden. Sus definiciones (`: ;`, `:=`, `=:`) y sus bloques `{a}` persisten; sus bloques `{b}`/`{b.}`/`{s}` y su código suelto se descartan al cargar el principal. Por eso las configuraciones de una librería deben ir dentro de `{a ... }`.
- Sin argumentos, usa `./tmp/live.p` (constante `DATA_FILE_DEFAULT`).
- Al cargar imprime por stdout el conteo de instrucciones por bloque y el tamaño del diccionario; los avisos y errores van a stderr.

## El lenguaje en tres minutos

Todo es RPN sobre una pila de doubles. El audio se produce en el bloque `{s}`, que corre una vez por muestra; ahí construís un valor y lo mandás a un canal con `OUT`.

```
( comentario anidable: se elimina en parseo, no cuesta runtime )

( bytebeat clasico: t * ((t>>5 | t>>8)) & 255 )
t d 5 >> 1 nd 8 >> | * 255 & 0 OUT

( oscilador: consume frecuencia en Hz )
440 sine 0 OUT

( stereo: un OUT por canal )
220 sine 0 OUT
330 sine 1 OUT
```

### Bloques de contexto

| Bloque                | Cuándo corre                                | Uso típico                                  |
|-----------------------|---------------------------------------------|---------------------------------------------|
| `{a}`                 | una vez al cargar/recargar                  | setup: SR, CH, VMIN/VMAX, cargas, grabación |
| `{a.}`                | antes de recargar y al salir                | teardown: recstop, liberar recursos         |
| `{b}`                 | una vez por buffer, antes de las muestras   | parámetros que cambian por buffer           |
| `{b.}`                | una vez por buffer, después de las muestras | medir, loguear                              |
| `{s}` o código suelto | una vez por muestra                         | generar audio (bloque implícito)            |

Además: `{x ... }` con marcador no reconocido es un **bloque apagado** (se descarta en parseo, útil para comentar código con bloques adentro), y `...` corta la lectura del resto del archivo.

### Strings y comentarios

**Strings literales**: `"texto"` se guarda en una tabla interna (strtab) y el token resultante es un índice numérico. El mismo contenido siempre devuelve el mismo índice (interning). Las palabras que los consumen (`_:`, `recfile`, `anomlog`, `SCOLOR`, `smpload`) distinguen por contexto: saben si el entero que reciben es un índice de string o un valor numérico común.

**Comentarios**: `( texto )` se eliminan en la fase de tokenización, antes de compilar. Son anidables: `(( comentario con ( anidado ) dentro ))` funciona. No llegan al código ejecutable ni cuestan en runtime.

**Fin de código**: `...` corta la lectura del programa. Todo lo que sigue en el archivo se ignora hasta el próximo reload.

Ejemplos:

```
( Este es un comentario )
( Comentarios ( anidados ) funcionan )
"salida.wav" recfile    ( path de grabación )
"#ff0000" 0 SCOLOR      ( color rojo para canal 0 )
"mensaje" _:            ( imprime texto por stderr )
```
### Literales numéricos con base explícita

Además de los enteros decimales estándar (`123`, `-42`), el lenguaje acepta literales con base explícita en el formato `<dígitos>'<base-1>`, donde el sufijo es el **dígito máximo permitido** (un solo carácter). La base se deduce sumando 1 al valor del sufijo.

El conjunto de dígitos es `0-9a-zA-Z` (62 caracteres), lo que permite bases de 2 a 62:

| Sufijo | Base | Uso típico                        |
|--------|------|-----------------------------------|
| `'1`   | 2    | Binario                           |
| `'3`   | 4    | Cuaternario (dos bits por dígito) |
| `'7`   | 8    | Octal                             |
| `'9`   | 10   | Decimal (redundante, pero válido) |
| `'f`   | 16   | Hexadecimal                       |
| `'z`   | 36   | Base 36 (alfanumérica minúscula)  |
| `'Z`   | 62   | Base 62 (máxima soportada)        |

**Mapeo de caracteres a valor**: `0-9` → 0..9, `a-z` → 10..35, `A-Z` → 36..61.

Ejemplos:

```
1010110'1        ( binario = 86 )
1203003'4        ( base 5 = 1*5^6 + 2*5^5 + 3*5^2 + 3 = 18778 )
ff00b1'f         ( hex = 16711857 )
DEADBEEF'Z       ( base 62 )
-ff'f            ( -255, signo al inicio )
```

**Validación**: si algún dígito del cuerpo tiene valor ≥ base, el literal se rechaza y cae como token desconocido (no interfiere con nombres de funciones que casualmente contengan `'`). El signo negativo va al inicio del literal, nunca en el medio.

**Procesamiento**: los literales con base se convierten a entero en la fase de tokenización (pre-evaluación), así que al llegar al intérprete ya son valores numéricos comunes. No hay costo en runtime. El plegado de constantes (`fold_constants`) opera sobre el valor ya convertido y emite el resultado como decimal estándar.

### Definiciones (persisten entre recargas dentro de la sesión)

Las definiciones (`: ;` y `:=`) se resuelven en **parse-time**: no emiten instrucciones a ningún bloque, no cuestan runtime y no consumen el presupuesto de instrucciones de ningún programa. Podés escribirlas fuera de bloques o dentro de `{a}` con idéntico efecto; el único requisito es que aparezcan **antes** que sus usos en el archivo. La excepción es `=: nombre`: el registro es de parse, pero el store es una instrucción de runtime que corre en el bloque donde aparece (fuera de bloques = una vez por muestra).

```
: limite 255 & ;          ( funcion: se expande en parseo )
lim := limite             ( alias )
t 100 % =: fase           ( variable de runtime: guarda el tope )
fase 50 < ?[ 255 ?! 0 ]? 0 OUT
```

### Control de flujo

- `cond ?[ ... ]?` y `cond ?[ ... ?! ... ]?` (if / if-else).
- `fin inicio paso @[ ... ]@` con `i` (índice), `j` (loop exterior), `@!` (salir).
- `a b cond ?` (ternario).
- `#nombre` declara un ancla y `->nombre` salta si el tope ≠ 0 (fuera de loops e ifs).
- Comparadores devuelven -1 (verdadero) o 0 (falso).

### Modos de aritmética

- `1 VINT` (default): enteros con wraparound int32 en `+ - *` y bits; `/` y `%` enteros. Es la estética bytebeat: el overflow es parte del sonido.
- `0 VINT`: doubles; `/` y `%` flotantes. Bits y shifts siguen siendo enteros.

La referencia completa de palabras (firma en pila, efecto, límites y ejemplos) va en `vocabulario.md`.

## Modelo de ejecución

- Cinco programas compilados por archivo (`{a}`, `{a.}`, `{b}`, `{b.}`, `{s}`), cada uno hasta 2048 instrucciones.
- `{s}` corre **una vez por muestra** (8000 veces por segundo con el SR default). Todo lo que pongas ahí se paga 8000 veces por segundo: evitá informadores (`.:`, `::`, `_:`) y cargas dentro de `{s}`.
- La pila de datos **nace vacía en cada ejecución** de un bloque: no hay estado de pila entre muestras ni entre bloques. El estado persistente vive en variables (`=:`), el diccionario y los módulos (samples, scope, delay).
- **Anomalías**: operaciones que no se pueden completar (pila vacía, división por cero, canal de `OUT` fuera de rango) son no-fatales: la palabra queda como no-op esa vez y se cuenta. El loop principal reporta el acumulado por stderr cada ~2 segundos. Un conteo alto y constante suele decirte que hay un bug de pila en `{s}`.
- **Persistencia de sesión**: diccionario de funciones/alias/variables, tabla de strings y slots de samples sobreviven a las recargas; mueren con el proceso. Los índices de strtab son implícitos (los literales se convierten a índice en parseo), así que nunca los escribís a mano salvo para depurar.
- **Fold de constantes**: las operaciones puras entre literales que dan igual resultado en ambos modos se pliegan en parseo (bits, shifts, comparadores, y `+ - *` sin overflow). `/`, `%` y trigonometría no se pliegan porque dependen del modo activo en runtime.
- `,_ nombre` cita una función o alias sin inlinearla: compila a una llamada en runtime que ejecuta la definición vigente (late binding). Habilita recursión (`: fact dup 1 > ?[ dup 1 - ,_ fact * ]? ;`, tope 16 niveles) y hace que los bloques retenidos de BCASC adopten redefiniciones futuras. En el hot path de `{s}`, el inlining sigue siendo más barato: citá solo donde necesites vinculación tardía.

### Límites duros

| Límite                     | Valor                    |
|----------------------------|--------------------------|
| Instrucciones por programa | 2048                     |
| Profundidad de pila        | 128                      |
| Anidamiento de loops       | 16                       |
| Variables de runtime       | 512                      |
| Anclas por programa        | 64                       |
| Canales de audio           | 16                       |
| Slots de samples           | 16                       |
| Strings en strtab          | 256 (256 chars c/u)      |
| Fuente .p                  | 16384 bytes              |
| Buffer de delay            | 65536 muestras por canal |
| Ring del scope             | 65536 muestras por canal |
| Profundidad de llamadas    | 16 niveles               |



### Valores por defecto

| Parámetro               | Default                      | Palabra                        |
|-------------------------|------------------------------|--------------------------------|
| Sample rate             | 8000 Hz                      | `SR`                           |
| Canales                 | 1                            | `CH`                           |
| Buffer                  | 256 frames                   | `BUF`                          |
| Rango de salida         | 0..255                       | `VMIN` `VMAX`                  |
| Aritmética              | entera                       | `VINT`                         |
| DC blocker              | activo                       | `DCB`                          |
| Saturación              | off (clamp duro)             | `SAT`                          |
| Mute                    | off (audio activo)           | `MUTE`                         |
| Pasa-bajos              | off                          | `LP`                           |
| Delay                   | off, 200 ms, feed 50, mix 50 | `DELAY` `DTIME` `DFEED` `DMIX` |
| Filtros del feedback    | off                          | `DFLO` `DFHI`                  |
| Crossfade de samples    | 0                            | `SMPFADE`                      |
| Scope                   | apagado                      | `SCOPE`                        |
| Zoom del scope          | 100 ms                       | `SZOOM`                        |
| Tipo de scope           | 0 (columnas)                 | `STYPE`                        |
| FFT del espectrograma   | 512                          | `SFFT`                         |
| Resolución del scope    | 1 px/columna                 | `SRES`                         |
| Color de fondo          | negro                        | `SBGCOLOR`                     |
| Posición de ventana     | centrada                     | `SWINX` `SWINY`                |
| Tamaño de ventana       | 800 × 400                    | `SWINW` `SWINH`                |
| Fullscreen              | off                          | `SFULLSCR`                     |
| Decoración de ventana   | on                           | `SDECOR`                       |
| Siempre arriba          | off                          | `SWINTOP`                      |
| Trazo del scope         | 1 px, blanco                 | `SSTROKE` `SCOLOR`             |
| Mute                    | off (audio activo)           | `MUTE`                         |
| Modo estricto           | off                          | `STRICT`                       |
| Profundidad de llamadas | 16 niveles                   | `MAX_CALL_DEPTH`               |


`SR`, `CH` y `BUF` reabren el stream de PortAudio entre buffers: producen un corte breve e inevitable. El resto de los setters aplica de inmediato.

## Pipeline de salida

Cada muestra que produce `{s}` atraviesa este camino antes de sonar:

```
   {s} por muestra --OUT--> sample_out[ch]  (silencio si el canal no recibe OUT)
        |
        v
   normalizacion [VMIN,VMAX] -> [-1,1]
        |
        v
   DCB   DC blocker (pasa-altos)            <-- DCB
        |
        v
   LP    pasa-bajos de un polo              <-- LP
        |
        v
   DELAY buffer circular + feedback         <-- DELAY DTIME DFEED DMIX
        |      feedback: DFHI -> DFLO       <-- DFHI DFLO
        v
   SAT   tanh, o clamp duro a [-1,1]        <-- SAT
        |
        v
   out_buffer (float32)
        |
        +--> Pa_WriteStream   (suena)
        +--> recorder_write   (si hay grabacion activa)
        +--> scope_push       (si el scope esta activo)
```

El recorder y el scope toman la señal **ya procesada**, exactamente la que sale por los parlantes: lo que ves y lo que grabás es lo que suena.

El DC blocker activo por defecto no es decorativo: elimina la componente continua que aparece naturalmente en bytebeat y que a la larga puede dañar parlantes y auriculares. Desactivalo (`0 DCB`) solo cuando entiendas qué DC estás dejando pasar.

Notas:
- **`MUTE`** actúa sobre la escritura final a PortAudio: con MUTE activo, el recorder y el scope siguen recibiendo la señal procesada completa. La combinación `1 MUTE` + grabación es el flujo natural para registrar una sesión que no podías escuchar en el momento.

## Scope

Ventana SDL2 redimensionable, sincronizada con el audio porque se alimenta del mismo buffer que va a PortAudio.

| `STYPE` | Modo                                                                                                                                                                                                                                                      |
|---------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0       | **Forma de onda**: Una fila por canal, min/max por columna, render incremental                                                                                                                                                                            |
| 1       | **XY (Lissajous)**: Canal 0 en X, canal 1 en Y. Requiere 2 canales.                                                                                                                                                                                       |
| 2       | **XY fósforo**: Estelas persistentes con decaimiento exponencial hacia `SBGCOLOR`; solo se dibujan los segmentos nuevos por frame (el modo más barato).                                                                                                   |
| 3       | **Espectrograma por franjas**: Una banda por canal, frecuencia en escala log (20 Hz–Nyquist), intensidad en dB teñida con el color del canal.                                                                                                             |
| 4       | **Espectrograma intercalado**: Columnas alternadas por canal compartiendo toda la altura de la ventana.                                                                                                                                                   |
| 5       | **Espectrómetro circular**: Espectro instantáneo en coordenadas polares (frecuencia log en el perímetro, magnitud en dB como gotas centradas en el hilo), gap de 12° con tick de origen, peak-hold y grilla automática por canal con el número al centro. |
| 6       | **waveform**: Dibuja una curva continua interpolando linealmente entre muestras, como un osciloscopio clásico. Soporta multicanal (una fila por canal, con su propio color y grosor).                                                                     |

Palabras del scope:

**Estado y visualización:**

| Palabra    | Efecto                                                                                                                           |
|------------|----------------------------------------------------------------------------------------------------------------------------------|
| `SCOPE`    | Enciende/apaga el visualizador (0 = off, ≠0 = on)                                                                                |
| `SZOOM`    | Ventana temporal visible en milisegundos (clamp 1..2000)                                                                         |
| `SCOLOR`   | Color del trazo o teñido por canal: `"#rrggbb" canal`                                                                            |
| `SSTROKE`  | Grosor del trazo por canal (mínimo 1, sin tope superior)                                                                         |
| `STYPE`    | Modo: 0 = columnas, 1 = XY, 2 = XY Fósforo, 3 = espectrograma franjas, 4 = intercalado, 5 = espectrograma circular, 6 = waveform |
| `SRES`     | Pixels por columna en modo 0 (clamp 1..16)                                                                                       |
| `SFFT`     | Tamaño de FFT en espectrogramas: 256/512/1024/2048                                                                               |
| `SBGCOLOR` | Color de fondo de la ventana: `"#rrggbb"`                                                                                        |

**Geometría de ventana (persistente: se aplica al abrir y en caliente si ya está abierta):**

| Palabra           | Efecto                                                                                        |
|-------------------|-----------------------------------------------------------------------------------------------|
| `SWINX` / `SWINY` | Posición de la ventana en coordenadas de pantalla (admite negativos en multi-monitor)         |
| `SWINW` / `SWINH` | Tamaño de la ventana (clamp ≥ 1; inicial 800×400)                                             |
| `SFULLSCR`        | 0 = ventana normal, ≠0 = fullscreen desktop (sin cambio de modo de video)                     |
| `SDECOR`          | 0 = sin bordes (modo vitrina), ≠0 = con decoración (default)                                  |
| `SWINTOP`         | 0 = normal, ≠0 = siempre arriba (requiere SDL ≥ 2.0.16; en Wayland nativo puede no funcionar) |

**Getters:**

| Palabra         | Efecto                                                    |
|-----------------|-----------------------------------------------------------|
| `scrw` / `scrh` | Ancho/alto del display donde vive (o viviría) la ventana. |
| `winx` / `winy` | Posición actual de la ventana (la que reporta el WM).     |
| `winw` / `winh` | Tamaño actual de la ventana (el que reporta el WM).       |

Notas:
- Los getters `winx`/`winy`/`winw`/`winh` devuelven la geometría **actual** (la que reporta el WM), no la deseada: si redimensionás la ventana a mano, reflejan el nuevo tamaño. Si la ventana no está abierta, devuelven los valores deseados guardados por los setters.
- `scrw`/`scrh` inicializan el subsistema de video si hace falta, así que funcionan desde `{a}` antes de que la ventana exista; devuelven 0 si no hay display (headless).
- En fullscreen la posición y el tamaño los maneja el desktop; tus valores seteados con `SWIN*` quedan guardados y se reaplican al volver a ventana con `0 SFULLSCR`.
- `SWINTOP` funciona en X11 con la mayoría de los WMs (Mutter/GNOME, KWin/KDE, xfwm, i3, sway en XWayland). En Wayland nativo el compositor puede ignorar el hint. Requiere SDL ≥ 2.0.16.
- En el modo 2 (fósforo) no aplican `SZOOM`, `SRES` ni `SFFT`: no hay ventana temporal ni columnas ni FFT. La longitud de las estelas la fija el decaimiento (constante `PHOS_FADE_ALPHA` en scope.c, ~0.25 s de media vida al tope de ~40 fps). `SCOLOR` del canal 0 y `SSTROKE` sí aplican.
- En el modo 5 (espectrómetro circular) no aplican `SZOOM` ni `SRES`: no hay eje temporal ni columnas. `SFFT` controla la resolución espectral, `SCOLOR` y `SSTROKE` aplican por canal. El thrott

Layout adaptativo típico:

```
{a
  1 SCOPE 0 SDECOR
  scrw 2 / 320 - SWINX      ( centra a mano una ventana de 640 )
  scrh 2 / 200 - SWINY
  640 SWINW 400 SWINH
}

```
Layout adaptativo típico (ventana centrada, sin bordes, siempre arriba):

```
{a
  1 SCOPE 0 SDECOR 1 SWINTOP
  scrw 2 / winw 2 / - SWINX
  scrh 2 / winh 2 / - SWINY
  "#112233" SBGCOLOR
}
```

Notas:
- El modo 0 usa render incremental (textura ping-pong): solo se dibujan las columnas nuevas, sin titileo.
- `SZOOM` fija la ventana temporal y se adapta al sample rate: la velocidad visual es la misma a 8 kHz que a 44.1 kHz.
- En los espectrogramas, cada columna nueva implica una FFT por canal; si la CPU aprieta, bajá `SFFT` o subí `SZOOM`.
- En fullscreen la posición y el tamaño los maneja el desktop; tus valores quedan guardados y se reaplican al volver a ventana con `0 SFULLSCR`. `scrw`/`scrh` inicializan el subsistema de video si hace falta, así que funcionan desde `{a}` antes de que la ventana exista; devuelven 0 si no hay display (headless).

## Samples

16 slots numerados. Los archivos son raw sin header, little-endian. El formato de lectura se decide por extensión: `.i16`/`.rawi16` → int16, `.f32`/`.rawf32` → float32. **Cualquier otra extensión (incluidas `.raw`, `.wav` o ninguna) fuerza la interpretación según el `VINT` activo en el momento del `smpload`** (entero → int16, float → float32), avisando por stderr: es la vía deliberada para efectos de rotura de formato, y no falla ni crashea. Internamente todo se guarda normalizado a [-1,1] y al leer se escala al rango `[VMIN,VMAX]` actual.

```
{a
  0 VINT
  "kick.i16" 0 smpload      ( carga o sobreescribe el slot 0 )
  0 smploop                 ( recorta al zero-crossing: adiós click de loop )
  3 2 / 0 smpgain             ( amplifica )
  0 smpreverse              ( invierte )
  0 1 smpmix                ( mezcla 1 sobre 0, con clip )
}
{s
  t 0 smplen % 0 sample 0 OUT   ( loop a velocidad normal )
  300 0 samplepitch 1 OUT       ( el sample como instrumento a 300 Hz )
}
```

- `sample` interpola linealmente entre muestras: las velocidades no enteras suenan limpias.
- La posición se envuelve por módulo: loop natural, y las posiciones negativas reproducen al revés.
- `SMPFADE` agrega un crossfade en el punto de loop para cortes imperfectos.
- `smpremove` libera un slot; `smpreset` libera todos. Los slots persisten entre recargas.
- **Exportación de samples**: RAW sin header y mono, o directamente los nombres con sufijo `.rawi16`/`.rawf32` que genera el recorder. Ver Troubleshooting.
- `smplive` congela un tramo del audio procesado en vivo dentro de un slot de samples: el puente natural entre la sesión y el banco de samples (capturás un momento y después lo tocás como instrumento con `samplepitch`).

## Grabación

```
{a
  "/dev/shm/toma.wav" recfile   ( termina en .wav => WAV con header )
  recstart
}
{a. recstop }
```

- **Regla de formato y nombre final**: si el path termina en `.wav` (sin distinguir mayúsculas), se graba WAV con header. **Cualquier otra extensión implica RAW**, y el sistema agrega al nombre final un sufijo explícito de formato: `.rawi16` (int16) o `.rawf32` (float32), según el `VINT` congelado en `recstart`. Si el path ya terminaba en `.raw`, `.rawi16` o `.rawf32`, ese sufijo se reemplaza por el del modo actual (no se acumula): la extensión nunca miente sobre el contenido. El log de `recstart` imprime el nombre final, y es ese el nombre que después se le pasa a `smpload`.
- El formato de muestras se congela en `recstart`: int16 si `VINT` está activo, float32 si no. El header WAV se parchea con los tamaños reales en `recstop`.
- La precisión de inicio/fin es de un buffer de audio (el volcado ocurre una vez por buffer).
- **Round-trip garantizado**: lo que graba `recstart` vuelve a entrar por `smpload` con el nombre sufijado, sin renombrar nada.
- **Importante**:No cambies `SR`, `CH` ni `VINT` mientras grabás: el header queda inconsistente con los datos.

```
{a 1 VINT "/dev/shm/toma" recfile recstart }   ( abre /dev/shm/toma.rawi16 )
{a. recstop }
...
{a "/dev/shm/toma.rawi16" 0 smpload  0 smploop }
```

## Flujo de livecoding

El ciclo central: editás el archivo principal → guardás → inotify avisa → se ejecuta `{a.}` del programa saliente → se carga el nuevo → se ejecuta su `{a}`. El audio no se corta (salvo que cambies SR/CH/BUF).

Con Emacs (`make editar` abre `codes/` con el modo y los keybinds cargados):

- `pilalcbb-mode`: resaltado, indentación, comentarios `( )` con `C-c C-c`.
- `s-e` (`clm:send-block-to-file`): envía el bloque delimitado por líneas `...` alrededor del cursor al archivo target (`clm:block-target-file`, default `../tmp/live.p`). Es la forma de tocar: tenés un archivo grande con bloques y vas mandando de a uno.
- `M-+` / `M--`: incrementa/decrementa el número bajo el cursor (parcheo rápido de constantes en vivo).
- `s-escape`: guarda. Teclas de volumen y modo zen incluidos en `utils/keybind.el`.
- `reload` fuerza una recarga del archivo principal aunque no haya cambios en disco: útil para reiniciar estado acumulado en variables o relanzar `{a}` sin tocar el editor. Ojo: incondicional en `{a}` produce un loop de recargas infinito.
- `BCASC` (y `--bcasc` en CLI) activa el modo de bloques en cascada: cada carga reemplaza solo los bloques que el archivo define y el resto sigue sonando desde su carga anterior. Bloque vacío (`{b }`) silencia; bloque ausente se retiene. Es el modo de enviar capas incrementales sin reenviar el archivo completo. Las variables se comparten en vivo; las funciones inlineadas conservan el cuerpo de su carga (con aviso si las redefinís).

Sin Emacs, `utils/send-blocks.py` hace lo mismo por línea de comandos, ideal para "partituras":

```
utils/send-blocks.py codes/comp/bb36f.p tmp/live.p 4          ( todos los bloques, 4 s entre cada uno )
utils/send-blocks.py codes/comp/bb36f.p tmp/live.p 4 --only 7 ( solo el bloque 7 )
```

El repositorio trae material para practicar: `codes/test/*.p` son bloques de prueba por palabra (cada uno documenta su palabra en comentarios), `codes/comp/*.p` son composiciones armadas para enviar por bloques, y `codes/lib/*.p` son librerías de configuración y helpers (`L`, `R`, `U`, `LR` para escribir canales, entre otros).

### Capas incrementales con BCASC y `...`

Con `1 BCASC` (o `--bcasc`), cada carga reemplaza solo los bloques que el archivo define y retiene el resto. Combinado con el corte `...`, el archivo se vuelve una pila de capas: movés el `...` para dejar activa solo la capa que estás editando, guardás, y el hot-reload aplica esa capa sobre la sesión viva sin tocar los bloques de las capas anteriores. Un bloque vacío (`{s }`) silencia su contexto; un bloque ausente lo deja sonando como estaba.

## Troubleshooting

Casos reales que aparecieron durante el desarrollo, con su diagnóstico:

**Click en el loop de un sample que no se va ni con fades.**
El archivo tiene header WAV y se está leyendo como crudo. Desde el cambio de extensiones, `smpload` lo dice explícitamente al cargar ("parece un WAV (header RIFF); se leerá como datos crudos"): si ves ese aviso y no estabas buscando el glitch, reexportá desde el editor como RAW header-less, o usá el nombre con sufijo `.rawi16`/`.rawf32` que generó el recorder. Los 44 bytes del header interpretados como muestras aparecen como basura justo en la posición 0.

**Quiero el glitch, no el aviso.**
Es deliberado y el aviso es informativo, no un error: cualquier extensión no reconocida fuerza la lectura según el `VINT` actual. Para romper el formato de un archivo concreto, cargalo por un nombre con extensión ambigua (una copia `.raw`, `.dat`, o el `.wav` original) con el `VINT` contrario al contenido: un float32 leído como int16 suena a vidrio roto, y el sistema no se niega ni crashea.

**Sample con un "halo" metálico o vibrato raro.**
El archivo es estéreo y se está leyendo como mono: los canales L y R intercalados se interpretan como muestras consecutivas de un solo canal, produciendo una modulación a `sample_rate/2` Hz. Solución: exportar en mono. (Como efecto, a veces es deseable; ahora ya sabés de dónde sale.)

**Sonido áspero o con ruido de cuantización.**
Es el modo entero haciendo su trabajo: el rango 0..255 cuantiza la señal. Es la estética bytebeat. Si querés suavidad: `0 VINT` con rango flotante, o `1 SAT` para saturación tanh.

**Golpe o discontinuidad al loopear un sample.**
Tres herramientas, de más a menos quirúrgica: `smploop` (recorta al zero-crossing), `SMPFADE` (crossfade en el punto de loop), o fades en el propio archivo.

**Reporte de anomalías constante.**
Bug de pila en `{s}` (consumís más de lo que apilás, o un `OUT` a canal inexistente con `CH` chico). Depurá el bloque una sola vez en `{a}` con `::` y `.:`.

**Corte breve al cambiar SR/CH/BUF.**
Esperado: el stream se reabre entre buffers. Por eso esos setters van en `{a}`, no modulados en `{s}`.

**Latencia perceptible al tocar.**
El buffer default son 256 frames (32 ms a 8 kHz). `512 BUF` o `1024 BUF` bajan la carga y las llamadas al scope a cambio de latencia.

**Un loop infinito ya no cuelga: degrada y avisa.**
Históricamente un `->` hacia atrás o un `@[` gigante congelaban el hilo de audio para siempre (es el mismo hilo que renderiza). Hoy cada ejecución de bloque tiene un presupuesto de instrucciones; al pasarlo, el bloque se abandona con una anomalía que el contador de stderr y `anomlog` atribuyen al pc del bucle. Si oís el audio roto y el contador de anomalías sube sin parar, hay un loop que no termina: mirá el `anomlog`.

## Modelo de confianza

Un archivo `.p` es código confiado, al mismo nivel que un script de shell: el lenguaje no tiene sandbox. `recfile` y `anomlog` abren y **truncan** cualquier ruta que el proceso pueda escribir; `smpload` lee cualquier archivo accesible; `default` y `bye` afectan la sesion completa. Es por diseño (herramienta personal de livecoding), e implica:

- No ejecutes `.p` de terceros en una sesion donde haya archivos que te importen: un path malicioso o torpe en `recfile`/`anomlog` pisa lo que habia, igual que un `>` de shell.
- Si algún día compartís sesiones o corres código ajeno, hazlo bajo un usuario dedicado, container o chroot: el binario no impone ningún límite propio.
- Los avisos de `smpload`/`recfile`/`anomlog` por stderr son tu única red: path que no abre, mensaje que sale.

## Depuración

- `make DEBUG=1 && make debug-run`: sanitizers (ASan/UBSan) encendidos.
- Informadores en `{a}` o `{b}`: `::` (pila completa), `.:` (tope), `_:` (string por índice).
- El conteo de instrucciones que imprime cada carga te dice si un bloque quedó vacío o truncado (límite 2048).
- Los avisos de parseo (comentarios sin cerrar, bloques apagados sin `}`, strings sin cierre, tokens desconocidos) salen por stderr en el momento de la carga y quedan acumulados en el módulo `diag`.
- `STRICT` es el modo "bug cero" para desarrollo: con `1 STRICT`, la primera anomalía detiene la sesión y vuelca por stderr dónde ocurrió (contexto, pc, opcode, pila), cómo estaba la sesión y qué loops estaban activos. Es el complemento de `anomlog`: `anomlog` graba la historia completa de anomalías en archivo; `STRICT` te frena en seco en la primera para que la mires de cerca.
- **Validación de scripts completos**: `./bytebeat-rpn --checkscript archivo.p` valida el archivo sin ejecutarlo en modo livecoding. Si el archivo excede el límite de 16383 bytes, aborta con error en vez de truncar silenciosamente. Útil para CI/CD, validación antes de deploy, o para detectar scripts que accidentalmente superan los límites del sistema.
- `showlimits` imprime por stdout el uso actual contra todos los límites del sistema (bloques, anclas, slots, canales, pila, loops, fuente, diccionario, strings): el chequeo rápido de "¿cuán cerca estoy de tocar un tope?" antes de que un aviso de truncamiento o de bloque lleno te lo diga por las malas.

### Debug detallado de anomalías (`anomlog`)

El contador agrupado cada ~2 segundos te dice *cuántas*; `anomlog` te dice *dónde y por qué*:

```
{a "/dev/shm/anom.log" 2 log }
```

Durante 2 segundos de audio, cada anomalía deja una línea con contexto, posición en el programa, opcode y fotografía de la pila:

```
t=256 (0.032s) ctx=sample pc=3 op=/ sp=2 pila=[ 1 0 ]
```

Con `pc` y `op` ubicás la palabra que falla; con la pila, el valor culpable (divisor 0, canal fuera de rango, índice de string inválido). Con segundos `0` cortás la captura antes del plazo.

### Mesa limpia entre pruebas (`default`)

`default` al principio de un `{a}` restablece toda la configuración del sistema (audio, procesamiento, scope, delay, geometría de ventana) a los valores de arranque, sin tocar tus definiciones, variables ni samples cargados. Es la herramienta para cuando probás muchos fragmentos en secuencia y las configuraciones se empiezan a mezclar.

Consideraciones:
 - Apaga el scope (es su default).
 - `default` resetea el buffer de delay completo en cada llamada; usalo en `{a}`, no en `{s}`.

### Precedencia de operadores (JS / Dolchan)

Al portar expresiones infijas de bytebeat clásico a RPN, respetá este orden.
Los operadores de arriba atan **más fuerte** que los de abajo; dentro de cada
nivel la asociatividad es de izquierda a derecha (salvo los unarios, que son
de derecha a izquierda).

| Nivel | Operadores        | Notas                                                                                                |
|------:|-------------------|------------------------------------------------------------------------------------------------------|
|     1 | `-` `~` (unarios) | Negación y complemento a nivel de bit                                                                |
|     2 | `*` `/` `%`       | Multiplicativos                                                                                      |
|     3 | `+` `-`           | Aditivos                                                                                             |
|     4 | `<<` `>>`         | Desplazamientos; el operando derecho es una expresión aditiva completa (`x >> a * b` = `x >> (a*b)`) |
|     5 | `<` `<=` `>` `>=` | Relacionales                                                                                         |
|     6 | `==` `!=`         | Igualdad                                                                                             |
|     7 | `&`               | AND a nivel de bit                                                                                   |
|     8 | `^`               | XOR a nivel de bit                                                                                   |
|     9 | `\|`              | OR a nivel de bit                                                                                    |

**Regla práctica**: cuando tengas duda, parenthesizá mentalmente la expresión
JS según esta tabla y traducí cada subexpresión de adentro hacia afuera. En
RPN no hay precedencia: el orden de evaluación es explícito en la pila.

**Ejemplo** — `t>>4|t&((t>>5)/(t>>7-(t>>15)&-t>>7-(t>>15)))`:

```
t 4 >>                                    ( A = t>>4 )
t                                         ( para el AND externo )
  t 5 >>                                  ( B = t>>5 )
  t 7 >>                                  ( C = t>>7 )
    t 15 >>                               ( D = t>>15 )
    t -1 * 7 >>                           ( E = (-t)>>7, NO -(t>>7) )
    &                                     ( F = D & E )
  -                                       ( G = C - F )
  t 15 >>                                 ( H = t>>15 )
  -                                       ( I = G - H )
/                                         ( J = B / I )
&                                         ( K = t & J )
|                                         ( resultado = A | K )
0 OUT
```

Los dos errores más comunes al portar:

1. **Confundir el operando derecho de un shift**: `x >> a * b` en JS es
   `x >> (a * b)`, no `(x >> a) * b`. En RPN: `a b * x >>`.
2. **Negación unaria vs. resta**: `-t >> 7` en JS es `(-t) >> 7`. En RPN:
   `t -1 * 7 >>`, nunca `t 7 >> -1 *`.
