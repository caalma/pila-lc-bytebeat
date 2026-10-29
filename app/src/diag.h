// -------------------------------------------------------------------------
// diag.h — Diagnosticos de carga (errores de parseo/compilacion).
// -------------------------------------------------------------------------
#ifndef DIAG_H
#define DIAG_H

#include "limits.h"

void diag_reset(void);
void diag_add(const char *fmt, ...);
int  diag_count(void);
int  diag_overflow_count(void); // cuantos mensajes se perdieron por limite
const char *diag_get(int i);

#endif // DIAG_H
