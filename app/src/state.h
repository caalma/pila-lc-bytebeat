// -------------------------------------------------------------------------
// state.h — Estado global de la aplicacion: programas cargados, parametros
// de audio, contador de tiempo y buffer de salida por muestra.
//
// Se mantiene como estado global (igual que el proyecto de referencia) para
// que todas las capas (parser, interprete, audio) lean/escriban sobre las
// mismas variables sin pasar structs por parametro.
// -------------------------------------------------------------------------
#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include "opcodes.h"


// El contrato de no-finitos del lenguaje (ver VOCABULARIO) depende de
// semantica IEEE: los guards con isfinite son la red de seguridad contra
// envenenamiento de filtros, lecturas invalidas y casteos UB.
// -ffinite-math-only (implicado por -Ofast/-ffast-math) los elimina en
// silencio como codigo muerto. Este candado convierte ese escenario en
// un error de compilacion en vez de en un bug fantasma.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "pila-bytebeat requiere semantica IEEE: si usas -Ofast o -ffast-math, agrega -fno-finite-math-only"
#endif

// -- Programas cargados ---------------------------------------------------
// Uno por bloque de contexto:
//   {a}  -> prog_app        (corre una vez al cargar/recargar)
//   {a.} -> prog_app_post   (teardown del programa saliente, antes del load)
//   {b}  -> prog_buf        (una vez por buffer, antes de generar muestras)
//   {b.} -> prog_buf_post   (una vez por buffer, despues de generarlas)
//   {s}  -> prog_sample     (una vez por muestra; es el bloque implicito)
extern Program prog_app;
extern Program prog_app_post;
extern Program prog_buf;
extern Program prog_buf_post;
extern Program prog_sample;

// Fuente .p leida del disco (para referencia/debug) y flag de recarga.
extern char rpn_source[RPN_BUF_SIZE];
extern int  needs_reload;

// -- Parametros de audio activos -----------------------------------------
extern int    sample_rate;    // Hz actuales del stream
extern int    num_channels;   // canales actuales del stream
extern int    buf_size;       // frames por buffer actuales
extern double vmin;           // minimo del rango de salida del programa
extern double vmax;           // maximo del rango de salida del programa
extern int    vint_mode;      // 1 = aritmetica entera (bytebeat), 0 = float

// -- Procesamiento de salida --------------------------------------------
// DC blocker: elimina componente continua de la salida (protege parlantes).
// 1 = activado (default), 0 = desactivado.
extern int dcb_enabled;

// Saturacion suave: 0 = clamp duro en [-1,1] (default, bytebeat puro),
// 1 = tanh() (suaviza los picos, suena mas musical).
extern int sat_enabled;

// Filtro pasa-bajos de un polo (suavizado). 0 = desactivado (default).
// Valores 1..99: cantidad de suavizado (mayor = mas suave).
extern int lp_amount;

// Mute: 0 = audio activo (default), 1 = silencio en la salida de audio.
// El scope y la grabacion siguen recibiendo las muestras procesadas.
extern int mute_enabled;

// -- Visualizador de onda (scope) ----------------------------------------
extern int scope_enabled;
extern int scope_zoom;                    // ventana temporal en ms
extern int scope_color[MAX_CHANNELS][3];  // RGB por canal
extern int scope_stroke[MAX_CHANNELS];    // grosor de trazo por canal
extern int scope_resolution;              // pixels por columna del scope (1 = res completa)
extern int scope_fft_size;                // tamaño de FFT (256/512/1024/2048)
extern int scope_pos_x, scope_pos_y;      // posicion deseada; INT32_MIN = sin set (centrar)
extern int scope_size_w, scope_size_h;    // tamanio deseado; 0 = default 800x400
extern int scope_fullscreen;              // 0 = ventana normal (default), 1 = fullscreen
extern int scope_decorated;               // 1 = con decoracion/bordes (default)
extern int scope_bg_color[3];             // RGB del fondo del scope (default: negro)
extern int scope_always_on_top;           // 0 = normal, 1 = siempre arriba
// scope_type: 0=columnas min/max, 1=XY Lissajous, 2=fosforo,
//             3=espectrograma, 4=espectrograma intercalado,
//             5=circular polar, 6=waveform (polilinea interpolada)
extern int scope_type;

// -- Delay (eco con feedback) ---------------------------------------------
extern int delay_enabled;   // 0 = off (default), 1 = on
extern int delay_time;      // tiempo de delay en ms (default 200)
extern int delay_feed;      // ganancia de feedback, 0-100 (default 50)
extern int delay_mix;       // mezcla seca/húmeda, 0-100 (default 50)
extern int delay_feed_lo;   // frecuencia (Hz) del pasa-bajos en el feedback, 0 = off
extern int delay_feed_hi;   // frecuencia (Hz) del pasa-altos en el feedback, 0 = off

extern int smp_crossfade_len;   // samples de crossfade en loop de samples (0 = off)

// Parametros pendientes de aplicar. Un setter (SR/CHMAX/BUF) los escribe y
// marca stream_dirty; el loop principal los aplica entre buffers (cerrando y
// reabriendo el stream de PortAudio) para no hacerlo a mitad de un callback.
// -1 significa "sin cambio pendiente".
extern int pending_sample_rate;
extern int pending_num_channels;
extern int pending_buf_size;
extern int stream_dirty;

// -- Tiempo ---------------------------------------------------------------
// Contador global de muestras. Crece una unidad por cada muestra emitida.
// Es int64 para no desbordarse en sesiones largas; al empujarlo a la pila
// (OP_T) en modo entero se envuelve a 32 bits como en el bytebeat clasico.
extern int64_t sample_index;

// -- Salida por muestra ---------------------------------------------------
// Buffer donde OUT va dejando el valor de cada canal para la muestra que se
// esta evaluando. Se limpia al silencio (punto medio del rango) al empezar
// cada muestra; los canales a los que no se les hizo OUT quedan en silencio.
extern double sample_out[MAX_CHANNELS];

// -- Control de ejecucion -------------------------------------------------
extern int running;   // 0 corta el loop principal (lo pone `bye`)

// Restablece toda la configuracion del sistema a sus valores por defecto
// (los mismos que al arrancar). No toca diccionario, strtab ni samples:
// eso es codigo/contenido, no configuracion. SR/CH/BUF se ponen como
// cambio pendiente para que el loop reabra el stream entre buffers.
void state_reset_defaults(void);

// Modo estricto: 0 = off (default); 1 = la primera anomalia detiene la
// sesion con un reporte completo por stderr (ver interpreter.c).
extern int strict_enabled;

// Flag de recarga forzada (palabra `reload`): lo pone el interprete, lo
// consume el loop principal una vez por vuelta.
extern int force_reload;

// Modo bloques en cascada (palabra BCASC, flag --bcasc): con él activo,
// cada carga reemplaza solo los bloques que el archivo define; el resto
// conserva el Program de su propia carga. Ver parser.c (route_blocks).
extern int bcasc_mode;

#endif // STATE_H
