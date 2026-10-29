// -------------------------------------------------------------------------
// interpreter.h — Maquina de pila que ejecuta un Program (app/app.post/
// buf/buf.post/sample) sobre una pila de doubles, con aritmetica conmutable
// entre modo entero (bytebeat, wraparound int32) y modo float (floatbeat).
// -------------------------------------------------------------------------
#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "opcodes.h"

// A que bloque de contexto pertenece el Program que se esta ejecutando.
// Solo se usa para saber que cache de mapas de saltos usar/reconstruir
// (cada Program tiene sus propios @[ ]@ / ?[ ?! ]? independientes). No
// restringe que opcodes pueden aparecer en cada uno: cualquier palabra
// funciona en cualquier contexto.
//
// APP_POST y BUF_POST son los "espejo" de cierre de APP/BUF: APP_POST
// corre el {a.} del programa SALIENTE justo antes de cargar uno nuevo (o
// al cerrar la app); BUF_POST corre el {b.} del buffer actual justo
// despues de terminar de generar sus muestras (ver main.c).
typedef enum { CTX_APP, CTX_APP_POST, CTX_BUF, CTX_BUF_POST, CTX_SAMPLE } ExecContext;

// Ejecuta `prog` completo una vez.
//
// ctx     → que cache de mapas de saltos usar (ver arriba).
// running → si no es NULL, `bye` lo pone en 0 para cerrar la app.
//
// El contador de tiempo (sample_index) y los parametros de audio se leen
// directamente del estado global (state.h); no se pasan por parametro.
void run_program(ExecContext ctx, Program *prog, int *running);

// Debe llamarse cada vez que los Programas cambian (despues de load_rpn),
// para invalidar los mapas de saltos cacheados de los cinco contextos.
void interpreter_on_program_loaded(void);

// -- Anomalias de runtime ------------------------------------------------
// Cuentan operaciones que no se pudieron completar (pila insuficiente,
// division por cero, indice de loop pedido fuera de un loop, canal de OUT
// fuera de rango, etc.). No son errores fatales: la palabra simplemente
// queda como no-op esa vez, pero acumularlas permite reportarlas en
// conjunto sin loggear cada ocurrencia (que podria ser una vez por muestra).
void interpreter_reset_anomalies(void);
int  interpreter_get_anomaly_count(void);

// -- Log detallado de anomalias (palabra `log`) ---------------------------
// Abre `path` (truncandolo) y registra una linea por anomalia hasta que
// sample_index supere el inicio + `seconds` segundos de audio. El loop
// principal debe llamar interpreter_log_tick() una vez por iteracion para
// el cierre automatico y el flush. Devuelve 1 si pudo abrir.
int  interpreter_log_start(const char *path, double seconds);
// Cierra el log si esta activo (flush + resumen por stderr). Seguro siempre.
void interpreter_log_stop(void);
// Revisa si se cumplio la duracion y cierra; flushea el buffer pendiente.
void interpreter_log_tick(void);

// 1 si la sesión fue detenida por el modo STRICT (para el código de salida).
int interpreter_strict_tripped(void);

// Construye los mapas de saltos (@[ ]@ y ?[ ?! ]?) para un codigo cualquiera,
// llenando lm/ijm/ejm (deben tener al menos `len` entradas) con -1 inicial.
// Devuelve por referencia cuantas estructuras quedaron sin cerrar. Lo usa
// internamente rebuild_control_maps y el parser para compilar cuerpos del
// diccionario (OP_CALL).
void interpreter_build_maps(const Inst *inst, int len,
                            int *lm, int *ijm, int *ejm,
                            int *unbalanced_loops, int *unbalanced_ifs);


#endif // INTERPRETER_H
