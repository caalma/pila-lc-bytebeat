// -------------------------------------------------------------------------
// wav.h — Grabacion de la salida de audio a un archivo WAV o RAW.
//
// Lo usan las palabras fileout/record/stoprecord (interpreter.c) para
// controlar el ciclo de vida, y el loop principal (main.c) para volcar las
// muestras ya normalizadas a [-1,1] via recorder_write().
// -------------------------------------------------------------------------
#ifndef WAV_H
#define WAV_H

// Define el path del archivo de grabacion. El formato (WAV o RAW) se
// infiere de la extensión .wav → WAV con header; cualquier otra extensión
// → RAW con sufijo automático de formato. No abre ningun archivo todavia;
// solo recuerda el path para cuando se llame a recorder_start().
// Devuelve 1 si el path es valido (no vacio), 0 si no.
int recorder_set_path(const char *path);

// Abre el archivo (usando el path de recorder_set_path), captura los
// parametros actuales de audio (sample_rate, num_channels, vint_mode) y
// escribe el encabezado si es WAV. A partir de aca recorder_write() vuelca
// las muestras. Si ya estaba grabando, detiene la grabacion previa y
// arranca una nueva. Devuelve 1 si pudo abrir el archivo, 0 si no (no habia
// path seteado, no se pudo abrir, etc.).
int recorder_start(void);

// Detiene la grabacion: si es WAV parchea el encabezado con los tamanios
// reales y cierra el archivo. No-op seguro si no estaba grabando.
void recorder_stop(void);

// 1 si hay una grabacion en curso, 0 si no.
int recorder_active(void);

// Vuelca `frames` frames de a `channels` canales por frame (interleaved,
// o sea `frames * channels` floats normalizados a [-1,1]) al archivo.
// Convierte cada muestra al formato capturado en recorder_start(): int16 si
// vint_mode estaba activo, float32 si no. No-op si no esta grabando.
void recorder_write(const float *samples, int frames, int channels);

#endif // WAV_H
