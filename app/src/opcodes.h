// -------------------------------------------------------------------------
// opcodes.h — Set de instrucciones del lenguaje RPN de bytebeat.
//
// Las constantes de dimensionamiento (MAX_INST, STACK_SIZE, etc.) viven
// en limits.h. Este archivo solo define el enum de opcodes y las structs
// Inst y Program.
// -------------------------------------------------------------------------
#ifndef OPCODES_H
#define OPCODES_H

#include "limits.h"

// -- Rutas -----------------------------------------------------------------
#define DATA_FILE_DEFAULT "./tmp/live.p"

// -- Delimitadores
#define END_CODE "..."

// -- Set de instrucciones ---------------------------------------------------
// No hay opcodes de comentario: los "( )" se eliminan en el preprocesador
// (parser.c) antes de compilar, no llegan a program[] ni cuestan en runtime.
typedef enum {
    // pila
    OP_PUSH,
    OP_ELIM,
    OP_DUP,
    OP_NDUP,
    OP_ROT,
    OP_NROT,
    OP_CLEAR_STACK,
    OP_STACK_LEN,
    // variables de runtime
    OP_VAR_STORE,
    OP_VAR_LOAD,
    // informadores
    OP_SHOW_STACK,
    OP_PRINT,
    OP_PRINT_STR,
    // estructuras de controls
    OP_IF_THEN_ELSE,
    OP_IF_START,
    OP_ELSE_START,
    OP_IF_END,
    OP_LOOP_START,
    OP_LEAVE,
    OP_LOOP_END,
    OP_LOOP_I,
    OP_LOOP_J,
    OP_ANCHOR,
    OP_JUMP,
    OP_CALL,
    // aritmetica
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    // funciones para enteros
    OP_SIN_DEG_MUL,
    OP_COS_DEG_MUL,
    OP_TAN_DEG_MUL,
    OP_SIN_RAD_MUL,
    OP_COS_RAD_MUL,
    OP_TAN_RAD_MUL,
    OP_SIN_HIP_MUL,
    OP_COS_HIP_MUL,
    OP_TAN_HIP_MUL,
    OP_LOG_MUL,
    OP_EXP_MUL,
    OP_SQRT_MUL,
    // funciones para decimales
    OP_SIN_DEG,
    OP_COS_DEG,
    OP_TAN_DEG,
    OP_SIN_RAD,
    OP_COS_RAD,
    OP_TAN_RAD,
    OP_SIN_HIP,
    OP_COS_HIP,
    OP_TAN_HIP,
    OP_LOG,
    OP_EXP,
    OP_SQRT,
    // funciones varias
    OP_ABS,
    OP_MIN,
    OP_MAX,
    // bit a bit
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_NEGATE,
    OP_RSHIFT,
    OP_LSHIFT,
    // comparadores
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS_THAN,
    OP_LESS_EQUAL_THAN,
    OP_GREATER_THAN,
    OP_GREATER_EQUAL_THAN,
    // getters de audio
    OP_T,
    OP_SR_GET,
    OP_CH_GET,
    OP_BUF_GET,
    OP_VMIN_GET,
    OP_VMAX_GET,
    // setters de audio
    OP_TIME_SET,
    OP_SR_SET,
    OP_CH_SET,
    OP_BUF_SET,
    OP_VMIN_SET,
    OP_VMAX_SET,
    OP_VINT_SET,
    OP_OUT,
    OP_DCB_SET,
    OP_SAT_SET,
    OP_LP_SET,
    OP_MUTE_SET,
    // anomalias - errores - setters
    OP_STRICT_SET,
    // setters del visualizador de onda
    OP_SCOPE_SET,
    OP_ZOOM_SET,
    OP_SCOLOR_SET,
    OP_SSTROKE_SET,
    OP_STYPE_SET,
    OP_SRES_SET,
    OP_SFFT_SET,
    OP_SWINX_SET,
    OP_SWINY_SET,
    OP_SWINW_SET,
    OP_SWINH_SET,
    OP_SFULLSCR_SET,
    OP_SDECOR_SET,
    OP_SBGCOLOR_SET,
    OP_SWINTOP_SET,
    OP_WINX_GET,
    OP_WINY_GET,
    OP_WINW_GET,
    OP_WINH_GET,
    // getters de ventana
    OP_SCRW_GET,
    OP_SCRH_GET,
    // setters del delay
    OP_DELAY_SET,
    OP_DTIME_SET,
    OP_DFEED_SET,
    OP_DMIX_SET,
    OP_DFLO_SET,
    OP_DFHI_SET,
    // samples
    OP_SMP_LOAD,
    OP_SMP_REMOVE,
    OP_SMP_RESET,
    OP_SMP_LOOP,
    OP_SMP_GAIN,
    OP_SMP_MIX,
    OP_SMP_REVERSE,
    OP_SMP_GET,
    OP_SMP_PITCH_GET,
    OP_SMP_LEN,
    OP_SMP_FADE_SET,
    OP_SMP_LIVE,
    // osciladores
    OP_SINE,
    OP_TRIANGLE,
    OP_SAW,
    OP_SAW_PB,
    OP_SQUARE,
    OP_SQUARE_PB,
    OP_NOISE,
    // grabación
    OP_RECFILE,
    OP_RECSTART,
    OP_RECSTOP,
    // control de aplicación - setters
    OP_BYE,
    OP_RELOAD,
    OP_DEFAULT,
    OP_ANOM_LOG,
    OP_BCASC_SET,
    OP_SHOW_LIMITS,
    // control de aplicación - gettess
    OP_BCASC_GET,
} OpType;

// Una instruccion compilada: opcode + valor inmediato (solo OP_PUSH y los
// que llevan operando codificado lo usan).
typedef struct {
    OpType op;
    int    val;
} Inst;

// Un programa compilado e independiente. Hay cinco instancias (ver state.h),
// una por bloque de contexto: {a} {a.} {b} {b.} y el implicito {s}. Cada una
// con su propia longitud y sus propios mapas de saltos (cacheados en
// interpreter.c).
typedef struct {
    Inst inst[MAX_INST];
    int  len;
    int  n_anchors;
} Program;

#endif // OPCODES_H
