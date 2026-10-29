// -------------------------------------------------------------------------
// diag.c
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "diag.h"

static char msgs[MAX_DIAG_MSGS][DIAG_MSG_LEN];
static int  n_msgs = 0;
static int  n_overflow = 0; // cuantos mensajes se perdieron por falta de espacio

void diag_reset(void) {
    n_msgs = 0;
    n_overflow = 0;
}

void diag_add(const char *fmt, ...) {
    if (n_msgs >= MAX_DIAG_MSGS) { n_overflow++; return; }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgs[n_msgs], DIAG_MSG_LEN, fmt, ap);
    va_end(ap);
    // Los llamadores suelen incluir un '\n' final (pensado para stderr via
    // DIAGF). En el buffer de diag se recorta cualquier caracter de control
    // al final (\n, \r, \t...) para que cada mensaje quede limpio si se lo
    // reimprime en un resumen.
    char *s = msgs[n_msgs];
    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len - 1] < 0x20) {
        s[--len] = '\0';
    }
    n_msgs++;
}

int diag_count(void) {
    return n_msgs;
}

int diag_overflow_count(void) {
    return n_overflow;
}

const char *diag_get(int i) {
    if (i < 0 || i >= n_msgs) return "";
    return msgs[i];
}
