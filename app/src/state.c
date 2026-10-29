// -------------------------------------------------------------------------
// state.c — Definicion y valores iniciales del estado global.
// -------------------------------------------------------------------------
#include "state.h"

// -- Programas cargados ---------------------------------------------------
Program prog_app       = {0};
Program prog_app_post  = {0};
Program prog_buf       = {0};
Program prog_buf_post  = {0};
Program prog_sample    = {0};

char rpn_source[RPN_BUF_SIZE] = {0};
int  needs_reload = 1;
int bcasc_mode = 0;

// -- Parametros de audio activos -----------------------------------------
int    sample_rate  = DEFAULT_SAMPLE_RATE;
int    num_channels = DEFAULT_CHANNELS;
int    buf_size     = DEFAULT_BUF_SIZE;
double vmin         = DEFAULT_VMIN;
double vmax         = DEFAULT_VMAX;
int    vint_mode    = 1;   // bytebeat entero por defecto
int dcb_enabled  = 1;   // DC blocker activo por defecto (proteccion)
int sat_enabled  = 0;   // clamp duro por defecto (estetica bytebeat)
int lp_amount    = 0;   // sin suavizado por defecto
int mute_enabled = 0;   // audio activo por defecto

int scope_enabled = 0;
int scope_zoom    = 100;   // 100 ms por pantalla por defecto
int scope_color[MAX_CHANNELS][3];   // se inicializa en main.c
int scope_stroke[MAX_CHANNELS];     // se inicializa en main.c
int scope_type = 0;
int scope_fft_size = 512;
int scope_resolution = 1;
int scope_pos_x = INT32_MIN;   // sin set: centrar al crear
int scope_pos_y = INT32_MIN;
int scope_size_w = 0;          // 0 = SCOPE_DEFAULT_W
int scope_size_h = 0;          // 0 = SCOPE_DEFAULT_H
int scope_fullscreen = 0;
int scope_decorated = 1;
int scope_bg_color[3] = {0, 0, 0};
int scope_always_on_top = 0;

int delay_enabled = 0;
int delay_time    = 200;
int delay_feed    = 50;
int delay_mix     = 50;
int delay_feed_lo = 0;   // desactivado por defecto
int delay_feed_hi = 0;   // desactivado por defecto

int smp_crossfade_len = 0;

int pending_sample_rate  = -1;
int pending_num_channels = -1;
int pending_buf_size     = -1;
int stream_dirty         = 0;

// -- Errores - Anomalías - Freno
int strict_enabled = 0;

// -- Tiempo ---------------------------------------------------------------
int64_t sample_index = 0;

// -- Salida por muestra ---------------------------------------------------
double sample_out[MAX_CHANNELS] = {0};

void state_reset_defaults(void) {
    // Stream: como los setters, via pendientes + reopen entre buffers,
    // pero solo si efectivamente difieren (evita un corte al pedo).
    if (sample_rate  != DEFAULT_SAMPLE_RATE)  { pending_sample_rate  = DEFAULT_SAMPLE_RATE;  stream_dirty = 1; }
    if (num_channels != DEFAULT_CHANNELS)     { pending_num_channels = DEFAULT_CHANNELS;     stream_dirty = 1; }
    if (buf_size     != DEFAULT_BUF_SIZE)     { pending_buf_size     = DEFAULT_BUF_SIZE;     stream_dirty = 1; }
    vmin = DEFAULT_VMIN;
    vmax = DEFAULT_VMAX;
    vint_mode = 1;
    dcb_enabled = 1;
    sat_enabled = 0;
    lp_amount   = 0;
    mute_enabled = 0;
    scope_enabled = 0;
    scope_zoom    = 100;
    scope_type    = 0;
    scope_fft_size = 512;
    scope_resolution = 1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        scope_color[i][0] = 255; scope_color[i][1] = 255; scope_color[i][2] = 255;
        scope_stroke[i]   = 1;
        sample_out[i]     = 0.0;
    }
    delay_enabled = 0;
    delay_time    = 200;
    delay_feed    = 50;
    delay_mix     = 50;
    delay_feed_lo = 0;
    delay_feed_hi = 0;
    smp_crossfade_len = 0;
    sample_index = 0;
    strict_enabled = 0;
    bcasc_mode = 0;
}

// -- Control de ejecucion -------------------------------------------------
int running = 1;
int force_reload = 0;
