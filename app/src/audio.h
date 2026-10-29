// -------------------------------------------------------------------------
// audio.h — Stream de salida de PortAudio (modo bloqueante) y generacion de
// muestras a partir del programa RPN.
// -------------------------------------------------------------------------
#ifndef AUDIO_H
#define AUDIO_H

// Inicializa PortAudio y abre el stream de salida con los parametros
// actuales (sample_rate, num_channels, buf_size). Tambien reserva el buffer
// de salida. Devuelve 1 si pudo, 0 si no (ya reporto el error por stderr).
int audio_init(void);

// Detiene y cierra el stream, libera el buffer y termina PortAudio. Seguro
// llamarlo aunque audio_init haya fallado a medias.
void audio_shutdown(void);

// Si hay cambios pendientes de SR/CHMAX/BUF (stream_dirty seteado por los
// setters del interprete), los aplica a las variables globales y reabre el
// stream con los nuevos valores. Produce un corte breve del audio, que es
// inevitable al cambiar sample rate, cantidad de canales o tamano de
// buffer. No-op si no hay cambios pendientes. Se llama entre buffer y
// buffer desde el loop principal, nunca desde dentro de un Pa_WriteStream.
void audio_restart_if_needed(void);

// Genera un buffer completo de buf_size frames y lo reproduce:
//   1. corre {b} una vez (antes de las muestras),
//   2. para cada frame limpia sample_out al silencio, corre {s} (que puede
//      escribir canales via OUT), normaliza cada canal a [-1,1] y lo guarda
//      en el buffer de salida,
//   3. corre {b.} una vez (despues de las muestras),
//   4. si hay grabacion activa, vuelca el buffer al recorder,
//   5. escribe el buffer en el stream de PortAudio (Pa_WriteStream).
// Es el trabajo principal de cada iteracion del loop. No-op si el stream no
// esta abierto.
void audio_render_and_write(void);

// Resetea el estado de procesamiento (filtros DCB/LP y buffer de delay).
// Lo usa la palabra `default` para que no queden colas de la config vieja.
void audio_reset_processing(void);

#endif // AUDIO_H
