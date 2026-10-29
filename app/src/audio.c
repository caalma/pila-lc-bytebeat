// -------------------------------------------------------------------------
// audio.c — Stream de PortAudio en modo bloqueante y generacion de muestras.
// Incluye el pipeline de procesamiento de salida: DC blocker, filtro
// pasa-bajos opcional, y saturacion/clamp.
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <portaudio.h>
#include "scope.h"
#include "audio.h"
#include "state.h"
#include "opcodes.h"
#include "interpreter.h"
#include "wav.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static PaStream *stream = NULL;
static float    *out_buffer = NULL;

// -- Estado del DC blocker (por canal) -----------------------------------
// Filtro pasa-altos de primer orden: y[n] = x[n] - x[n-1] + R * y[n-1]
// Elimina componente continua protegiendo parlantes/auriculares.
static double dcb_prev_in[MAX_CHANNELS]  = {0};
static double dcb_prev_out[MAX_CHANNELS] = {0};
#define DCB_R 0.995   // ~6 Hz de corte a 8kHz, ~35 Hz a 44.1kHz

// -- Estado del filtro pasa-bajos (por canal) ----------------------------
static double lp_prev_out[MAX_CHANNELS] = {0};

// -- Estado del delay (por canal) ------------------------------------------
// Buffer circular que almacena la señal procesada (seca + feedback).
// El tiempo de delay se traduce a samples: delay_time * sample_rate / 1000.
#define DELAY_BUF_SIZE 65536
static double delay_buf[MAX_CHANNELS][DELAY_BUF_SIZE];
static int    delay_write_pos = 0;
// Estado de los filtros del feedback del delay (por canal)
static double dfhi_prev_in[MAX_CHANNELS]  = {0};
static double dfhi_prev_out[MAX_CHANNELS] = {0};
static double dflo_prev_out[MAX_CHANNELS] = {0};

// -- Normalizacion -------------------------------------------------------
// Normaliza de [vmin, vmax] a [-1, 1] SIN recortar. El recorte o
// saturacion se aplica despues, segun sat_enabled.
static double normalize_raw(double val) {
    double range = vmax - vmin;
    if (range == 0.0) return 0.0;
    return (val - vmin) / range * 2.0 - 1.0;
}

// -- Pipeline de procesamiento por muestra --------------------------------
static double process_sample(double val, int ch) {
    // 1. Normalizar a [-1, 1] (sin recortar todavia)
    val = normalize_raw(val);
    // 1b. Sanitizar no-finitos: un NaN/Inf que llegue desde la pila
    //     (explosiones de fe, frecuencias infinitas, asymptotas) envenenaria
    //     el estado del DC blocker, del LP y del buffer de delay para
    //     siempre: ni una recarga lo arregla, porque los filtros solo se
    //     resetean al reabrir el stream. Lo volvemos silencio ANTES de tocar
    //     cualquier filtro.
    if (!isfinite(val)) val = 0.0;

    // 2. DC blocker (si esta activo)
    if (dcb_enabled) {
        double y = val - dcb_prev_in[ch] + DCB_R * dcb_prev_out[ch];
        dcb_prev_in[ch]  = val;
        dcb_prev_out[ch] = y;
        val = y;
    }

    // 3. Filtro pasa-bajos de un polo (si lp_amount > 0)
    if (lp_amount > 0) {
        double alpha = 1.0 / (1.0 + (double)lp_amount);
        val = alpha * val + (1.0 - alpha) * lp_prev_out[ch];
        lp_prev_out[ch] = val;
    }

    // 4. Delay con feedback y filtros opcionales en el loop de realimentacion
    if (delay_enabled) {
        int delay_samples = (int)((double)delay_time * sample_rate / 1000.0);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= DELAY_BUF_SIZE) delay_samples = DELAY_BUF_SIZE - 1;

        int read_pos = (delay_write_pos - delay_samples + DELAY_BUF_SIZE) % DELAY_BUF_SIZE;
        double delayed = delay_buf[ch][read_pos];

        // Filtros en el feedback (mismo orden que el patch de Pd:
        // high-pass primero, luego low-pass). Solo se aplican si la
        // frecuencia es > 0; con 0 el filtro esta desactivado.
        if (delay_feed_hi > 0) {
            double R = exp(-2.0 * M_PI * (double)delay_feed_hi / (double)sample_rate);
            double y = delayed - dfhi_prev_in[ch] + R * dfhi_prev_out[ch];
            dfhi_prev_in[ch]  = delayed;
            dfhi_prev_out[ch] = y;
            delayed = y;
        }
        if (delay_feed_lo > 0) {
            double alpha = 1.0 - exp(-2.0 * M_PI * (double)delay_feed_lo / (double)sample_rate);
            double y = alpha * delayed + (1.0 - alpha) * dflo_prev_out[ch];
            dflo_prev_out[ch] = y;
            delayed = y;
        }

        // Escribir la muestra actual + feedback de la senal retrasada filtrada
        double feed = (double)delay_feed / 100.0;
        delay_buf[ch][delay_write_pos] = val + delayed * feed;

        // Mezclar seca y humeda
        double mix = (double)delay_mix / 100.0;
        val = val * (1.0 - mix) + delayed * mix;

        // Avanzar la posicion de escritura (una vez por frame, en canal 0)
        if (ch == 0) {
            delay_write_pos = (delay_write_pos + 1) % DELAY_BUF_SIZE;
        }
    }

    // 5. Saturación o clamp duro
    if (sat_enabled) {
        val = tanh(val);
    } else {
        if (val >  1.0) val =  1.0;
        if (val < -1.0) val = -1.0;
    }

    return val;
}

// Resetear estado de filtros (se llama al reabrir el stream)
static void reset_filters(void) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        dcb_prev_in[i]  = 0;
        dcb_prev_out[i] = 0;
        lp_prev_out[i]  = 0;
    }
}

static void delay_reset(void) {
    memset(delay_buf, 0, sizeof(delay_buf));
    delay_write_pos = 0;
    memset(dfhi_prev_in,  0, sizeof(dfhi_prev_in));
    memset(dfhi_prev_out, 0, sizeof(dfhi_prev_out));
    memset(dflo_prev_out, 0, sizeof(dflo_prev_out));
}

void audio_reset_processing(void) {
    reset_filters();
    delay_reset();
}

// -- Gestion del stream --------------------------------------------------
static int open_stream(void) {
    PaError err = Pa_OpenDefaultStream(
        &stream, 0, num_channels, paFloat32,
        sample_rate, buf_size, NULL, NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio | Error abriendo stream: %s\n", Pa_GetErrorText(err));
        stream = NULL;
        return 0;
    }
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio | Error iniciando stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        stream = NULL;
        return 0;
    }
    return 1;
}

static void close_stream(void) {
    if (!stream) return;
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    stream = NULL;
}

static void reallocate_buffer(void) {
    free(out_buffer);
    out_buffer = malloc((size_t)buf_size * num_channels * sizeof(float));
    if (!out_buffer) {
        fprintf(stderr, "    Audio | No se pudo reservar buffer de salida: %d frames x %d ch\n",
                buf_size, num_channels);
    }
}

int audio_init(void) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio | Error inicializando: %s\n", Pa_GetErrorText(err));
        return 0;
    }
    reallocate_buffer();
    if (!open_stream()) {
        Pa_Terminate();
        return 0;
    }
    fprintf(stderr, "    Audio | Stream abierto: Hz:%d ch: %d  buffer/frames:%d mute:%d\n",
            sample_rate, num_channels, buf_size, mute_enabled);
    return 1;
}

void audio_shutdown(void) {
    close_stream();
    free(out_buffer);
    out_buffer = NULL;
    Pa_Terminate();
}

void audio_restart_if_needed(void) {
    if (!stream_dirty) return;
    if (pending_sample_rate  > 0) sample_rate  = pending_sample_rate;
    if (pending_num_channels > 0) num_channels = pending_num_channels;
    if (pending_buf_size     > 0) buf_size     = pending_buf_size;
    pending_sample_rate  = -1;
    pending_num_channels = -1;
    pending_buf_size     = -1;
    stream_dirty = 0;
    if (num_channels < 1)            num_channels = 1;
    if (num_channels > MAX_CHANNELS) num_channels = MAX_CHANNELS;
    if (buf_size < 16)   buf_size = 16;
    if (buf_size > 4096) buf_size = 4096;
    close_stream();
    reset_filters();
    scope_reset();
    delay_reset();
    reallocate_buffer();
    open_stream();
    fprintf(stderr, "    Audio | Stream reabierto: Hz:%d ch:%d buffer/frames:%d mute:%d\n",
            sample_rate, num_channels, buf_size, mute_enabled);

}

void audio_render_and_write(void) {
    if (!stream || !out_buffer) return;

    // {b}: una vez por buffer, antes de generar las muestras
    run_program(CTX_BUF, &prog_buf, &running);

    double sil = (vmin + vmax) * 0.5;
    for (int f = 0; f < buf_size; f++) {
        for (int ch = 0; ch < MAX_CHANNELS; ch++) sample_out[ch] = sil;
        run_program(CTX_SAMPLE, &prog_sample, &running);
        for (int ch = 0; ch < num_channels; ch++) {
            out_buffer[f * num_channels + ch] = (float)process_sample(sample_out[ch], ch);
        }
        sample_index++;
    }

    // {b.}: una vez por buffer, despues de generar las muestras
    run_program(CTX_BUF_POST, &prog_buf_post, &running);

    if (recorder_active()) {
        recorder_write(out_buffer, buf_size, num_channels);
    }

    // Alimentar el scope si esta activo (mismas muestras que van a PortAudio)
    if (scope_enabled) {
        scope_push(out_buffer, buf_size, num_channels);
    }

    // Escribir al stream de audio (respetando mute)
    if (mute_enabled) {
        // Mute activo: enviar silencio en lugar de las muestras procesadas
        static float *silence_buffer = NULL;
        static int silence_buffer_size = 0;
        int needed = buf_size * num_channels;
        if (needed > silence_buffer_size) {
            free(silence_buffer);
            silence_buffer = malloc((size_t)needed * sizeof(float));
            silence_buffer_size = needed;
        }
        if (silence_buffer) {
            memset(silence_buffer, 0, (size_t)needed * sizeof(float));
            Pa_WriteStream(stream, silence_buffer, buf_size);
        }
    } else {
        Pa_WriteStream(stream, out_buffer, buf_size);
    }
}
