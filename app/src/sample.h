// -------------------------------------------------------------------------
// sample.h — Carga y lectura de samples de audio desde archivos raw.
// -------------------------------------------------------------------------
#ifndef SAMPLE_H
#define SAMPLE_H

#include "limits.h"

int sample_used_slots(void);

// Carga un archivo raw en `slot`. Formato por extension: .i16/.rawi16 (int16
// little-endian) o .f32/.rawf32 (float32 little-endian). Si el slot ya estaba
// ocupado lo sobreescribe (libera el sample anterior). Devuelve 1 si pudo.
int sample_load(const char *path, int slot);

// Devuelve el valor del sample en `slot` en la posicion `position`,
// envuelto por modulo al largo del sample (loop natural). El valor se
// escala a [VMIN, VMAX]. Si el slot esta vacio o es invalido devuelve el
// punto medio del rango (silencio).
double sample_get(int slot, double position);

// Largo del sample en `slot`, en muestras. 0 si esta vacio o es invalido.
int sample_length(int slot);

// Libera la memoria del sample en `slot`. No-op si ya esta vacio.
void sample_remove(int slot);

// Libera la memoria de TODOS los slots.
void sample_reset(void);

// Ajusta el largo del sample al zero-crossing mas cercano desde el final.
// Si no encuentra uno razonable (dentro de los ultimos 10% del sample),
// deja el largo intacto. Devuelve el nuevo largo, o 0 si el slot es invalido.
int sample_loop(int slot);

// Invierte el sample en el lugar. Devuelve 1 si tuvo exito, 0 si el slot
// es invalido.
int sample_reverse(int slot);

// Multiplica todas las muestras del sample por `gain`. Devuelve 1 si tuvo
// exito, 0 si el slot es invalido.
int sample_gain(int slot, float gain);

// Mezcla `src_slot` sobre `dst_slot` (suma con clip a [-1, 1]).
// Devuelve 1 si tuvo exito, 0 si alguno de los slots es invalido o tienen
// distinto largo.
int sample_mix(int dst_slot, int src_slot);

// Captura n muestras del audio procesado del canal y las publica en el
// slot de forma atomica. Devuelve 1 si ok, 0 si parametros invalidos o
// sin memoria (el llamador lo convierte en anomalia).
int sample_live(int slot, int channel, int n);

#endif // SAMPLE_H
