
// -------------------------------------------------------------------------
// limits.h — Límites del lenguaje y del sistema.
//
// Todas las constantes que definen los límites del lenguaje viven acá:
// el usuario ve de un vistazo todos los límites del sistema, y es fácil
// ajustar valores sin cazar definiciones en múltiples archivos.
//
// Las constantes internas de implementación (MAX_TOKEN_LEN, MAX_RAW_TOKENS,
// MAX_FUNC_BODY_TOKENS, MAX_EXPANDED_TOKENS) se quedan en parser.c porque
// son detalles de implementación, no de la API del lenguaje.
// -------------------------------------------------------------------------
#ifndef LIMITS_H
#define LIMITS_H

// -- Diccionario de palabras del usuario -----------------------------------
#define MAX_WORDS 2048        // funciones + alias + variables (espacio único)

// -- Programas compilados --------------------------------------------------
#define MAX_INST 2048         // instrucciones por bloque de contexto
#define STACK_SIZE 128        // profundidad de la pila de datos
#define MAX_LOOP_DEPTH 16     // anidamiento de @[ ]@
#define MAX_ANCHORS 64        // anclas #nombre y jumps ->nombre
#define RPN_BUF_SIZE 65536    // tamaño del buffer de fuente .p (16 KB)
#define MAX_CALL_DEPTH 16     // profundidad maxima de llamadas '= (OP_CALL)

// -- Audio -----------------------------------------------------------------
#define MAX_CHANNELS 16       // tope de canales
#define DEFAULT_SAMPLE_RATE 8000
#define DEFAULT_CHANNELS 1
#define DEFAULT_BUF_SIZE 256
#define DEFAULT_VMIN 0.0
#define DEFAULT_VMAX 255.0

// -- Scope -----------------------------------------------------------------
#define SCOPE_RING_SIZE 65536 // muestras en el ring buffer

// -- Samples ---------------------------------------------------------------
#define MAX_SMP_SLOTS 16      // slots de samples

// -- Strings ---------------------------------------------------------------
#define MAX_STRINGS 256       // strings literales interned
#define MAX_STRING_LEN 256    // largo máximo de cada string

// -- Diagnósticos ----------------------------------------------------------
#define MAX_DIAG_MSGS 12
#define DIAG_MSG_LEN 120

#endif // LIMITS_H
