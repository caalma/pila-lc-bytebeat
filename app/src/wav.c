// -------------------------------------------------------------------------
// wav.c — Grabacion de la salida de audio a WAV o RAW.
//
// Flujo tipico (las palabras que lo disparan estan en interpreter.c):
//   {a  "salida.wav" recfile  recstart }   ( arranca al cargar )
//   {a. recstop }                          ( detiene al recargar/salir )
//
// recorder_start() congela los parametros de audio del momento (sample
// rate, canales, y si la aritmetica es entera o float via vint_mode) porque
// el encabezado WAV fija el formato al inicio. Si el usuario cambia
// SR/CHMAX/VINT mientras graba, el archivo queda inconsistente — es
// responsabilidad del usuario no hacerlo.
//
// La precision del inicio/fin de grabacion es de un buffer de audio:
// recorder_write() se llama una vez por buffer desde el loop principal, asi
// que un record/stoprecord disparado a mitad de buffer incluye o descarta
// hasta un buffer completo de muestras. Para livecoding es suficiente.
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "wav.h"
#include "state.h"

#define REC_PATH_LEN 512

static FILE    *rec_file = NULL;
static int      rec_active = 0;
static int      rec_is_wav = 1;
static int      rec_use_float = 0;   // 0 = int16, 1 = float32
static int      rec_sample_rate = 0;
static int      rec_num_channels = 0;
static uint32_t rec_data_bytes = 0;  // bytes de audio escritos (para el header)
static char     rec_path[REC_PATH_LEN];

// -- Escritura little-endian (el formato WAV es little-endian) -----------
static void write_le16(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}
static void write_le32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                     (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
    fwrite(b, 1, 4, f);
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

// -- Construccion de path raw con sufijo de formato ----------------------
// Si el path no termina en ".wav", la grabacion es raw y el nombre final
// lleva sufijo explicito de formato: ".rawi16" (int16) o ".rawf32"
// (float32), segun el vint_mode congelado en recorder_start(). Si el path
// ya terminaba en ".raw", ".rawi16" o ".rawf32", ese sufijo se reemplaza
// por el del modo actual: la extension nunca miente sobre el contenido.
static void build_raw_path(const char *base, char *out, size_t out_max, int use_float) {
    strncpy(out, base, out_max - 1);
    out[out_max - 1] = '\0';
    size_t len = strlen(out);
    const char *known[] = { ".rawi16", ".rawf32", ".raw" };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        size_t sl = strlen(known[i]);
        if (len >= sl && has_extension(out, known[i])) {
            out[len - sl] = '\0';
            len -= sl;
            break;
        }
    }
    const char *suf = use_float ? ".rawf32" : ".rawi16";
    size_t sl = strlen(suf);
    if (len + sl < out_max) memcpy(out + len, suf, sl + 1);
}

// -- Encabezado WAV ------------------------------------------------------
// 44 bytes con tamanios provisorios en 0; finalize_wav_header() los parchea
// al cerrar, cuando ya se sabe cuantos bytes de audio se escribieron.
static void write_wav_header(FILE *f, int sr, int ch, int use_float) {
    int bits        = use_float ? 32 : 16;
    int audio_fmt   = use_float ? 3 : 1;   // 3 = IEEE float, 1 = PCM
    int block_align = ch * (bits / 8);
    int byte_rate   = sr * block_align;
    fwrite("RIFF", 1, 4, f);
    write_le32(f, 0);                       // RIFF size (parche al cerrar)
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    write_le32(f, 16);                      // tamanio del chunk fmt
    write_le16(f, (uint16_t)audio_fmt);
    write_le16(f, (uint16_t)ch);
    write_le32(f, (uint32_t)sr);
    write_le32(f, (uint32_t)byte_rate);
    write_le16(f, (uint16_t)block_align);
    write_le16(f, (uint16_t)bits);
    fwrite("data", 1, 4, f);
    write_le32(f, 0);                       // data size (parche al cerrar)
}

static void finalize_wav_header(FILE *f, uint32_t data_bytes) {
    fseek(f, 4, SEEK_SET);
    write_le32(f, 36 + data_bytes);         // RIFF size
    fseek(f, 40, SEEK_SET);
    write_le32(f, data_bytes);              // data size
}

// -- Conversion de muestra normalizada [-1,1] a int16 --------------------
static int16_t float_to_int16(float v) {
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * 32767.0f);
}

// -- API publica ---------------------------------------------------------
int recorder_set_path(const char *path) {
    if (!path || !path[0]) return 0;
    strncpy(rec_path, path, REC_PATH_LEN - 1);
    rec_path[REC_PATH_LEN - 1] = '\0';
    return 1;
}

int recorder_start(void) {
    // Captura los parametros actuales: el encabezado WAV fija el formato al
    // inicio, asi que quedan congelados aunque SR/CH/VINT cambien despues.
    rec_use_float    = (vint_mode == 0) ? 1 : 0;
    rec_is_wav       = has_extension(rec_path, ".wav");
    rec_sample_rate  = sample_rate;
    rec_num_channels = num_channels;
    rec_data_bytes   = 0;

    char final_path[REC_PATH_LEN];
    if (rec_is_wav) {
        strncpy(final_path, rec_path, REC_PATH_LEN - 1);
        final_path[REC_PATH_LEN - 1] = '\0';
    } else {
        build_raw_path(rec_path, final_path, sizeof(final_path), rec_use_float);
    }

    rec_file = fopen(final_path, "wb");
    if (!rec_file) {
        fprintf(stderr, " recstart | No se pudo abrir '%s' para escritura\n", final_path);
        return 0;
    }
    if (rec_is_wav)
        write_wav_header(rec_file, rec_sample_rate, rec_num_channels, rec_use_float);
    rec_active = 1;
    fprintf(stderr, " recstart | Grabando en '%s' (%s, %s, %d Hz, %d ch)\n",
            final_path, rec_is_wav ? "WAV" : "RAW",
            rec_use_float ? "float32" : "int16",
            rec_sample_rate, rec_num_channels);
    return 1;
}

void recorder_stop(void) {
    if (!rec_active || !rec_file) { rec_active = 0; rec_file = NULL; return; }
    if (rec_is_wav) finalize_wav_header(rec_file, rec_data_bytes);
    fclose(rec_file);
    rec_file = NULL;
    rec_active = 0;
    fprintf(stderr, "  recstop | Grabación finalizada (%u bytes de datos)\n",
            (unsigned)rec_data_bytes);
}

int recorder_active(void) {
    return rec_active;
}

void recorder_write(const float *samples, int frames, int channels) {
    if (!rec_active || !rec_file || !samples) return;
    if (frames <= 0 || channels <= 0) return;
    int total = frames * channels;
    size_t bytes;
    uint8_t *buf;
    if (rec_use_float) {
        // float32 little-endian
        bytes = (size_t)total * 4;
        buf = malloc(bytes);
        if (!buf) return;
        for (int i = 0; i < total; i++) {
            uint32_t u;
            memcpy(&u, &samples[i], 4);
            buf[i*4 + 0] = (uint8_t)(u & 0xFF);
            buf[i*4 + 1] = (uint8_t)((u >> 8) & 0xFF);
            buf[i*4 + 2] = (uint8_t)((u >> 16) & 0xFF);
            buf[i*4 + 3] = (uint8_t)((u >> 24) & 0xFF);
        }
    } else {
        // int16 little-endian
        bytes = (size_t)total * 2;
        buf = malloc(bytes);
        if (!buf) return;
        for (int i = 0; i < total; i++) {
            uint16_t v = (uint16_t)float_to_int16(samples[i]);
            buf[i*2 + 0] = (uint8_t)(v & 0xFF);
            buf[i*2 + 1] = (uint8_t)((v >> 8) & 0xFF);
        }
    }
    fwrite(buf, 1, bytes, rec_file);
    free(buf);
    rec_data_bytes += (uint32_t)bytes;
}
