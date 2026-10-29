// -------------------------------------------------------------------------
// scope.h — Visualizador de forma de onda opcional (ventana SDL2).
// -------------------------------------------------------------------------
#ifndef SCOPE_H
#define SCOPE_H

#include "limits.h"

// Crea la ventana SDL y el renderer. Devuelve 1 si pudo, 0 si no.
int scope_init(void);

// Destruye ventana/renderer y cierra SDL. No-op si no estaba abierto.
void scope_shutdown(void);

// 1 si la ventana del scope esta abierta.
int scope_is_open(void);

// Alimenta el ring buffer interno con muestras normalizadas [-1,1].
// Se llama desde audio.c con el mismo buffer que va a PortAudio.
void scope_push(const float *samples, int frames, int channels);

// Renderiza la forma de onda de todos los canales activos.
// Llamar una vez por iteracion del loop principal.
void scope_render(void);

// Procesa eventos SDL (resize, cierre de ventana). Devuelve 0 si el
// usuario cerro la ventana (el llamador debe desactivar el scope).
int scope_handle_events(void);

// Reinicia el ring buffer (se llama al reabrir el stream por cambio de
// SR/CHMAX/BUF, para no mezclar datos de la configuracion anterior).
void scope_reset(void);

// Setters de estilo por canal.
void scope_set_color(int channel, int r, int g, int b);
void scope_set_stroke(int channel, int width);

// Parsea "#rrggbb" a componentes RGB. Devuelve 1 si el formato es valido.
int scope_parse_color(const char *hex, int *r, int *g, int *b);

// -- Geometria de ventana --------------------------------------------------
// Setters persistentes: guardan el valor en state y lo aplican en caliente
// si la ventana ya esta abierta; si no, se aplican al abrirla.
void scope_set_pos_x(int x);
void scope_set_pos_y(int y);
void scope_set_size_w(int w);
void scope_set_size_h(int h);
void scope_set_fullscreen(int on);
void scope_set_decorated(int on);

// Tamanio del display donde vive (o viviria) la ventana. Inicializa el
// subsistema de video si todavia no lo esta. Devuelve 1 si pudo medir,
// 0 si no (headless); en ese caso *w y *h quedan en 0.
int scope_screen_size(int *w, int *h);

// Color de fondo y siempre-arriba
void scope_set_bg_color(int r, int g, int b);
void scope_set_always_on_top(int on);

// Getters de geometria ACTUAL de la ventana. Si la ventana no esta abierta,
// devuelven los valores deseados guardados por SWINX/SWINY/SWINW/SWINH
// (o 0 para tamanios si no fueron seteados).
int scope_get_winx(void);
int scope_get_winy(void);
int scope_get_winw(void);
int scope_get_winh(void);

// Restablece geometria y estilo de ventana a los defaults (centrada,
// 800x400, con decoracion, sin fullscreen ni always-on-top, fondo negro)
// y lo aplica en caliente si la ventana esta abierta.
void scope_reset_defaults(void);

// Copia las ultimas n muestras del canal desde el ring de audio procesado
// (la mas reciente al final); rellena con silencio si falta historia.
// Devuelve n, o 0 si dst/canal invalidos.
int scope_capture_channel(int channel, float *dst, int n);

#endif // SCOPE_H
