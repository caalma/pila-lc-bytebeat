// -------------------------------------------------------------------------
// scope.c — Visualizador de forma de onda con renderizado incremental.
//
// En vez de redibujar toda la curva en cada frame (lo que produce titileo),
// se mantiene la forma de onda en una textura persistente. Cada frame:
//   1. Se desplaza la textura hacia la izquierda el equivalente a las
//      muestras nuevas que entraron.
//   2. Se dibujan solo las columnas nuevas en el borde derecho.
//   3. Se presenta la textura en la ventana.
//
// Se usa doble textura (ping-pong) porque SDL no permite leer y escribir
// la misma textura en una sola pasada de render.
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <unistd.h>
#include "scope.h"
#include "state.h"
#include "opcodes.h"

// scope_type: 0=columnas min/max, 1=XY Lissajous, 2=fosforo,
//             3=espectrograma, 4=espectrograma intercalado,
//             5=circular polar, 6=waveform (polilinea interpolada)

#define SCOPE_TITLE "Pila LiveCoding ByteBeat [SCOPE]"
#define ICON_FILE "icon/pilalcbb-icon.bmp"
#define SCOPE_DEFAULT_W   800
#define SCOPE_DEFAULT_H   400
#define SCOPE_MARGIN      4
#define SCOPE_NUM_SCALE   2
// -- velo de decaimiento por frame ----------------------------------------
// 1 * (1 - 80/255) < 0.5 → el pixel de delta=1 cae a 0 en un frame.
// Con valores menores queda un halo tenue que se nota en pantallas
// con sharpening (TVs) aunque no en monitores de laptop.
// Alpha 80 es el primer valor que NO deja residuo cuantizado:
#define PHOS_FADE_ALPHA   32

// -- Estado principal -----------------------------------------------------
static SDL_Window   *scope_win = NULL;
static SDL_Renderer *scope_ren = NULL;
static int          scope_open = 0;

// -- Ring buffer por canal ------------------------------------------------
static float ring[MAX_CHANNELS][SCOPE_RING_SIZE];
static int   ring_pos   = 0;
static int   ring_count = 0;

// -- Renderizado incremental: doble textura ping-pong ---------------------
static SDL_Texture *scope_tex[2] = {NULL, NULL};
static int scope_tex_front   = 0;     // indice de la textura "frontal"
static int scope_new_samples = 0;     // muestras nuevas desde el ultimo render
static int scope_last_win_w  = 0;
static int scope_last_win_h  = 0;
static int scope_last_zoom   = 0;
static int scope_last_num_ch = 0;
static int scope_last_sres   = 0;
static int scope_last_type   = 0;
static double scope_scroll_frac = 0.0; // acumulador de scroll fraccional
static int scope_tex_w = 0;
static int scope_tex_h = 0;
static int scope_last_stroke[MAX_CHANNELS];

// -- Modo XY: buffer de vertices para la polilinea ------------------------
// Declarado aca (y no junto a scope_render_xy) porque scope_shutdown lo
// libera y aparece antes en el archivo.
static SDL_Point *xy_pts = NULL;
static int        xy_pts_cap = 0;

// -- Modo fosforo : ultimo punto dibujado para continuidad ----------------
static int phos_last_x = 0;
static int phos_last_y = 0;
static int phos_have_last = 0;


// -- Fuente bitmap 5x7 para digitos 0-9 -----------------------------------
static const uint8_t font_digits[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
  {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
};

// -- Estado del espectrograma --
#define SPEC_MAX_FFT   2048
#define SPEC_MAX_ROWS  1024
#define SPEC_FMIN      20.0       // frecuencia mínima del eje log
static double spec_re[SPEC_MAX_FFT];
static double spec_im[SPEC_MAX_FFT];
static double spec_mag[SPEC_MAX_FFT / 2];
static int spec_row_map[SPEC_MAX_ROWS];
static int spec_last_fft_size = 0;
static int spec_last_row_h = 0;
static int spec_last_sr = 0;
static double spec_scroll_frac = 0.0;
static SDL_Texture *spec_strip = NULL;
static int spec_strip_w = 0;
static int spec_strip_h = 0;
static int spec_hann_n = 0;
static double spec_hann[SPEC_MAX_FFT];

// -- Modo circular: espectrometro polar ------------------------------------
#define CIRC_STEPS        348
#define CIRC_GAP_DEG     12.0
#define CIRC_THREAD_FRAC  0.3    // radio del hilo, como fraccion del radio de celda
#define CIRC_SMOOTH_ALPHA 0.3    // suavizado temporal por bin
#define CIRC_PEAK_DECAY  0.97    // decaimiento del peak-hold
#define CIRC_MARGIN_PX   4       // padding entre el circulo y el borde de celda
#define CIRC_DB_FLOOR  (-48.0)   // dB considerados silencio (mas generoso que -60)
#define CIRC_GAMMA     0.6       // gamma < 1 estira las magnitudes medias hacia afuera

static double circ_smooth[MAX_CHANNELS][CIRC_STEPS];
static double circ_peak[MAX_CHANNELS][CIRC_STEPS];
static int    circ_bin_map[CIRC_STEPS];
static int    circ_last_fft_size = 0;
static int    circ_last_sr = 0;
static int    circ_last_num_ch = 0;


// Escribe un pixel en el strip streaming. La textura es ABGR8888, cuyo
// orden de bytes en memoria es R,G,B,A en little-endian (en big-endian
// se invierte; el guard lo cubre).
static inline void spec_put_pixel(uint8_t *px, uint8_t r, uint8_t g, uint8_t b) {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
  px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
#else
  px[0] = 255; px[1] = b; px[2] = g; px[3] = r;
#endif
}

// -- Prototipos internos (definidos mas abajo) -----------------------------
static void draw_digit(int digit, int x, int y, int scale, int r, int g, int b);
static void draw_channel_number(int channel, int x, int y, int r, int g, int b);
static void fft_radix2(double *re, double *im, int n);
static void spec_build_hann(int n);
static void circ_build_bin_map(int fft_size, int sr);
static void circ_reset_smoothing(void);

// -- Parseo de color hex ---------------------------------------------------
int scope_parse_color(const char *hex, int *r, int *g, int *b) {
  if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0;
  unsigned int rv, gv, bv;
  if (sscanf(hex + 1, "%2x%2x%2x", &rv, &gv, &bv) != 3) return 0;
  *r = (int)rv; *g = (int)gv; *b = (int)bv;
  return 1;
}

void scope_set_color(int channel, int r, int g, int b) {
  if (channel < 0 || channel >= MAX_CHANNELS) return;
  scope_color[channel][0] = r;
  scope_color[channel][1] = g;
  scope_color[channel][2] = b;
}

void scope_set_stroke(int channel, int width) {
  if (channel < 0 || channel >= MAX_CHANNELS) return;
  if (width < 1) width = 1;
  //if (width > 8) width = 8;
  scope_stroke[channel] = width;
}

// -- Geometria de ventana --------------------------------------------------
int scope_screen_size(int *w, int *h) {
  if (!SDL_WasInit(SDL_INIT_VIDEO)) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) { *w = 0; *h = 0; return 0; }
  }
  int disp = scope_win ? SDL_GetWindowDisplayIndex(scope_win) : 0;
  if (disp < 0) disp = 0;
  SDL_Rect rect;
  if (SDL_GetDisplayBounds(disp, &rect) != 0) { *w = 0; *h = 0; return 0; }
  *w = rect.w;
  *h = rect.h;
  return 1;
}

static void scope_apply_pos(void) {
  if (!scope_win) return;
  if (scope_pos_x == INT32_MIN && scope_pos_y == INT32_MIN) return;
  int cx, cy;
  SDL_GetWindowPosition(scope_win, &cx, &cy);
  SDL_SetWindowPosition(scope_win,
                        scope_pos_x != INT32_MIN ? scope_pos_x : cx,
                        scope_pos_y != INT32_MIN ? scope_pos_y : cy);
}

static void scope_apply_size(void) {
  if (!scope_win) return;
  if (scope_fullscreen) return;   // en fullscreen el tamanio lo pone el desktop
  int cw, ch;
  SDL_GetWindowSize(scope_win, &cw, &ch);
  SDL_SetWindowSize(scope_win,
                    scope_size_w > 0 ? scope_size_w : cw,
                    scope_size_h > 0 ? scope_size_h : ch);
}

void scope_set_pos_x(int x) { scope_pos_x = x; scope_apply_pos(); }
void scope_set_pos_y(int y) { scope_pos_y = y; scope_apply_pos(); }
void scope_set_size_w(int w) { if (w < 1) w = 1; scope_size_w = w; scope_apply_size(); }
void scope_set_size_h(int h) { if (h < 1) h = 1; scope_size_h = h; scope_apply_size(); }

void scope_set_fullscreen(int on) {
  scope_fullscreen = on ? 1 : 0;
  if (!scope_win) return;
  SDL_SetWindowFullscreen(scope_win,
                          scope_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (!scope_fullscreen) { scope_apply_pos(); scope_apply_size(); }
}

void scope_set_decorated(int on) {
  scope_decorated = on ? 1 : 0;
  if (scope_win)
    SDL_SetWindowBordered(scope_win, scope_decorated ? SDL_TRUE : SDL_FALSE);
}

// -- Color de fondo y siempre-arriba ------------------------------------
// Helper para todos los puntos de limpieza: respeta scope_bg_color.
static void scope_clear_bg(void) {
  SDL_SetRenderDrawColor(scope_ren,
                         (Uint8)scope_bg_color[0],
                         (Uint8)scope_bg_color[1],
                         (Uint8)scope_bg_color[2], 255);
  SDL_RenderClear(scope_ren);
}

void scope_set_bg_color(int r, int g, int b) {
  scope_bg_color[0] = r;
  scope_bg_color[1] = g;
  scope_bg_color[2] = b;
}

void scope_set_always_on_top(int on) {
  scope_always_on_top = on ? 1 : 0;
  if (!scope_win) return;
#if SDL_VERSION_ATLEAST(2, 0, 16)
  SDL_SetWindowAlwaysOnTop(scope_win,
                           scope_always_on_top ? SDL_TRUE : SDL_FALSE);
#else
  fprintf(stderr, "  SWINTOP | SDL < 2.0.16, no soporta always-on-top en caliente\n");
#endif
}

void scope_reset_defaults(void) {
  scope_pos_x = INT32_MIN;
  scope_pos_y = INT32_MIN;
  scope_size_w = 0;
  scope_size_h = 0;
  scope_bg_color[0] = 0;
  scope_bg_color[1] = 0;
  scope_bg_color[2] = 0;
  scope_set_always_on_top(0);
  scope_set_fullscreen(0);
  scope_set_decorated(1);
  circ_reset_smoothing();
  circ_last_num_ch = 0;
}

int scope_get_winx(void) {
  if (!scope_win) return (scope_pos_x == INT32_MIN) ? 0 : scope_pos_x;
  int x, y; SDL_GetWindowPosition(scope_win, &x, &y);
  return x;
}
int scope_get_winy(void) {
  if (!scope_win) return (scope_pos_y == INT32_MIN) ? 0 : scope_pos_y;
  int x, y; SDL_GetWindowPosition(scope_win, &x, &y);
  return y;
}
int scope_get_winw(void) {
  if (!scope_win) return scope_size_w > 0 ? scope_size_w : SCOPE_DEFAULT_W;
  int w, h; SDL_GetWindowSize(scope_win, &w, &h);
  return w;
}
int scope_get_winh(void) {
  if (!scope_win) return scope_size_h > 0 ? scope_size_h : SCOPE_DEFAULT_H;
  int w, h; SDL_GetWindowSize(scope_win, &w, &h);
  return h;
}


// -- Icono de la ventana ---------------------------------------------------
// SDL2 core solo carga BMP (SDL_LoadBMP), asi que el icono del proyecto
// vive como .bmp de 32 bits con alpha y no hace falta SDL_image.
// Se prueba primero relativo al cwd (como se ejecuta desde la raiz del
// proyecto con make run o ./activar) y, si falla, relativo al directorio
// del ejecutable via /proc/self/exe, para que el icono sobreviva aunque
// el binario se corra desde cualquier otro cwd.
static void scope_set_window_icon(void) {
  if (!scope_win) return;
  SDL_Surface *surf = SDL_LoadBMP(ICON_FILE);
  if (!surf) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
      exe[n] = '\0';
      char *slash = strrchr(exe, '/');
      if (slash) {
        *slash = '\0';
        char path[4300];
        snprintf(path, sizeof(path), "%s/" ICON_FILE, exe);
        surf = SDL_LoadBMP(path);
      }
    }
  }
  if (surf) {
    SDL_SetWindowIcon(scope_win, surf);
    SDL_FreeSurface(surf);
  } else {
    fprintf(stderr, "    Scope | Aviso: no se pudo cargar el icono '%s'\n",
            ICON_FILE);
  }
}

// -- Gestión de ventana ----------------------------------------------------
int scope_init(void) {
  if (scope_open) return 1;
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "    Scope | Error iniciando SDL: %s\n", SDL_GetError());
    return 0;
  }

  int w = scope_size_w > 0 ? scope_size_w : SCOPE_DEFAULT_W;
  int h = scope_size_h > 0 ? scope_size_h : SCOPE_DEFAULT_H;
  Uint32 wflags = SDL_WINDOW_RESIZABLE;
  if (scope_always_on_top) {
#if SDL_VERSION_ATLEAST(2, 0, 16)
    wflags |= SDL_WINDOW_ALWAYS_ON_TOP;
#endif
  }
  scope_win = SDL_CreateWindow(SCOPE_TITLE,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w, h, wflags);

  if (!scope_win) {
    fprintf(stderr, "    Scope | Error creando ventana: %s\n", SDL_GetError());
    SDL_Quit();
    return 0;
  }

  scope_set_window_icon();

  scope_ren = SDL_CreateRenderer(scope_win, -1, SDL_RENDERER_ACCELERATED);
  if (!scope_ren) {
    fprintf(stderr, "    Scope | Error creando renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(scope_win);
    scope_win = NULL;
    SDL_Quit();
    return 0;
  }
  // Geometria guardada por SPOSX/SPOSY/SPOSW/SPOSH/SDECOR/SFULLSCR
  if (!scope_decorated) SDL_SetWindowBordered(scope_win, SDL_FALSE);
  scope_apply_pos();
  if (scope_fullscreen)
    SDL_SetWindowFullscreen(scope_win, SDL_WINDOW_FULLSCREEN_DESKTOP);

  scope_open = 1;
  ring_pos   = 0;
  ring_count = 0;
  scope_new_samples = 0;
  scope_scroll_frac = 0.0;
  return 1;
}

void scope_shutdown(void) {
  if (!scope_open) return;
  for (int i = 0; i < 2; i++) {
    if (scope_tex[i]) { SDL_DestroyTexture(scope_tex[i]); scope_tex[i] = NULL; }
  }
  if (xy_pts) { free(xy_pts); xy_pts = NULL; xy_pts_cap = 0; }
  if (scope_ren) { SDL_DestroyRenderer(scope_ren); scope_ren = NULL; }
  if (scope_win) { SDL_DestroyWindow(scope_win);   scope_win = NULL; }
  if (spec_strip) { SDL_DestroyTexture(spec_strip); spec_strip = NULL; }
  SDL_Quit();
  scope_open = 0;
}

int scope_is_open(void) { return scope_open; }

void scope_reset(void) {
  ring_pos   = 0;
  ring_count = 0;
  scope_new_samples = 0;
  scope_scroll_frac = 0.0;
  // Forzar redibujo completo en el proximo render
  scope_last_win_w  = 0;
  scope_last_win_h  = 0;
  scope_last_zoom   = 0;
  scope_last_num_ch = 0;
  scope_last_sres   = 0;
  phos_have_last = 0;
}

// -- Ring buffer -----------------------------------------------------------
// El ring se alimenta SIEMPRE, aun con la ventana cerrada: smplive y
// cualquier analisis futuro viven de aca; solo el render exige ventana
// abierta. scope_new_samples en cambio solo lo consume el render, asi que
// solo acumula con el scope abierto (evita desborde con horas de scope off).
void scope_push(const float *samples, int frames, int channels) {
    if (!samples) return;
    for (int f = 0; f < frames; f++) {
        for (int ch = 0; ch < channels && ch < MAX_CHANNELS; ch++) {
            ring[ch][ring_pos] = samples[f * channels + ch];
        }
        ring_pos = (ring_pos + 1) % SCOPE_RING_SIZE;
        if (ring_count < SCOPE_RING_SIZE) ring_count++;
    }
    if (scope_open) scope_new_samples += frames;
}

int scope_capture_channel(int channel, float *dst, int n) {
    if (!dst || n <= 0) return 0;
    if (channel < 0 || channel >= MAX_CHANNELS) return 0;
    int avail = (ring_count < n) ? ring_count : n;
    int pad   = n - avail;
    for (int i = 0; i < pad; i++) dst[i] = 0.0f;
    int base = (ring_pos - avail + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
    for (int i = 0; i < avail; i++)
        dst[pad + i] = ring[channel][(base + i) % SCOPE_RING_SIZE];
    return n;
}

// -- Texturas --------------------------------------------------------------
// Crea o recrea las dos texturas ping-pong. Devuelve 1 si ambas se crearon
// exitosamente, 0 si hubo algun fallo (las texturas quedan en NULL y el
// llamador debe abstenerse de renderizar).
static int scope_create_textures(int win_w, int win_h) {
  for (int i = 0; i < 2; i++) {
    if (scope_tex[i]) {
      SDL_DestroyTexture(scope_tex[i]);
      scope_tex[i] = NULL;
    }
  }
  for (int i = 0; i < 2; i++) {
    scope_tex[i] = SDL_CreateTexture(scope_ren, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET, win_w, win_h);
    if (!scope_tex[i]) {
      fprintf(stderr, "    Scope | Error creando textura %d: %s\n", i, SDL_GetError());
      // Limpiar la otra si ya se creo
      if (i == 1 && scope_tex[0]) {
        SDL_DestroyTexture(scope_tex[0]);
        scope_tex[0] = NULL;
      }
      scope_tex_w = 0;
      scope_tex_h = 0;
      return 0;
    }
  }
  scope_tex_front = 0;
  scope_tex_w = win_w;
  scope_tex_h = win_h;
  return 1;
}

// -- Dibujo de columnas ----------------------------------------------------
// Dibuja las columnas [start_col, start_col + num_cols) para todos los
// canales. Se usa tanto en el redibujo completo como en el incremental.
static void scope_draw_columns(int win_w, int win_h, int start_col, int num_cols) {
  int display_samples = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (display_samples < 2)                display_samples = 2;
  if (display_samples > ring_count)       display_samples = ring_count;
  if (display_samples > SCOPE_RING_SIZE)  display_samples = SCOPE_RING_SIZE;

  int res = scope_resolution;
  if (res < 1) res = 1;
  int total_cols = win_w / res;
  if (total_cols < 1) total_cols = 1;
  double samples_per_col = (double)display_samples / total_cols;

  int row_h = (num_channels > 0) ? win_h / num_channels : win_h;
  if (row_h < 1) row_h = 1;

  int end_col = start_col + num_cols;
  if (end_col > total_cols) end_col = total_cols;

  for (int ch = 0; ch < num_channels; ch++) {
    int row_top  = ch * row_h;
    int row_bot  = row_top + row_h;
    int center_y = (row_top + row_bot) / 2;
    int half_h   = (row_h - SCOPE_MARGIN * 2) / 2;
    if (half_h < 1) half_h = 1;

    int cr = scope_color[ch][0];
    int cg = scope_color[ch][1];
    int cb = scope_color[ch][2];
    int sw = scope_stroke[ch];

    SDL_SetRenderDrawColor(scope_ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);

    for (int col = start_col; col < end_col; col++) {
      int x = col * res;
      int idx_start = (int)((double)col * samples_per_col);
      int idx_end   = (int)((double)(col + 1) * samples_per_col);
      if (idx_end <= idx_start) idx_end = idx_start + 1;
      if (idx_end > display_samples) idx_end = display_samples;

      float vmin_local = 1.0f, vmax_local = -1.0f;
      for (int s = idx_start; s < idx_end; s++) {
        int ring_idx = (ring_pos - display_samples + s + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
        float v = ring[ch][ring_idx];
        if (v < vmin_local) vmin_local = v;
        if (v > vmax_local) vmax_local = v;
      }

      int y_top = center_y - (int)(vmax_local * half_h);
      int y_bot = center_y - (int)(vmin_local * half_h);
      if (y_top > y_bot) { int tmp = y_top; y_top = y_bot; y_bot = tmp; }
      if (y_top == y_bot) y_bot = y_top + 1;

      for (int off = -(sw / 2); off < sw - sw / 2; off++) {
        SDL_RenderDrawLine(scope_ren, x, y_top + off, x, y_bot + off);
      }
    }
  }
}

// -- Redibujo completo -----------------------------------------------------
static void scope_full_redraw(int win_w, int win_h) {
  // Recrear texturas si no existen o si el tamaño de la ventana cambió
  if (!scope_tex[0] || !scope_tex[1] ||
      scope_tex_w != win_w || scope_tex_h != win_h) {
    scope_create_textures(win_w, win_h);
  }

  SDL_SetRenderTarget(scope_ren, scope_tex[scope_tex_front]);

  scope_clear_bg();

  int res = scope_resolution;
  if (res < 1) res = 1;
  int total_cols = win_w / res;
  if (total_cols < 1) total_cols = 1;
  scope_draw_columns(win_w, win_h, 0, total_cols);

  SDL_SetRenderTarget(scope_ren, NULL);

  scope_last_win_w  = win_w;
  scope_last_win_h  = win_h;
  scope_last_zoom   = scope_zoom;
  scope_last_num_ch = num_channels;
  scope_last_sres   = scope_resolution;
  for (int ch = 0; ch < MAX_CHANNELS; ch++)
    scope_last_stroke[ch] = scope_stroke[ch];
  scope_scroll_frac = 0.0;
}

// -- Actualización incremental ---------------------------------------------
static void scope_incremental_update(int win_w, int win_h) {
  if (scope_new_samples <= 0) return;

  // Resguardo: si las texturas no coinciden con la ventana, redibujo completo
  if (scope_tex_w != win_w || scope_tex_h != win_h) {
    scope_full_redraw(win_w, win_h);
    return;
  }

  int display_samples = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (display_samples < 2)                display_samples = 2;
  if (display_samples > ring_count)       display_samples = ring_count;
  if (display_samples > SCOPE_RING_SIZE)  display_samples = SCOPE_RING_SIZE;

  double samples_per_pixel = (double)display_samples / win_w;
  double scroll_pixels_f = (double)scope_new_samples / samples_per_pixel;
  scope_scroll_frac += scroll_pixels_f;
  int scroll_pixels = (int)scope_scroll_frac;
  if (scroll_pixels <= 0) return;
  scope_scroll_frac -= scroll_pixels;

  if (scroll_pixels >= win_w) {
    scope_full_redraw(win_w, win_h);
    return;
  }

  int back = 1 - scope_tex_front;

  SDL_SetRenderTarget(scope_ren, scope_tex[back]);
  scope_clear_bg();

  SDL_Rect src = {scroll_pixels, 0, win_w - scroll_pixels, win_h};
  SDL_Rect dst = {0, 0, win_w - scroll_pixels, win_h};
  SDL_RenderCopy(scope_ren, scope_tex[scope_tex_front], &src, &dst);

  int res = scope_resolution;
  if (res < 1) res = 1;
  int total_cols = win_w / res;
  if (total_cols < 1) total_cols = 1;

  int redraw_start_x = win_w - scroll_pixels - res;
  if (redraw_start_x < 0) redraw_start_x = 0;
  int start_col = redraw_start_x / res;
  int num_new_cols = total_cols - start_col;
  if (num_new_cols > 0) {
    scope_draw_columns(win_w, win_h, start_col, num_new_cols);
  }

  scope_tex_front = back;
  SDL_SetRenderTarget(scope_ren, NULL);
}


// -- Renderizado modo XY (Lissajous) -------------------------------------
// Una polilinea por pasada de grosor (SDL_RenderDrawLines) en vez de una
// llamada por segmento: el costo deja de depender del sample rate y del
// zoom. El stride acota los vertices a ~16k: mas puntos en una ventana de
// ~800px no agregan informacion visual.
static int xy_ensure_cap(int n) {
  if (n <= xy_pts_cap) return 1;
  free(xy_pts);
  xy_pts = (SDL_Point *)malloc(sizeof(SDL_Point) * (size_t)n);
  if (!xy_pts) { xy_pts_cap = 0; return 0; }
  xy_pts_cap = n;
  return 1;
}

static void scope_render_xy(int win_w, int win_h) {
  if (!scope_tex[scope_tex_front]) return;  // Textura no disponible
  if (!scope_ren) return;  // Renderer no disponible

  int display_samples = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (display_samples < 2)                display_samples = 2;
  if (display_samples > ring_count)       display_samples = ring_count;
  if (display_samples > SCOPE_RING_SIZE)  display_samples = SCOPE_RING_SIZE;
  if (display_samples < 2) return;

  // Techo duro de vertices, independiente del zoom y del sample rate.
  // Division hacia arriba (ceil) para no superar nunca max_pts.
  const int max_pts = 4096;
  int stride = (display_samples + max_pts - 1) / max_pts;
  if (stride < 1) stride = 1;
  int n_pts = display_samples / stride + 1;
  if (!xy_ensure_cap(n_pts)) return;

  // Verificar que el render target se pueda setear correctamente
  if (SDL_SetRenderTarget(scope_ren, scope_tex[scope_tex_front]) != 0) {
    fprintf(stderr, "    Scope | Error seteando render target: %s\n", SDL_GetError());
    return;
  }

  SDL_SetRenderDrawColor(scope_ren, 0, 0, 0, 255);
  SDL_RenderClear(scope_ren);

  int center_x = win_w / 2;
  int center_y = win_h / 2;
  int half_w = win_w / 2 - SCOPE_MARGIN;
  int half_h = win_h / 2 - SCOPE_MARGIN;
  if (half_w < 1) half_w = 1;
  if (half_h < 1) half_h = 1;

  SDL_SetRenderDrawColor(scope_ren, 40, 40, 40, 255);
  SDL_RenderDrawLine(scope_ren, center_x, 0, center_x, win_h);
  SDL_RenderDrawLine(scope_ren, 0, center_y, win_w, center_y);

  int cr = scope_color[0][0];
  int cg = scope_color[0][1];
  int cb = scope_color[0][2];
  int sw = scope_stroke[0];
  SDL_SetRenderDrawColor(scope_ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);

  int base = (ring_pos - display_samples + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;

  for (int off = -(sw / 2); off < sw - sw / 2; off++) {
    int n = 0;

    for (int s = 0; s < display_samples && n < xy_pts_cap; s += stride) {
      int ri = base + s;
      if (ri >= SCOPE_RING_SIZE) ri -= SCOPE_RING_SIZE;
      double vx = ring[0][ri];
      double vy = ring[1][ri];
      if (!isfinite(vx)) vx = 0.0;
      if (!isfinite(vy)) vy = 0.0;
      xy_pts[n].x = center_x + (int)(vx * half_w);
      xy_pts[n].y = center_y - (int)(vy * half_h) + off;
      n++;
    }

    if (n < xy_pts_cap) {
      int ri = (ring_pos - 1 + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
      xy_pts[n].x = center_x + (int)(ring[0][ri] * half_w);
      xy_pts[n].y = center_y - (int)(ring[1][ri] * half_h) + off;
      n++;
    }
    // Verificacion defensiva antes de dibujar
    if (n >= 2 && xy_pts && scope_ren) {
      SDL_RenderDrawLines(scope_ren, xy_pts, n);
    }
  }

  SDL_SetRenderTarget(scope_ren, NULL);
}

// -- Renderizado modo fosforo ----------------------------------------------
// Osciloscopio de fosforo: la textura persiste entre frames y cada render
// (1) pasa un velo translucido del color de fondo que apaga exponencialmente
// las estelas viejas, y (2) dibuja SOLO los segmentos nuevos (las muestras
// entradas desde el ultimo render) a brillo pleno, conectados al ultimo
// punto dibujado. Costo O(buf_size) por frame: plano respecto del sample
// rate y del zoom, el modo mas barato de todos.
static void scope_render_phosphor(int win_w, int win_h) {
  int n_new = scope_new_samples;
  if (n_new <= 0) return;
  if (n_new > ring_count)      n_new = ring_count;
  if (n_new > SCOPE_RING_SIZE) n_new = SCOPE_RING_SIZE;
  if (!xy_ensure_cap(n_new + 2)) return;

  int center_x = win_w / 2;
  int center_y = win_h / 2;
  int half_w = win_w / 2 - SCOPE_MARGIN;
  int half_h = win_h / 2 - SCOPE_MARGIN;
  if (half_w < 1) half_w = 1;
  if (half_h < 1) half_h = 1;

  // Render in-place sobre la textura frontal (sin ping-pong en este modo)
  SDL_SetRenderTarget(scope_ren, scope_tex[scope_tex_front]);

  // 1) Velo de decaimiento hacia el color de fondo
  SDL_SetRenderDrawBlendMode(scope_ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(scope_ren,
                         (Uint8)scope_bg_color[0], (Uint8)scope_bg_color[1],
                         (Uint8)scope_bg_color[2], PHOS_FADE_ALPHA);
  SDL_RenderFillRect(scope_ren, NULL);
  SDL_SetRenderDrawBlendMode(scope_ren, SDL_BLENDMODE_NONE);

  // 2) Cruz de referencia tenue, redibujada opaca cada frame (queda constante)
  SDL_SetRenderDrawColor(scope_ren, 40, 40, 40, 255);
  SDL_RenderDrawLine(scope_ren, center_x, 0, center_x, win_h);
  SDL_RenderDrawLine(scope_ren, 0, center_y, win_w, center_y);

  // 3) Segmentos nuevos a brillo pleno, con el grosor del canal 0
  SDL_SetRenderDrawColor(scope_ren,
                         (Uint8)scope_color[0][0], (Uint8)scope_color[0][1],
                         (Uint8)scope_color[0][2], 255);
  int sw = scope_stroke[0];
  int start = ring_pos - n_new;

  for (int off = -(sw / 2); off < sw - sw / 2; off++) {
    int n = 0;
    if (phos_have_last) {
      xy_pts[n].x = phos_last_x;
      xy_pts[n].y = phos_last_y + off;
      n++;
    }
    for (int s = 0; s < n_new; s++) {
      int ri = start + s;
      if (ri < 0) ri += SCOPE_RING_SIZE;
      xy_pts[n].x = center_x + (int)(ring[0][ri] * half_w);
      xy_pts[n].y = center_y - (int)(ring[1][ri] * half_h) + off;
      n++;
    }
    if (n >= 2) SDL_RenderDrawLines(scope_ren, xy_pts, n);
  }

  // Recordar el extremo para conectar el frame siguiente
  int ri = (ring_pos - 1 + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
  phos_last_x = center_x + (int)(ring[0][ri] * half_w);
  phos_last_y = center_y - (int)(ring[1][ri] * half_h);
  phos_have_last = 1;

  SDL_SetRenderTarget(scope_ren, NULL);
}

// -- Renderizado modo circular ---------------------------------------------
// Espectrometro polar: una FFT por canal por frame, frecuencia en escala
// log alrededor del perimetro (gap de CIRC_GAP_DEG arriba, con tick de
// origen en 20 Hz). La magnitud en dB se dibuja como "gotas en un hilo":
// cada bin es un segmento CENTRADO en el radio del hilo (R), que se
// extiende hacia adentro y hacia afuera segun su magnitud; el hilo mismo
// se dibuja apagado en el color del canal como referencia de silencio.
// Suavizado temporal por bin, peak-hold con decaimiento como puntitos
// exteriores, y grilla automatica de canales con el numero al centro.
static void scope_render_circular(int win_w, int win_h) {
  int fft_size = scope_fft_size;
  int bins = fft_size / 2;
  int nch = num_channels;
  if (nch < 1) nch = 1;

  // Grilla de canales: cols x rows que cubren nch
  int cols = 1, rows = 1;
  while (cols * rows < nch) {
    if (cols <= rows) cols++; else rows++;
  }
  int cell_w = win_w / cols;
  int cell_h = win_h / rows;
  if (cell_w < 1) cell_w = 1;
  if (cell_h < 1) cell_h = 1;

  circ_build_bin_map(fft_size, sample_rate);
  spec_build_hann(fft_size);

  SDL_SetRenderTarget(scope_ren, scope_tex[scope_tex_front]);
  scope_clear_bg();

  double gap_rad = CIRC_GAP_DEG * M_PI / 180.0;
  double start_angle = -M_PI / 2.0 + gap_rad / 2.0;
  double arc_rad = (360.0 - CIRC_GAP_DEG) * M_PI / 180.0;

  for (int ch = 0; ch < nch; ch++) {
    int col = ch % cols;
    int row = ch / cols;
    int cx = col * cell_w + cell_w / 2;
    int cy = row * cell_h + cell_h / 2;
    int radius = (cell_w < cell_h ? cell_w : cell_h) / 2 - CIRC_MARGIN_PX;
    if (radius < 20) radius = 20;

    // Hilo al 70% del radio; las gotas se extienden E pixels hacia
    // cada lado, asi la magnitud maxima toca justo el borde de celda.
    int R = (int)(radius * CIRC_THREAD_FRAC);
    int E = radius - R;
    if (E < 4) E = 4;

    // FFT del canal (ultimas fft_size muestras)
    for (int i = 0; i < fft_size; i++) {
      int off = fft_size - 1 - i;
      double v = 0.0;
      if (off < ring_count) {
        int ridx = (ring_pos - 1 - off + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
        v = (double)ring[ch][ridx];
      }
      spec_re[i] = v * spec_hann[i];
      spec_im[i] = 0.0;
    }
    fft_radix2(spec_re, spec_im, fft_size);

    for (int k = 0; k < bins; k++) {
      spec_mag[k] = sqrt(spec_re[k]*spec_re[k] + spec_im[k]*spec_im[k])
        * (2.0 / fft_size);
    }

    // Mapeo a pasos angulares con suavizado y peak-hold
    for (int i = 0; i < CIRC_STEPS; i++) {
      int bin = circ_bin_map[i];
      double mag = spec_mag[bin];
      double db = (mag > 1e-6) ? 20.0 * log10(mag) : CIRC_DB_FLOOR;
      if (db < CIRC_DB_FLOOR) db = CIRC_DB_FLOOR;
      if (db > 0.0) db = 0.0;
      // 0..1 entre el piso y 0 dB, y gamma para estirar las medias:
      // sin gamma, un espectro de banda ancha vive pegado al hilo.
      double norm = (db - CIRC_DB_FLOOR) / (-CIRC_DB_FLOOR);
      norm = pow(norm, CIRC_GAMMA);

      circ_smooth[ch][i] += CIRC_SMOOTH_ALPHA * (norm - circ_smooth[ch][i]);

      if (circ_smooth[ch][i] > circ_peak[ch][i]) {
        circ_peak[ch][i] = circ_smooth[ch][i];
      } else {
        circ_peak[ch][i] *= CIRC_PEAK_DECAY;
      }
    }

    int cr = scope_color[ch][0];
    int cg = scope_color[ch][1];
    int cb = scope_color[ch][2];
    int sw = scope_stroke[ch];

    // Hilo circular: el perimetro del que cuelgan las gotas, en el
    // color del canal apagado (un tercio) para identificar sin competir.
    SDL_SetRenderDrawColor(scope_ren,
                           (Uint8)(cr / 3), (Uint8)(cg / 3), (Uint8)(cb / 3), 255);
    for (int a = 0; a < 360; a++) {
      double angle = a * M_PI / 180.0 - M_PI / 2.0;
      int x = cx + (int)(R * cos(angle));
      int y = cy + (int)(R * sin(angle));
      SDL_RenderDrawPoint(scope_ren, x, y);
    }

    // Gotas: segmentos centrados en el hilo, mas peak-hold exterior
    SDL_SetRenderDrawColor(scope_ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);

    for (int i = 0; i < CIRC_STEPS; i++) {
      double frac = (double)i / (CIRC_STEPS - 1);
      double angle = start_angle + frac * arc_rad;
      double ca = cos(angle), sa = sin(angle);

      double mag = circ_smooth[ch][i];
      if (mag > 0.01) {
        int ext = (int)(mag * E);
        if (ext < 1) ext = 1;
        int rin  = R - ext;
        int rout = R + ext;
        int x1 = cx + (int)(rin  * ca);
        int y1 = cy + (int)(rin  * sa);
        int x2 = cx + (int)(rout * ca);
        int y2 = cy + (int)(rout * sa);

        for (int off = -(sw / 2); off < sw - sw / 2; off++) {
          int ox = (int)(off * cos(angle + M_PI / 2.0));
          int oy = (int)(off * sin(angle + M_PI / 2.0));
          SDL_RenderDrawLine(scope_ren, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
        }
      }

      double peak = circ_peak[ch][i];
      if (peak > 0.02 && peak > mag + 0.02) {
        int peak_r = R + (int)(peak * E) + 2;
        int px = cx + (int)(peak_r * ca);
        int py = cy + (int)(peak_r * sa);
        SDL_RenderDrawPoint(scope_ren, px, py);
      }
    }

    // Tick de origen (20 Hz): cruza el hilo en el inicio del arco
    SDL_SetRenderDrawColor(scope_ren, 80, 80, 80, 255);
    int tx1 = cx + (int)((R - 6) * cos(start_angle));
    int ty1 = cy + (int)((R - 6) * sin(start_angle));
    int tx2 = cx + (int)((R + 6) * cos(start_angle));
    int ty2 = cy + (int)((R + 6) * sin(start_angle));
    SDL_RenderDrawLine(scope_ren, tx1, ty1, tx2, ty2);

    // Numero de canal centrado
    int digit_w = 6 * SCOPE_NUM_SCALE;
    int num_digits = (ch >= 10) ? 2 : 1;
    int text_w = digit_w * num_digits;
    int text_h = 7 * SCOPE_NUM_SCALE;
    draw_channel_number(ch, cx - text_w / 2, cy - text_h / 2, cr, cg, cb);
  }

  SDL_SetRenderTarget(scope_ren, NULL);
}

// -- Renderizado modo waveform (polilinea interpolada) ---------------------
// Dibuja una curva continua interpolando linealmente entre muestras, como
// un osciloscopio clasico. Una polilinea por canal, en su propia fila.
// Usa SDL_RenderDrawLines para eficiencia (una llamada por canal).
static void scope_render_waveform(int win_w, int win_h) {
  if (!scope_tex[scope_tex_front]) return;
  if (!scope_ren) return;

  int display_samples = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (display_samples < 2) display_samples = 2;
  if (display_samples > ring_count) display_samples = ring_count;
  if (display_samples > SCOPE_RING_SIZE) display_samples = SCOPE_RING_SIZE;
  if (display_samples < 2) return;

  // Techo de vertices: acotar a ~8k puntos por canal para no saturar
  const int max_pts_per_ch = 8192;
  int stride = (display_samples + max_pts_per_ch - 1) / max_pts_per_ch;
  if (stride < 1) stride = 1;
  int n_pts = display_samples / stride + 1;

  if (!xy_ensure_cap(n_pts)) return;

  if (SDL_SetRenderTarget(scope_ren, scope_tex[scope_tex_front]) != 0) {
    fprintf(stderr, "    Scope | Error seteando render target: %s\n", SDL_GetError());
    return;
  }

  SDL_SetRenderDrawColor(scope_ren, 0, 0, 0, 255);
  SDL_RenderClear(scope_ren);

  int row_h = (num_channels > 0) ? win_h / num_channels : win_h;
  if (row_h < 1) row_h = 1;

  int base = (ring_pos - display_samples + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;

  for (int ch = 0; ch < num_channels; ch++) {
    int row_top = ch * row_h;
    int row_bot = row_top + row_h;
    int center_y = (row_top + row_bot) / 2;
    int half_h = (row_h - SCOPE_MARGIN * 2) / 2;
    if (half_h < 1) half_h = 1;

    int cr = scope_color[ch][0];
    int cg = scope_color[ch][1];
    int cb = scope_color[ch][2];
    int sw = scope_stroke[ch];

    SDL_SetRenderDrawColor(scope_ren, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);

    // Construir array de puntos con interpolacion lineal
    int n = 0;
    for (int s = 0; s < display_samples && n < xy_pts_cap; s += stride) {
      // Posicion fraccional en el ring
      double pos = (double)s / (double)stride;
      int idx0 = (base + (int)pos) % SCOPE_RING_SIZE;
      int idx1 = (idx0 + 1) % SCOPE_RING_SIZE;
      double frac = pos - (double)(int)pos;

      // Interpolacion lineal entre muestras adyacentes
      double v0 = (double)ring[ch][idx0];
      double v1 = (double)ring[ch][idx1];
      if (!isfinite(v0)) v0 = 0.0;
      if (!isfinite(v1)) v1 = 0.0;
      double v = v0 + (v1 - v0) * frac;

      // Mapear a coordenadas de pantalla
      int x = (int)((double)s / (double)display_samples * (double)win_w);
      int y = center_y - (int)(v * half_h);

      xy_pts[n].x = x;
      xy_pts[n].y = y;
      n++;
    }

    // Dibujar polilinea con grosor
    if (n >= 2) {
      for (int off = -(sw / 2); off < sw - sw / 2; off++) {
        // Desplazar todos los puntos verticalmente para el grosor
        for (int i = 0; i < n; i++) {
          xy_pts[i].y += off;
        }
        SDL_RenderDrawLines(scope_ren, xy_pts, n);
        // Restaurar posiciones
        for (int i = 0; i < n; i++) {
          xy_pts[i].y -= off;
        }
      }
    }
  }

  SDL_SetRenderTarget(scope_ren, NULL);
}


// -- FFT radix-2 iterativa in-place ----------------------------------------
static void fft_radix2(double *re, double *im, int n) {
  // Bit-reversal
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double tmp = re[i]; re[i] = re[j]; re[j] = tmp;
      tmp = im[i]; im[i] = im[j]; im[j] = tmp;
    }
  }
  // Mariposas
  for (int len = 2; len <= n; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    double wre = cos(angle);
    double wim = sin(angle);
    for (int i = 0; i < n; i += len) {
      double cur_re = 1.0, cur_im = 0.0;
      for (int j = 0; j < len / 2; j++) {
        double tre = re[i+j+len/2]*cur_re - im[i+j+len/2]*cur_im;
        double tim = re[i+j+len/2]*cur_im + im[i+j+len/2]*cur_re;
        re[i+j+len/2] = re[i+j] - tre;
        im[i+j+len/2] = im[i+j] - tim;
        re[i+j] += tre;
        im[i+j] += tim;
        double nre = cur_re*wre - cur_im*wim;
        double nim = cur_re*wim + cur_im*wre;
        cur_re = nre; cur_im = nim;
      }
    }
  }
}

// -- Precomputar mapeo fila -> bin FFT (escala log) ------------------------
static void spec_build_row_map(int row_h, int fft_size, int sr) {
  if (row_h == spec_last_row_h && fft_size == spec_last_fft_size
      && sr == spec_last_sr) return;
  spec_last_row_h = row_h;
  spec_last_fft_size = fft_size;
  spec_last_sr = sr;
  double fmax = sr / 2.0;
  double log_min = log(SPEC_FMIN);
  double log_max = log(fmax);
  int bins = fft_size / 2;
  for (int y = 0; y < row_h && y < SPEC_MAX_ROWS; y++) {
    // y=0 es la frecuencia máxima (Nyquist), y=row_h-1 es la mínima
    double frac = 1.0 - (double)y / (row_h - 1);
    double freq = exp(log_min + frac * (log_max - log_min));
    int bin = (int)(freq * fft_size / sr + 0.5);
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    spec_row_map[y] = bin;
  }
}

static void spec_build_hann(int n) {
  if (n == spec_hann_n) return;
  spec_hann_n = n;
  for (int i = 0; i < n; i++)
    spec_hann[i] = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
}

static void circ_build_bin_map(int fft_size, int sr) {
  if (fft_size == circ_last_fft_size && sr == circ_last_sr) return;
  circ_last_fft_size = fft_size;
  circ_last_sr = sr;

  double fmax = sr / 2.0;
  double log_min = log(20.0);  // 20 Hz minimo
  double log_max = log(fmax);
  int bins = fft_size / 2;

  for (int i = 0; i < CIRC_STEPS; i++) {
    double frac = (double)i / (CIRC_STEPS - 1);
    double freq = exp(log_min + frac * (log_max - log_min));
    int bin = (int)(freq * fft_size / sr + 0.5);
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    circ_bin_map[i] = bin;
  }
}

static void circ_reset_smoothing(void) {
  memset(circ_smooth, 0, sizeof(circ_smooth));
  memset(circ_peak, 0, sizeof(circ_peak));
}


// -- Renderizado del espectrograma -----------------------------------------
static void scope_render_spectrogram(int win_w, int win_h) {
  int fft_size = scope_fft_size;
  int bins = fft_size / 2;

  // Ventana temporal del zoom (mismo eje de tiempo que el modo normal)
  int ds = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (ds < 2) ds = 2;
  if (ds > SCOPE_RING_SIZE) ds = SCOPE_RING_SIZE;
  double spp = (double)ds / (double)win_w;   // muestras por columna de pixel

  // Cuantas columnas nuevas entran este frame
  spec_scroll_frac += (double)scope_new_samples / spp;
  int scroll_pixels = (int)spec_scroll_frac;
  if (scroll_pixels <= 0) return;            // nada nuevo que dibujar
  spec_scroll_frac -= (double)scroll_pixels;
  if (scroll_pixels > win_w) scroll_pixels = win_w;

  int back = 1 - scope_tex_front;
  SDL_SetRenderTarget(scope_ren, scope_tex[back]);
  scope_clear_bg();
  if (scroll_pixels < win_w) {
    SDL_Rect src = {scroll_pixels, 0, win_w - scroll_pixels, win_h};
    SDL_Rect dst = {0, 0, win_w - scroll_pixels, win_h};
    SDL_RenderCopy(scope_ren, scope_tex[scope_tex_front], &src, &dst);
  }

  int row_h = (num_channels > 0) ? win_h / num_channels : win_h;
  if (row_h < 1) row_h = 1;
  spec_build_row_map(row_h, fft_size, sample_rate);
  spec_build_hann(fft_size);

  // Preparar la franja de columnas nuevas en CPU (strip streaming)
  void *pix = NULL;
  int pitch = 0;
  if (SDL_LockTexture(spec_strip, NULL, &pix, &pitch) != 0) {
    SDL_SetRenderTarget(scope_ren, NULL);
    return;
  }
  uint8_t *base = (uint8_t *)pix;
  int x0 = win_w - scroll_pixels;
  int w  = scroll_pixels;

  // Inicializar la franja en negro opaco
  for (int y = 0; y < win_h; y++) {
    uint8_t *r = base + (size_t)y * pitch + (size_t)x0 * 4;
    for (int lx = 0; lx < w; lx++) {
      spec_put_pixel(r + lx * 4, 0, 0, 0);
    }
  }

  // Una FFT por columna de pixel nueva, por canal
  for (int x = x0; x < win_w; x++) {
    int s = (int)((double)x * spp);        // indice de display de la columna
    int off_latest = ds - 1 - s;           // distancia de la columna al ultimo sample
    for (int ch = 0; ch < num_channels; ch++) {
      int cr = scope_color[ch][0];
      int cg = scope_color[ch][1];
      int cb = scope_color[ch][2];
      // Ventana de fft_size muestras que TERMINA en el tiempo de la columna
      for (int i = 0; i < fft_size; i++) {
        int off = off_latest + (fft_size - 1 - i);
        double v = 0.0;
        if (off >= 0 && off < ring_count) {
          int ridx = (ring_pos - 1 - off + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
          v = (double)ring[ch][ridx];
        }
        spec_re[i] = v * spec_hann[i];
        spec_im[i] = 0.0;
      }
      fft_radix2(spec_re, spec_im, fft_size);
      for (int k = 0; k < bins; k++)
        spec_mag[k] = sqrt(spec_re[k]*spec_re[k] + spec_im[k]*spec_im[k])
          * (2.0 / fft_size);
      int row_top = ch * row_h;
      for (int y = 0; y < row_h; y++) {
        int py = row_top + y;
        if (py >= win_h) break;
        int yy = (y < SPEC_MAX_ROWS) ? y : SPEC_MAX_ROWS - 1;
        double mag = spec_mag[spec_row_map[yy]];
        double db = (mag > 1e-6) ? 20.0 * log10(mag) : -60.0;
        if (db < -60.0) db = -60.0;
        if (db > 0.0) db = 0.0;
        uint8_t g = (uint8_t)((db + 60.0) * (255.0 / 60.0));
        uint8_t *px = base + (size_t)py * pitch + (size_t)x * 4;
        spec_put_pixel(px,
                       (uint8_t)((g * cr) / 255),
                       (uint8_t)((g * cg) / 255),
                       (uint8_t)((g * cb) / 255));
      }
    }
  }
  SDL_UnlockTexture(spec_strip);

  SDL_Rect dstrect = {x0, 0, w, win_h};
  SDL_RenderCopy(scope_ren, spec_strip, &dstrect, &dstrect);

  // Separadores entre franjas de canal
  SDL_SetRenderDrawColor(scope_ren, 40, 40, 40, 255);

  for (int ch = 1; ch < num_channels; ch++)
    SDL_RenderDrawLine(scope_ren, 0, ch * row_h, win_w, ch * row_h);

  scope_tex_front = back;
  SDL_SetRenderTarget(scope_ren, NULL);
}

// -- Espectrograma intercalado ---------------------------------------------
// Cada paso de tiempo ocupa num_channels pixels: el pixel x pertenece al
// canal (x % num_channels) y al paso de tiempo (x / num_channels). Todos
// los canales comparten la altura completa de la ventana (rango de
// frecuencias completo) con su propio color. El scroll se cuantiza a
// multiplos de num_channels para preservar la alineacion canal-pixel; el
// remanente queda en el acumulador fraccional.
static void scope_render_spectrogram_interleaved(int win_w, int win_h) {
  int fft_size = scope_fft_size;
  int bins = fft_size / 2;
  int nch = num_channels;
  if (nch < 1) nch = 1;

  int ds = (int)((double)scope_zoom * sample_rate / 1000.0);
  if (ds < 2) ds = 2;
  if (ds > SCOPE_RING_SIZE) ds = SCOPE_RING_SIZE;

  // Muestras por paso de tiempo (cada paso ocupa nch pixels)
  double steps = (double)win_w / (double)nch;
  double sps   = (double)ds / steps;

  // Scroll crudo en pixels (identico al de la opcion A)
  spec_scroll_frac += (double)scope_new_samples * (double)win_w / (double)ds;
  int total = (int)spec_scroll_frac;
  int scroll_pixels = total - (total % nch);   // multiplo de nch
  if (scroll_pixels <= 0) return;
  spec_scroll_frac -= (double)scroll_pixels;   // conserva el remanente
  if (scroll_pixels > win_w) scroll_pixels = win_w - (win_w % nch);

  int back = 1 - scope_tex_front;
  SDL_SetRenderTarget(scope_ren, scope_tex[back]);
  scope_clear_bg();
  if (scroll_pixels < win_w) {
    SDL_Rect src = {scroll_pixels, 0, win_w - scroll_pixels, win_h};
    SDL_Rect dst = {0, 0, win_w - scroll_pixels, win_h};
    SDL_RenderCopy(scope_ren, scope_tex[scope_tex_front], &src, &dst);
  }

  // Mapa de filas a altura completa (toda la ventana = rango de frec.)
  spec_build_row_map(win_h, fft_size, sample_rate);
  spec_build_hann(fft_size);

  void *pix = NULL;
  int pitch = 0;
  if (SDL_LockTexture(spec_strip, NULL, &pix, &pitch) != 0) {
    SDL_SetRenderTarget(scope_ren, NULL);
    return;
  }
  uint8_t *base = (uint8_t *)pix;
  int x0 = win_w - scroll_pixels;
  int w  = scroll_pixels;

  for (int y = 0; y < win_h; y++) {
    uint8_t *r = base + (size_t)y * pitch + (size_t)x0 * 4;
    for (int lx = 0; lx < w; lx++) spec_put_pixel(r + lx * 4, 0, 0, 0);
  }

  for (int x = x0; x < win_w; x++) {
    int ch = x % nch;
    int k  = x / nch;
    int s  = (int)((double)k * sps);
    int off_latest = ds - 1 - s;
    int cr = scope_color[ch][0];
    int cg = scope_color[ch][1];
    int cb = scope_color[ch][2];

    for (int i = 0; i < fft_size; i++) {
      int off = off_latest + (fft_size - 1 - i);
      double v = 0.0;
      if (off >= 0 && off < ring_count) {
        int ridx = (ring_pos - 1 - off + SCOPE_RING_SIZE) % SCOPE_RING_SIZE;
        v = (double)ring[ch][ridx];
      }
      spec_re[i] = v * spec_hann[i];
      spec_im[i] = 0.0;
    }
    fft_radix2(spec_re, spec_im, fft_size);
    for (int b = 0; b < bins; b++)
      spec_mag[b] = sqrt(spec_re[b]*spec_re[b] + spec_im[b]*spec_im[b])
        * (2.0 / fft_size);

    for (int y = 0; y < win_h; y++) {
      int yy = (y < SPEC_MAX_ROWS) ? y : SPEC_MAX_ROWS - 1;
      double mag = spec_mag[spec_row_map[yy]];
      double db = (mag > 1e-6) ? 20.0 * log10(mag) : -60.0;
      if (db < -60.0) db = -60.0;
      if (db > 0.0) db = 0.0;
      uint8_t g = (uint8_t)((db + 60.0) * (255.0 / 60.0));
      uint8_t *px = base + (size_t)y * pitch + (size_t)x * 4;
      spec_put_pixel(px,
                     (uint8_t)((g * cr) / 255),
                     (uint8_t)((g * cg) / 255),
                     (uint8_t)((g * cb) / 255));
    }
  }
  SDL_UnlockTexture(spec_strip);

  SDL_Rect dstrect = {x0, 0, w, win_h};
  SDL_RenderCopy(scope_ren, spec_strip, &dstrect, &dstrect);

  scope_tex_front = back;
  SDL_SetRenderTarget(scope_ren, NULL);
}

// -- Render principal -------------------------------------------------------
void scope_render(void) {
  if (!scope_open || !scope_ren) return;

  int win_w, win_h;
  SDL_GetRendererOutputSize(scope_ren, &win_w, &win_h);

  // Determinar tipo efectivo (espectrograma requiere al menos 1 canal)
  int effective_type = scope_type;
  if (effective_type == 2 && num_channels < 1) effective_type = 0;
  if (effective_type == 1 && num_channels < 2) effective_type = 0;
  if (effective_type == 2 && num_channels < 2) effective_type = 0;
  if (effective_type == 5 && num_channels < 1) effective_type = 0;

  // Detectar cambios
  int need_full = 0;
  if (!scope_tex[0] || !scope_tex[1])                          need_full = 1;
  if (win_w != scope_last_win_w || win_h != scope_last_win_h)  need_full = 1;
  if (scope_zoom != scope_last_zoom)                           need_full = 1;
  if (num_channels != scope_last_num_ch)                       need_full = 1;
  if (scope_resolution != scope_last_sres)                     need_full = 1;
  if (effective_type != scope_last_type)                       need_full = 1;
  if (scope_fft_size != spec_last_fft_size)                    need_full = 1;
  for (int ch = 0; ch < num_channels && !need_full; ch++) {
    if (scope_stroke[ch] != scope_last_stroke[ch]) need_full = 1;
  }

  // Tope de tasa de render (~40 fps): con buffers chicos o sr altos el
  // loop renderiza mucho mas rapido de lo que el ojo aprovecha y le roba
  // CPU al audio. Saltear frames acumula scope_new_samples y el siguiente
  // render compensa: no se pierde sincronia ni scroll.
  {
    int min_per_render = sample_rate / 40;
    if (min_per_render < 1) min_per_render = 1;
    if (!need_full && scope_new_samples < min_per_render) return;
  }

  if (effective_type == 1) {
    // Modo XY: verificar que las texturas sean validas antes de renderizar
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      if (!scope_create_textures(win_w, win_h)) {
        // Fallo la creacion: saltear este frame
        scope_last_type = effective_type;
        goto present;
      }
    }
    // Verificacion adicional: el renderer y la textura frontal deben ser validos
    if (!scope_ren || !scope_tex[scope_tex_front]) {
      goto present;
    }
    scope_render_xy(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
    scope_scroll_frac = 0.0;
  } else if (effective_type == 2) {
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      scope_create_textures(win_w, win_h);
      phos_have_last = 0;
    }
    if (scope_last_type != effective_type) phos_have_last = 0;
    scope_render_phosphor(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
  } else if (effective_type == 3) {
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      scope_create_textures(win_w, win_h);
    }
    if (!spec_strip || spec_strip_w != win_w || spec_strip_h != win_h) {
      if (spec_strip) SDL_DestroyTexture(spec_strip);
      spec_strip = SDL_CreateTexture(scope_ren, SDL_PIXELFORMAT_ABGR8888,
                                     SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
      if (!spec_strip) {
        fprintf(stderr, "    Scope | Error creando spec_strip: %s\n", SDL_GetError());
        scope_last_type = effective_type;
        goto present;
      }
      spec_strip_w = win_w;
      spec_strip_h = win_h;
    }
    scope_render_spectrogram(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
    spec_scroll_frac = 0.0;
  } else if (effective_type == 4) {
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      scope_create_textures(win_w, win_h);
    }
    if (!spec_strip || spec_strip_w != win_w || spec_strip_h != win_h) {
      if (spec_strip) SDL_DestroyTexture(spec_strip);
      spec_strip = SDL_CreateTexture(scope_ren, SDL_PIXELFORMAT_ABGR8888,
                                     SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
      spec_strip_w = win_w;
      spec_strip_h = win_h;
    }
    // Si cambio la intercalacion (canales), la geometria o el tipo,
    // la historia queda desalineada: limpiar ambas texturas.
    if (num_channels != scope_last_num_ch ||
        scope_last_type != effective_type ||
        win_w != scope_last_win_w || win_h != scope_last_win_h) {
      SDL_SetRenderTarget(scope_ren, scope_tex[0]);
      scope_clear_bg();
      SDL_SetRenderTarget(scope_ren, scope_tex[1]);
      SDL_RenderClear(scope_ren);
      SDL_SetRenderTarget(scope_ren, NULL);
      spec_scroll_frac = 0.0;
    }
    scope_render_spectrogram_interleaved(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
    // Nota: aca NO se resetea spec_scroll_frac; el remanente de la
    // cuantizacion debe conservarse entre frames.
  } else if (effective_type == 5) {
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      scope_create_textures(win_w, win_h);
    }
    if (num_channels != circ_last_num_ch ||
        scope_fft_size != circ_last_fft_size) {
      circ_reset_smoothing();
    }
    scope_render_circular(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
  } else if (effective_type == 6) {
    // Modo waveform: verificar texturas
    if (!scope_tex[0] || !scope_tex[1] ||
        scope_tex_w != win_w || scope_tex_h != win_h) {
      if (!scope_create_textures(win_w, win_h)) {
        scope_last_type = effective_type;
        goto present;
      }
    }
    if (!scope_ren || !scope_tex[scope_tex_front]) {
      goto present;
    }
    scope_render_waveform(win_w, win_h);
    scope_last_win_w  = win_w;
    scope_last_win_h  = win_h;
    scope_last_zoom   = scope_zoom;
    scope_last_num_ch = num_channels;
    scope_last_sres   = scope_resolution;
    scope_last_type   = effective_type;
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
      scope_last_stroke[ch] = scope_stroke[ch];
    scope_scroll_frac = 0.0;
  } else if (need_full) {
    scope_full_redraw(win_w, win_h);
    scope_last_type = effective_type;
  } else if (scope_new_samples > 0) {
    scope_incremental_update(win_w, win_h);
  }

 present:

// Presentar textura
  SDL_SetRenderTarget(scope_ren, NULL);
  scope_clear_bg();
  if (scope_tex[scope_tex_front]) {
    SDL_RenderCopy(scope_ren, scope_tex[scope_tex_front], NULL, NULL);
  }

  // Numeros de canal (modo normal, espectrograma y waveform; en XY no aplican)
  if (effective_type == 0 || effective_type == 3 || effective_type == 6) {
    int row_h = (num_channels > 0) ? win_h / num_channels : win_h;
    for (int ch = 0; ch < num_channels; ch++) {
      int row_top = ch * row_h;
      draw_channel_number(ch, SCOPE_MARGIN + 2, row_top + SCOPE_MARGIN,
                          scope_color[ch][0], scope_color[ch][1], scope_color[ch][2]);
    }
  }

  SDL_RenderPresent(scope_ren);
  scope_new_samples = 0;
}

// -- Eventos ----------------------------------------------------------------
int scope_handle_events(void) {
  if (!scope_open) return 1;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) return 0;
    if (ev.type == SDL_WINDOWEVENT) {
      if (ev.window.event == SDL_WINDOWEVENT_CLOSE) return 0;
      if (ev.window.event == SDL_WINDOWEVENT_RESIZED)
        SDL_RenderSetViewport(scope_ren, NULL);
    }
  }
  return 1;
}

// -- Dibujo de dígitos -------------------------------------------------------
static void draw_digit(int digit, int x, int y, int scale, int r, int g, int b) {
  if (digit < 0 || digit > 9) return;
  SDL_SetRenderDrawColor(scope_ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      if (font_digits[digit][row] & (0x10 >> col)) {
        SDL_Rect rect = {x + col * scale, y + row * scale, scale, scale};
        SDL_RenderFillRect(scope_ren, &rect);
      }
    }
  }
}

static void draw_channel_number(int channel, int x, int y, int r, int g, int b) {
  int scale   = SCOPE_NUM_SCALE;
  int digit_w = 6 * scale;
  if (channel >= 10) {
    draw_digit(channel / 10, x,           y, scale, r, g, b);
    draw_digit(channel % 10, x + digit_w, y, scale, r, g, b);
  } else {
    draw_digit(channel, x, y, scale, r, g, b);
  }
}
