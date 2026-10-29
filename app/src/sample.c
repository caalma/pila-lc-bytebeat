// -------------------------------------------------------------------------
// sample.c — Carga y lectura de samples de audio desde archivos raw.
// Internamente todo se guarda como float normalizado a [-1, 1]. La lectura
// envuelve la posicion por modulo y escala al rango [VMIN, VMAX] actual.
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include "sample.h"
#include "state.h"
#include "scope.h"

typedef struct {
    float *data;
    int    length;
} SampleSlot;

static SampleSlot slots[MAX_SMP_SLOTS];

// Cuantos slots tienen sample cargado ahora (para `showlimits`).
int sample_used_slots(void) {
    int n = 0;
    for (int i = 0; i < MAX_SMP_SLOTS; i++) if (slots[i].data) n++;
    return n;
}

// -- Deteccion de extension (sin distinguir mayusculas) ------------------
static int has_extension(const char *path, const char *ext) {
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return 0;
    const char *p = path + plen - elen;
    for (size_t i = 0; i < elen; i++)
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)ext[i])) return 0;
    return 1;
}

int sample_load(const char *path, int slot) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) {
        fprintf(stderr, "  smpload | Slot inválido %d (rango 0-%d)\n",
                slot, MAX_SMP_SLOTS - 1);
        return 0;
    }

    int is_i16 = has_extension(path, ".i16") || has_extension(path, ".rawi16");
    int is_f32 = has_extension(path, ".f32") || has_extension(path, ".rawf32");
    int forced = 0;
    if (!is_i16 && !is_f32) {
        // Extension no reconocida: se fuerza el formato segun el VINT
        // activo en este momento. Es el efecto de "rotura de formato":
        // leer un float32 como int16 (o viceversa) sin que el sistema
        // se niegue ni crashee.
        forced = 1;
        if (vint_mode) is_i16 = 1; else is_f32 = 1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "  smpload | No se pudo abrir '%s'\n", path);
        return 0;
    }
    if (forced) {
        char magic[4];
        size_t rd = fread(magic, 1, 4, f);
        fseek(f, 0, SEEK_SET);
        fprintf(stderr,
                "  smpload | '%s' sin extension reconocida; se fuerza %s por VINT actual\n",
                path, is_i16 ? "int16" : "float32");
        if (rd == 4 && memcmp(magic, "RIFF", 4) == 0)
            fprintf(stderr,
                    "  smpload | Aviso: '%s' parece un WAV (header RIFF); "
                    "se leera como datos crudos, header incluido\n", path);
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    int bytes_per_sample = is_i16 ? 2 : 4;
    int num_samples = (int)(file_size / bytes_per_sample);
    if (num_samples <= 0) {
        fprintf(stderr, "  smpload | Archivo vacio o demasiado chico '%s'\n", path);
        fclose(f);
        return 0;
    }

    // Leer el archivo completo a un buffer temporal
    size_t raw_size = (size_t)num_samples * bytes_per_sample;
    uint8_t *raw = malloc(raw_size);
    if (!raw) {
        fprintf(stderr, "  smpload | Sin memoria para leer '%s'\n", path);
        fclose(f);
        return 0;
    }
    size_t read_bytes = fread(raw, 1, raw_size, f);
    fclose(f);

    int actual_samples = (int)(read_bytes / bytes_per_sample);
    if (actual_samples <= 0) {
        free(raw);
        fprintf(stderr, "  smpload | No se pudieron leer muestras de '%s'\n", path);
        return 0;
    }

    // Sobreescribir: liberar el slot anterior si estaba ocupado
    if (slots[slot].data) {
        free(slots[slot].data);
        slots[slot].data = NULL;
        slots[slot].length = 0;
    }

    slots[slot].data = malloc((size_t)actual_samples * sizeof(float));
    if (!slots[slot].data) {
        free(raw);
        fprintf(stderr, "  smpload | Sin memoria para almacenar '%s'\n", path);
        return 0;
    }

    // Convertir a float [-1, 1] respetando little-endian explicitamente
    if (is_i16) {
        for (int i = 0; i < actual_samples; i++) {
            uint16_t u = (uint16_t)raw[i*2] | ((uint16_t)raw[i*2+1] << 8);
            int16_t  s = (int16_t)u;
            slots[slot].data[i] = (float)s / 32768.0f;
        }
    } else {
        for (int i = 0; i < actual_samples; i++) {
            uint32_t u = (uint32_t)raw[i*4]
                       | ((uint32_t)raw[i*4+1] << 8)
                       | ((uint32_t)raw[i*4+2] << 16)
                       | ((uint32_t)raw[i*4+3] << 24);
            float fval;
            memcpy(&fval, &u, 4);
            slots[slot].data[i] = fval;
        }
    }

    slots[slot].length = actual_samples;
    free(raw);
    return 1;
}


double sample_get(int slot, double position) {
    double mid = (vmin + vmax) * 0.5;   // silencio
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return mid;
    if (!slots[slot].data || slots[slot].length <= 0) return mid;

    // Posicion no finita (NaN/Inf via frecuencias o cuentas explotadas):
    // sin este guard, fmod(NaN, len) y el (int)pos siguiente producen
    // INT_MIN y la lectura se va fuera del buffer (heap-buffer-overflow).
    if (!isfinite(position)) return mid;
    int len = slots[slot].length;

    // Wrapping por modulo: soporta posiciones negativas y mayores al largo
    double pos = fmod(position, (double)len);
    if (pos < 0.0) pos += (double)len;

    // Interpolacion lineal: usar frac para interpolar entre idx0 y idx1
    int idx0 = (int)pos;
    double frac = pos - (double)idx0;
    int idx1 = (idx0 + 1) % len;

    double norm0 = (double)slots[slot].data[idx0];   // [-1, 1]
    double norm1 = (double)slots[slot].data[idx1];

    // Crossfade en el punto de loop: en los ultimos smp_crossfade_len
    // samples del buffer, mezclar progresivamente con la cabeza del buffer.
    if (smp_crossfade_len > 0 && smp_crossfade_len < len &&
        idx0 >= len - smp_crossfade_len) {
        int fade_pos = idx0 - (len - smp_crossfade_len);   // 0 .. crossfade-1
        int head_idx = (fade_pos + 1) % smp_crossfade_len;
        double t = (double)(fade_pos + 1) / (double)smp_crossfade_len;
        double norm_head = (double)slots[slot].data[head_idx];
        norm0 = norm0 * (1.0 - t) + norm_head * t;
    }

    // Aplicar crossfade a idx1 si tambien cae en la zona de crossfade
    if (smp_crossfade_len > 0 && smp_crossfade_len < len &&
        idx1 >= len - smp_crossfade_len) {
        int fade_pos = idx1 - (len - smp_crossfade_len);
        int head_idx = (fade_pos + 1) % smp_crossfade_len;
        double t = (double)(fade_pos + 1) / (double)smp_crossfade_len;
        double norm_head = (double)slots[slot].data[head_idx];
        norm1 = norm1 * (1.0 - t) + norm_head * t;
    }

    // Interpolacion lineal entre idx0 y idx1
    double norm = norm0 + (norm1 - norm0) * frac;

    // Escalar de [-1, 1] a [VMIN, VMAX]
    return vmin + (norm + 1.0) * 0.5 * (vmax - vmin);
}


int sample_length(int slot) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return 0;
    return slots[slot].length;
}

void sample_remove(int slot) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return;
    if (slots[slot].data) {
        free(slots[slot].data);
        slots[slot].data = NULL;
        slots[slot].length = 0;
    }
}

void sample_reset(void) {
    for (int i = 0; i < MAX_SMP_SLOTS; i++) sample_remove(i);
}

int sample_loop(int slot) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return 0;
    if (!slots[slot].data || slots[slot].length <= 0) return 0;

    int len = slots[slot].length;
    int search_start = len - (len / 10);  // buscar en el ultimo 10%
    if (search_start < 0) search_start = 0;

    // Buscar zero-crossing desde el final hacia atras
    for (int i = len - 1; i >= search_start; i--) {
        float val = slots[slot].data[i];
        // Zero-crossing: cambio de signo entre muestras adyacentes
        if (i > 0) {
            float prev = slots[slot].data[i - 1];
            if ((prev < 0.0f && val >= 0.0f) || (prev >= 0.0f && val < 0.0f)) {
                slots[slot].length = i;
                return i;
            }
        }
        // Tambien aceptar muestras muy cercanas a cero
        if (fabsf(val) < 0.001f) {
            slots[slot].length = i + 1;
            return i + 1;
        }
    }

    // No encontro zero-crossing razonable, dejar intacto
    return len;
}

int sample_reverse(int slot) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return 0;
    if (!slots[slot].data || slots[slot].length <= 0) return 0;

    int len = slots[slot].length;
    for (int i = 0; i < len / 2; i++) {
        int j = len - 1 - i;
        float tmp = slots[slot].data[i];
        slots[slot].data[i] = slots[slot].data[j];
        slots[slot].data[j] = tmp;
    }
    return 1;
}

int sample_gain(int slot, float gain) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return 0;
    if (!slots[slot].data || slots[slot].length <= 0) return 0;

    int len = slots[slot].length;
    for (int i = 0; i < len; i++) {
        slots[slot].data[i] *= gain;
    }
    return 1;
}

int sample_mix(int dst_slot, int src_slot) {
    if (dst_slot < 0 || dst_slot >= MAX_SMP_SLOTS) return 0;
    if (src_slot < 0 || src_slot >= MAX_SMP_SLOTS) return 0;
    if (!slots[dst_slot].data || slots[dst_slot].length <= 0) return 0;
    if (!slots[src_slot].data || slots[src_slot].length <= 0) return 0;

    int dst_len = slots[dst_slot].length;
    int src_len = slots[src_slot].length;

    // Mezclar hasta el minimo de los dos largos
    int mix_len = (dst_len < src_len) ? dst_len : src_len;

    for (int i = 0; i < mix_len; i++) {
        float sum = slots[dst_slot].data[i] + slots[src_slot].data[i];
        // Clip a [-1, 1]
        if (sum > 1.0f) sum = 1.0f;
        if (sum < -1.0f) sum = -1.0f;
        slots[dst_slot].data[i] = sum;
    }
    return 1;
}

// -- Captura viva (smplive) -------------------------------------------------
// Arma un sample con las ultimas n muestras del audio procesado (canal
// channel, dominio normalizado [-1,1], el mismo de los slots) y lo publica
// de forma atomica: el buffer se llena FUERA del slot y se publica en un
// solo paso, asi cualquier lector (sample, samplepitch, smplen) ve el
// sample anterior completo o el nuevo completo, nunca un estado intermedio.
int sample_live(int slot, int channel, int n) {
    if (slot < 0 || slot >= MAX_SMP_SLOTS) return 0;
    if (channel < 0 || channel >= num_channels) return 0;
    if (n < 1) return 0;
    if (n > SCOPE_RING_SIZE) n = SCOPE_RING_SIZE;
    float *tmp = (float *)malloc(sizeof(float) * (size_t)n);
    if (!tmp) return 0;
    scope_capture_channel(channel, tmp, n);
    // Publicacion atomica: unico punto donde el slot cambia de mano.
    free(slots[slot].data);
    slots[slot].data   = tmp;
    slots[slot].length = n;
    // Si tu struct de slot tiene campos extra (loop_len y similares),
    // resetealos aca para que el sample nuevo arranque limpio.
    return 1;
}
