// -------------------------------------------------------------------------
// strtab.h — Tabla de strings literales ("texto"), con interning.
// -------------------------------------------------------------------------
#ifndef STRTAB_H
#define STRTAB_H

#include "limits.h"

int strtab_count(void);

// Devuelve el indice de `s` en la tabla — reusa uno existente si el
// contenido ya estaba interned, si no crea una entrada nueva. Devuelve -1
// si la tabla esta llena y `s` es contenido nuevo (se avisa por stderr).
int strtab_intern(const char *s);

// Texto de la entrada `index`, o "" si el indice es invalido.
const char *strtab_get(int index);

// 1 si `index` corresponde a una entrada real (distingue de un string
// vacio interned a proposito, que tambien devuelve "" en strtab_get).
int strtab_is_valid(int index);

#endif // STRTAB_H
