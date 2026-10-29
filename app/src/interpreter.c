// -------------------------------------------------------------------------
// interpreter.c — Ejecuta las instrucciones de un Program sobre una pila de
// doubles. No conoce nada de sintaxis textual (eso es responsabilidad de
// parser.c) ni de PortAudio; solo lee/escribe el estado global (state.h) y
// llama al modulo de grabacion (wav.h) cuando corresponde.
//
// La aritmetica es conmutable segun vint_mode (state.h):
//   vint_mode = 1 (default, bytebeat): las operaciones + - * / % y las
//     bit a bit/desplazamientos castean a int64, operan, y envuelven el
//     resultado a int32 (overflow autentico, como en el bytebeat clasico).
//     Division y modulo son enteros (truncamiento hacia cero).
//   vint_mode = 0 (floatbeat): las operaciones usan double directamente.
//     Division es float, modulo usa fmod. Bit a bit/desplazamientos siguen
//     siendo enteros (son operaciones inherentemente enteras) pero sin
//     wraparound forzado.
// -------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "limits.h"
#include "scope.h"
#include "interpreter.h"
#include "state.h"
#include "opcodes.h"
#include "diag.h"
#include "strtab.h"
#include "wav.h"
#include "sample.h"
#include "audio.h"
#include "parser.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// -- Mapas de saltos precalculados: loops @[ ]@  e  if/else ?[ ?! ]? --------
// Un juego de mapas por contexto (CTX_APP/CTX_APP_POST/CTX_BUF/
// CTX_BUF_POST/CTX_SAMPLE), porque cada uno tiene su propio Program con sus
// propios indices de instruccion. Se cachean porque cada Program se ejecuta
// muchas veces sin cambiar entre medio (el de CTX_SAMPLE, una vez por
// muestra): recalcular en cada llamada seria recorrer el programa entero
// por cada muestra del buffer.
// Se invalidan explicitamente con interpreter_on_program_loaded().
#define N_CTX 5

// -- Watchdog de instrucciones por ejecucion -------------------------------
// Sin esto, un salto hacia atras (#A ... ->A), un loop de miles de millones
// de vueltas o loops anidados profundos cuelgan el hilo de audio para
// siempre: no hay watchdog ni preemptivo. Al alcanzar el tope se suma una
// anomalia (visible en el contador de stderr y en `log`, con el pc del
// bucle culpable) y se abandona SOLO esa ejecucion: el programa sigue
// vivo y el audio sigue saliendo.
// Presupuestos generosos: un {s} legitimo raro pasa de unos miles de
// instrucciones; 100k deja cabeza para loops grandes y acota el peor caso
// a ~0.1 ms. Si algun programa real queda chico, la senal es un storm de
// anomalias y se sube el define correspondiente.
#define RUN_LIMIT_SAMPLE 100000
#define RUN_LIMIT_BUFFER 500000
#define RUN_LIMIT_APP    5000000

static int run_step_limit_for(ExecContext ctx) {
  switch (ctx) {
  case CTX_SAMPLE:   return RUN_LIMIT_SAMPLE;
  case CTX_BUF:
  case CTX_BUF_POST: return RUN_LIMIT_BUFFER;
  default:           return RUN_LIMIT_APP;   // {a} y {a.}: una vez por carga
  }
}


static int loop_map[N_CTX][MAX_INST];
static int if_jump_map[N_CTX][MAX_INST];
static int else_jump_map[N_CTX][MAX_INST];
static int control_maps_valid[N_CTX];

void interpreter_on_program_loaded(void) {
  for (int i = 0; i < N_CTX; i++) control_maps_valid[i] = 0;
}

// Frame de analisis para un "?[ ... ?! ... ]?" en construccion.
typedef struct { int if_pc; int else_pc; } IfFrame;

void interpreter_build_maps(const Inst *inst, int len,
                            int *lm, int *ijm, int *ejm,
                            int *unbalanced_loops, int *unbalanced_ifs) {
  memset(lm,  -1, sizeof(int) * (size_t)len);
  memset(ijm, -1, sizeof(int) * (size_t)len);
  memset(ejm, -1, sizeof(int) * (size_t)len);
  int nest_stack[MAX_INST];
  int nsp = 0;
  IfFrame if_stack[MAX_INST];
  int isp = 0;
  for (int i = 0; i < len; i++) {
    switch (inst[i].op) {
    case OP_LOOP_START:
      nest_stack[nsp++] = i;
      break;
    case OP_LOOP_END:
      if (nsp > 0) {
        int open = nest_stack[--nsp];
        lm[open] = i;
        lm[i]    = open;
      }
      break;
    case OP_IF_START:
      if (isp < MAX_INST) { if_stack[isp].if_pc = i; if_stack[isp].else_pc = -1; isp++; }
      break;
    case OP_ELSE_START:
      if (isp > 0 && if_stack[isp - 1].else_pc == -1)
        if_stack[isp - 1].else_pc = i;
      break;
    case OP_IF_END:
      if (isp > 0) {
        IfFrame f = if_stack[--isp];
        if (f.else_pc == -1) {
          ijm[f.if_pc] = i;
        } else {
          ijm[f.if_pc]   = f.else_pc;
          ejm[f.else_pc] = i;
        }
      }
      break;
    default:
      break;
    }
  }
  if (unbalanced_loops) *unbalanced_loops = nsp;
  if (unbalanced_ifs)   *unbalanced_ifs   = isp;
}

static void rebuild_control_maps(ExecContext ctx, const Program *prog) {
  int ul = 0, ui = 0;
  interpreter_build_maps(prog->inst, prog->len,
                         loop_map[ctx], if_jump_map[ctx], else_jump_map[ctx],
                         &ul, &ui);
  if (ul > 0) {
    fprintf(stderr, "    Aviso | %d loop(s) @[ sin ]@ correspondiente\n", ul);
    diag_add("    Aviso | %d loop(s) @[ sin ]@ correspondiente", ul);
  }
  if (ui > 0) {
    fprintf(stderr, "    Aviso | %d if(s) ?[ sin ]? correspondiente\n", ui);
    diag_add("    Aviso | %d if(s) ?[ sin ]? correspondiente", ui);
  }
  control_maps_valid[ctx] = 1;
}

// open_pc: posicion del '@[' que abrio este loop — lo necesita `@!` (leave)
// para encontrar el ']@' correspondiente via el mismo loop_map de siempre.
typedef struct { int i; int end; int step; int open_pc; } LoopFrame;


// Contador de anomalias de runtime (ver interpreter.h).
static int anomaly_count = 0;
void interpreter_reset_anomalies(void) { anomaly_count = 0; }
int  interpreter_get_anomaly_count(void) { return anomaly_count; }

// -- Log detallado de anomalias (palabra `log`) ---------------------------
// Mientras esta activo, cada anomalia escribe una linea con contexto,
// posicion en el programa, opcode y fotografia de la pila. La duracion es
// en segundos de audio (sample_index), no de reloj: se cierra solo desde
// el loop principal via interpreter_log_tick().
static FILE   *anom_log_file  = NULL;
static int64_t anom_log_end   = 0;
static int64_t anom_log_count = 0;

int interpreter_log_start(const char *path, double seconds) {
  interpreter_log_stop();
  if (!path || !path[0]) return 0;
  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "      Log | No se pudo abrir '%s' para escritura\n", path);
    return 0;
  }
  anom_log_file  = f;
  anom_log_count = 0;
  double sr = (sample_rate > 0) ? (double)sample_rate : 1.0;
  anom_log_end = sample_index + (int64_t)(seconds * sr);
  fprintf(f, "# Log de anomalias: %.1f segs de audio, sr:%d, desde t:%lld\n",
          seconds, sample_rate, (long long)sample_index);
  fprintf(stderr, "      Log | Registrando anomalias en '%s' por %.1f s\n", path, seconds);
  return 1;
}

void interpreter_log_stop(void) {
  if (!anom_log_file) return;
  fprintf(anom_log_file, "# Cierre: %lld anomalias registradas\n",
          (long long)anom_log_count);
  fclose(anom_log_file);
  anom_log_file = NULL;
  fprintf(stderr, "      Log | Cerrado (%lld anomalias registradas)\n",
          (long long)anom_log_count);
}

void interpreter_log_tick(void) {
  if (!anom_log_file) return;
  if (sample_index >= anom_log_end) { interpreter_log_stop(); return; }
  fflush(anom_log_file);   // flush barato, una vez por buffer de audio
}

static const char *ctx_name(ExecContext c) {
  switch (c) {
  case CTX_APP:      return "app";
  case CTX_APP_POST: return "app.";
  case CTX_BUF:      return "buf";
  case CTX_BUF_POST: return "buf.";
  default:           return "sample";
  }
}

// Nombre legible del opcode para el log. Los opcodes agregados despues que
// no esten en este switch caen al fallback "opN", que igual identifica.
static const char *op_name(OpType op) {
  switch (op) {
  case OP_PUSH: return "push";   case OP_ELIM: return "e";
  case OP_DUP: return "d";       case OP_NDUP: return "nd";
  case OP_ROT: return "r";       case OP_NROT: return "nr";
  case OP_CLEAR_STACK: return "cs";    case OP_STACK_LEN: return "ls";
  case OP_VAR_STORE: return "=:store"; case OP_VAR_LOAD: return "varload";
  case OP_SHOW_STACK: return "::";     case OP_PRINT: return ".:";    case OP_PRINT_STR: return "_:";
  case OP_IF_THEN_ELSE: return "?";  case OP_IF_START: return "?[";
  case OP_ELSE_START: return "?!";   case OP_IF_END: return "]?";
  case OP_LOOP_START: return "@[";   case OP_LEAVE: return "@!";
  case OP_LOOP_END: return "]@";     case OP_LOOP_I: return "i";   case OP_LOOP_J: return "j";
  case OP_JUMP: return "->jump";     case OP_CALL: return "call";
  case OP_ADD: return "+";  case OP_SUB: return "-";  case OP_MUL: return "*";
  case OP_DIV: return "/";  case OP_MOD: return "%";
  case OP_SIN_DEG_MUL: return "gsin*"; case OP_COS_DEG_MUL: return "gcos*"; case OP_TAN_DEG_MUL: return "gtan*";
  case OP_SIN_RAD_MUL: return "rsin*"; case OP_COS_RAD_MUL: return "rcos*"; case OP_TAN_RAD_MUL: return "rtan*";
  case OP_SIN_HIP_MUL: return "hsin*"; case OP_COS_HIP_MUL: return "hcos*"; case OP_TAN_HIP_MUL: return "htan*";
  case OP_LOG_MUL: return "logn*";  case OP_EXP_MUL: return "exp*"; case OP_SQRT_MUL: return "sqrt*";
  case OP_SIN_DEG: return "gsin"; case OP_COS_DEG: return "gcos"; case OP_TAN_DEG: return "gtan";
  case OP_SIN_RAD: return "rsin"; case OP_COS_RAD: return "rcos"; case OP_TAN_RAD: return "rtan";
  case OP_SIN_HIP: return "hsin"; case OP_COS_HIP: return "hcos"; case OP_TAN_HIP: return "htan";
  case OP_LOG: return "logn";  case OP_EXP: return "exp";  case OP_SQRT: return "sqrt";
  case OP_ABS: return "abs";  case OP_MIN: return "min";   case OP_MAX: return "max";
  case OP_AND: return "&";   case OP_OR: return "|";       case OP_XOR: return "^";
  case OP_NEGATE: return "~"; case OP_RSHIFT: return ">>"; case OP_LSHIFT: return "<<";
  case OP_EQUAL: return "=="; case OP_NOT_EQUAL: return "!=";
  case OP_LESS_THAN: return "<"; case OP_LESS_EQUAL_THAN: return "<=";
  case OP_GREATER_THAN: return ">"; case OP_GREATER_EQUAL_THAN: return ">=";
  case OP_T: return "t";  case OP_OUT: return "OUT";
  case OP_SR_SET: return "SR"; case OP_CH_SET: return "CH"; case OP_BUF_SET: return "BUF";
  case OP_VMIN_SET: return "VMIN"; case OP_VMAX_SET: return "VMAX";
  case OP_VINT_SET: return "VINT"; case OP_TIME_SET: return "T";
  case OP_SCOLOR_SET: return "SCOLOR"; case OP_SSTROKE_SET: return "SSTROKE";
  case OP_STYPE_SET: return "STYPE"; case OP_SFFT_SET: return "SFFT";
  case OP_SMP_LOAD: return "smpload"; case OP_SMP_GET: return "sample";
  case OP_SMP_PITCH_GET: return "samplepitch"; case OP_SMP_LEN: return "smplen";
  case OP_SMP_LOOP: return "smploop"; case OP_SMP_MIX: return "smpmix";
  case OP_SMP_GAIN: return "smpgain"; case OP_SMP_REVERSE: return "smpreverse";
  case OP_RECFILE: return "recfile";
  case OP_SHOW_LIMITS: return "showlimits";
  default: break;
  }
  static char buf[16];
  snprintf(buf, sizeof(buf), "op%d", (int)op);
  return buf;
}

// Snapshot de la ejecucion en curso para el reporte de STRICT:
// run_program no es reentrante, asi que un unico snapshot global basta.
static LoopFrame *cur_lframes    = NULL;
static int       *cur_ldepth_ptr = NULL;
static int strict_tripped = 0;
int interpreter_strict_tripped(void) { return strict_tripped; }

// -- Reporte de STRICT -----------------------------------------------------
// Imprime por stderr el contexto completo de la anomalia que detiene la
// sesion: la misma linea detallada que escribe `log`, mas el estado de la
// sesion y la pila de loops activa en el momento del fallo.
static void strict_report(ExecContext ctx, int pc, const Inst *ins,
                          const double *stack, int sp) {
  double sr = (sample_rate > 0) ? (double)sample_rate : 1.0;
  fprintf(stderr,
          "\n---------------------------------------------------------------------\n"
          "STRICT: Anomalia fatal - Sesión detenida\n"
          "t:%lld (%.3fs) ctx:%s pc:%d op:%s sp:%d pila:[",
          (long long)sample_index, (double)sample_index / sr,
          ctx_name(ctx), pc, ins ? op_name(ins->op) : "?", sp);
  int from = sp - 4;
  if (from < 0) from = 0;
  for (int i = from; i < sp; i++) fprintf(stderr, " %g", stack[i]);
  fprintf(stderr, " ]\n");
  fprintf(stderr,
          "   Sesión | sr:%d ch:%d buf:%d vint:%d mute:%d scope:%d stype:%d delay:%d\n",
          sample_rate, num_channels, buf_size, vint_mode,
          mute_enabled, scope_enabled, scope_type, delay_enabled);
  int ld = cur_ldepth_ptr ? *cur_ldepth_ptr : 0;
  if (ld > 0 && cur_lframes) {
    fprintf(stderr, "    Loops | %d activo(s)\n", ld);
    for (int d = 0; d < ld; d++) {
      fprintf(stderr, "  #%d: i:%d end:%d step:%d open_pc:%d\n",
              d, cur_lframes[d].i, cur_lframes[d].end,
              cur_lframes[d].step, cur_lframes[d].open_pc);
    }
  }
  fprintf(stderr,
          "Para capturar la historia completa: \"ruta.log N log\" antes del fragmento.\n"
          "---------------------------------------------------------------------\n");
}

// Registra una anomalia: suma al contador, escribe la linea detallada si
// hay log activo, y con STRICT encendido detiene la sesion con reporte.
// La llama el macro ANOM() desde run_program, que tiene a mano contexto,
// pc, instruccion, pila y el flag running.
static void anom_record(ExecContext ctx, int pc, const Inst *ins,
                        const double *stack, int sp, int *running) {
  anomaly_count++;
  if (anom_log_file) {
    anom_log_count++;
    double sr = (sample_rate > 0) ? (double)sample_rate : 1.0;
    fprintf(anom_log_file, "t:%lld (%.3fs) ctx:%s pc:%d op:%s sp:%d pila:[",
            (long long)sample_index, (double)sample_index / sr,
            ctx_name(ctx), pc, ins ? op_name(ins->op) : "?", sp);
    int from = sp - 4;
    if (from < 0) from = 0;
    for (int i = from; i < sp; i++) fprintf(anom_log_file, " %g", stack[i]);
    fprintf(anom_log_file, " ]\n");
  }
  // STRICT: primera anomalia = fin de la sesion. El chequeo de *running
  // evita un segundo reporte si el shutdown ({a.}) genera anomalias.
  if (strict_enabled && running && *running) {
    strict_report(ctx, pc, ins, stack, sp);
    strict_tripped = 1;
    *running = 0;   // salida limpia: {a.}, recorder, scope, audio
  }
}

#define ANOM() anom_record(ctx, pc, ins, stack, sp, running)

// Almacenamiento de variables en runtime (`=:`, ver parser.c). Persiste
// entre cargas igual que el diccionario de funciones/alias — nunca se
// resetea aca; es justamente el punto (predefinir y reusar entre bloques
// de la misma sesion). El slot de cada nombre lo asigna el parser una sola
// vez (se conserva mientras dure la sesion, aunque el codigo se recargue).
static double var_slots[MAX_WORDS];

// Estado del generador de ruido blanco (OP_NOISE). LCG simple; la semilla
// es fija para que la secuencia sea reproducible entre recargas.
static uint32_t noise_state = 12345u;

// -- Helpers de conversion ------------------------------------------------
// Envuelve un int64 a int32 (wraparound bytebeat) y lo devuelve como double.
static inline double wrap_i64_to_i32(int64_t v) {
  return (double)(int32_t)(uint32_t)v;
}

// Convierte un double a int32 con wraparound, protegiendo el casteo a
// int64 contra valores fuera de rango (evita comportamiento indefinido).
static inline double to_int32(double v) {
  if (!isfinite(v)) return (vmin + vmax) * 0.5;   // silencio, no click en 0
  if (v >  9.2e18) v =  9.2e18;
  if (v < -9.2e18) v = -9.2e18;
  return (double)(int32_t)(uint32_t)(int64_t)v;
}

// Casteo de double a int32 para operaciones de bits y desplazamientos:
// definido para cualquier valor (no-finitos -> 0; fuera de rango int64 se
// clampeta antes de envolver a 32). Evita el UB del casteo directo cuando
// la pila trae NaN/Inf o magnitudes enormes.
static inline int32_t bits_i32(double v) {
  if (!isfinite(v)) return 0;
  if (v >  9.2e18) v =  9.2e18;
  if (v < -9.2e18) v = -9.2e18;
  return (int32_t)(uint32_t)(int64_t)v;
}

// Casteo de double a int64 para operaciones de bits: definido para cualquier
// valor (no-finitos -> 0; fuera de rango int64 se clampeta antes del casteo).
// Es el hermano de 64 bits de bits_i32: & | ^ ~ operan en int64 porque en
// modo float el resultado no se envuelve a 32.
static inline int64_t bits_i64(double v) {
  if (!isfinite(v)) return 0;
  if (v >  9.2e18) v =  9.2e18;
  if (v < -9.2e18) v = -9.2e18;
  return (int64_t)v;
}

// Casteo de double a int para indices, contadores, canales, slots y
// cuentas de shift: definido para cualquier valor. Un no-finito devuelve
// INT_MIN a proposito (no 0): los chequeos de rango posteriores lo
// convierten en anomalia o en clamp, que es exactamente lo que hoy
// produce el hardware x86 con el casteo UB, pero sin el UB y sin perder
// visibilidad (el contador de anomalias y `log` lo siguen viendo).
// Finos fuera de rango int64 se clampetan; el wrap a 32 bits es el de siempre.
static inline int stack_int(double v) {
  if (!isfinite(v)) return (int)INT32_MIN;
  if (v >  9.2e18) v =  9.2e18;
  if (v < -9.2e18) v = -9.2e18;
  return (int)(int32_t)(uint32_t)(int64_t)v;
}

// Punto medio del rango [vmin, vmax] — es el silencio (normaliza a 0.0).
static inline double silence_value(void) {
  return (vmin + vmax) * 0.5;
}

// PolyBLEP: polynomial band-limited step. Corrige la discontinuidad de
// un oscilador alrededor de phase=0 (o phase=1, que es lo mismo).
// dt es el incremento de fase por sample: freq / sample_rate.
// Devuelve la correccion a restar/sumar en el sample actual.
static double polyblep(double phase, double dt) {
  if (dt <= 0.0) return 0.0;
  if (phase < dt) {
    double t = phase / dt;
    return t + t - t * t - 1.0;
  } else if (phase > 1.0 - dt) {
    double t = (phase - 1.0) / dt;
    return t * t + t + 1.0;
  }
  return 0.0;
}


// Nucleo ejecutable reentrante: lo usa run_program (nivel 0) y OP_CALL
// (niveles anidados). La pila es compartida (sp via puntero, con write-back
// antes y despues de cada llamada); el watchdog (steps/step_limit/tripped)
// es compartido por todo el arbol: una ejecucion runaway muere con una
// sola anomalia sin importar en que nivel estaba el bucle.
static void exec_core(ExecContext ctx, const Inst *code, int len,
                      int *lm, int *ijm, int *ejm,
                      double *stack, int *sp_ptr, int *running,
                      int depth, int *steps, int step_limit, int *tripped) {
  int sp = *sp_ptr;
  // Estado de loops propio de este nivel de llamada
  LoopFrame lframes[MAX_LOOP_DEPTH];
  int ldepth = 0;
  LoopFrame *save_lf = cur_lframes;      // el reporte de STRICT muestra
  int       *save_ld = cur_ldepth_ptr;   // el nivel mas interno activo
  cur_lframes    = lframes;
  cur_ldepth_ptr = &ldepth;
  for (int pc = 0; pc < len && *running; pc++) {
    const Inst *ins = &code[pc];
    // DELTA watchdog: compartido y de una sola anomalia por arbol
    if (*tripped || ++(*steps) > step_limit) {
      if (!*tripped) { ANOM(); *tripped = 1; }
      break;
    }
    switch (ins->op) {

      // -- pila ----------------------------------------------------------
    case OP_PUSH: if(sp<STACK_SIZE) stack[sp++]=(double)ins->val; else ANOM(); break;
    case OP_ELIM: if(sp>0) sp--; else ANOM(); break;
    case OP_DUP:  if(sp>0&&sp<STACK_SIZE){stack[sp]=stack[sp-1];sp++;} else ANOM(); break;
    case OP_NDUP: {
      // nd: pop n, copia el valor a profundidad n y lo apila.
      if (sp < 1) { ANOM(); break; }
      int n = stack_int(stack[--sp]);
      if (n < 0 || n >= sp || sp >= STACK_SIZE) { ANOM(); break; }
      stack[sp] = stack[sp - 1 - n];
      sp++;
      break;
    }
    case OP_ROT: if(sp>=2){double tmp=stack[sp-1];stack[sp-1]=stack[sp-2];stack[sp-2]=tmp;} else ANOM(); break;
    case OP_NROT: {
      // nr: pop n, saca el valor a profundidad n y lo mueve al tope.
      if (sp < 1) { ANOM(); break; }
      int n = stack_int(stack[--sp]);
      if (n < 0 || n >= sp) { ANOM(); break; }
      int idx = sp - 1 - n;
      double val = stack[idx];
      for (int k = idx; k < sp - 1; k++) stack[k] = stack[k + 1];
      stack[sp - 1] = val;
      break;
    }
      // cs: "clear stack" — vacia la pila por completo. Nunca falla.
    case OP_CLEAR_STACK:
      sp = 0;
      break;
      // ls: "length stack" — apila la profundidad de la pila incluyendo
      // el valor que se esta apilando. Pila vacia -> apila 1.
    case OP_STACK_LEN:
      if (sp < STACK_SIZE) { stack[sp] = (double)(sp + 1); sp++; } else ANOM();
      break;

      // -- variables (=:) ------------------------------------------------
    case OP_VAR_STORE:
      if (sp > 0) { var_slots[ins->val] = stack[--sp]; } else ANOM();
      break;
    case OP_VAR_LOAD:
      if (sp < STACK_SIZE) { stack[sp++] = var_slots[ins->val]; } else ANOM();
      break;

      // -- informadores (no destructivos: inspeccionan sin consumir) -----
    case OP_SHOW_STACK:
      if (sp > 0) {
        fprintf(stderr, "     Pila |");
        for (int i = 0; i < sp; i++) fprintf(stderr, " %g", stack[i]);
        fprintf(stderr, "\n");
      }
      break;
    case OP_PRINT:
      if (sp > 0) fprintf(stderr, "    Valor | %g\n", stack[sp-1]);
      break;
    case OP_PRINT_STR:
      if (sp > 0) {
        int idx = stack_int(stack[sp-1]);
        if (strtab_is_valid(idx)) fprintf(stderr, "    Texto | %s\n", strtab_get(idx));
        else { fprintf(stderr, "    Texto | <indice invalido: %d>\n", idx); ANOM(); }
      }
      break;

      // -- estructura ----------------------------------------------------
    case OP_IF_THEN_ELSE: if(sp>=3){
        if(stack[--sp] == 0) stack[sp-2] = stack[sp-1];
        sp--;
      } else ANOM(); break;
    case OP_IF_START: {
      if (sp < 1) { ANOM(); break; }
      double cond = stack[--sp];
      if (cond == 0 && ijm[pc] >= 0) pc = ijm[pc];
      break;
    }
    case OP_ELSE_START:
      if (ejm[pc] >= 0) pc = ejm[pc];
      break;
    case OP_IF_END:
      break;

      // -- LOOP ----------------------------------------------------------
      // Sintaxis en pila (orden de push): fin inicio paso @[
    case OP_LOOP_START: {
      if (sp < 3) { ANOM(); break; }
      int paso   = stack_int(stack[--sp]);
      int inicio = stack_int(stack[--sp]);
      int fin    = stack_int(stack[--sp]);
      if (paso == 0) {
        if (lm[pc] >= 0) pc = lm[pc];
        break;
      }
      // -INT32_MIN no es representable: sin este clamp, la correccion de
      // signo de abajo es UB y el paso queda negativo igual.
      if (paso == INT32_MIN) paso = INT32_MAX;
      if (inicio < fin && paso < 0) paso = -paso;
      if (inicio > fin && paso > 0) paso = -paso;
      int empty = (inicio < fin) ? (inicio >= fin) : (inicio <= fin);
      if (inicio == fin) empty = 1;
      if (empty) {
        if (lm[pc] >= 0) pc = lm[pc];
        break;
      }
      if (ldepth < MAX_LOOP_DEPTH) {
        lframes[ldepth].i       = inicio;
        lframes[ldepth].end     = fin;
        lframes[ldepth].step    = paso;
        lframes[ldepth].open_pc = pc;
        ldepth++;
      }
      break;
    }
    case OP_LEAVE:
      if (ldepth > 0) {
        ldepth--;
        int close_pc = lm[lframes[ldepth].open_pc];
        if (close_pc >= 0) pc = close_pc;
      } else ANOM();
      break;
    case OP_LOOP_END: {
      if (ldepth == 0) { ANOM(); break; }
      LoopFrame *f = &lframes[ldepth - 1];
      // Avance en int64: si i se sale de int32, el loop se cierra en vez
      // de desbordar signed (UB). Con pasos gigantes eso es exactamente
      // lo que el programa queria: una iteracion y afuera.
      int64_t ni = (int64_t)f->i + (int64_t)f->step;
      if (ni < INT32_MIN || ni > INT32_MAX) { ldepth--; break; }
      f->i = (int)ni;
      int continua = (f->step > 0) ? (f->i < f->end) : (f->i > f->end);
      if (continua) {
        if (lm[pc] >= 0) pc = lm[pc];
      } else {
        ldepth--;
      }
      break;
    }
    case OP_LOOP_I:
      if (sp < STACK_SIZE && ldepth > 0) stack[sp++] = lframes[ldepth - 1].i;
      else ANOM();
      break;
    case OP_LOOP_J:
      if (sp < STACK_SIZE && ldepth > 1) stack[sp++] = lframes[ldepth - 2].i;
      else ANOM();
      break;

    case OP_ANCHOR:
      break;
    case OP_JUMP:
      if (sp > 0) {
        double activar = stack[--sp];
        if (activar != 0) {
          if (ins->val >= 0) pc = ins->val;
          else ANOM();
        }
      } else ANOM();
      break;

      // -- aritmetica (conmutable segun vint_mode) -----------------------
    case OP_ADD:
      if (sp >= 2) {
        if (vint_mode) stack[sp-2] = wrap_i64_to_i32(bits_i64(stack[sp-2]) + bits_i64(stack[sp-1]));
        else           stack[sp-2] = stack[sp-2] + stack[sp-1];
        sp--;
      } else ANOM();
      break;
    case OP_SUB:
      if (sp >= 2) {
        if (vint_mode) stack[sp-2] = wrap_i64_to_i32(bits_i64(stack[sp-2]) - bits_i64(stack[sp-1]));
        else           stack[sp-2] = stack[sp-2] - stack[sp-1];
        sp--;
      } else ANOM();
      break;
    case OP_MUL:
      if (sp >= 2) {
        if (vint_mode) stack[sp-2] = wrap_i64_to_i32(bits_i64(stack[sp-2]) * bits_i64(stack[sp-1]));
        else           stack[sp-2] = stack[sp-2] * stack[sp-1];
        sp--;
      } else ANOM();
      break;
    case OP_DIV:
      if (sp >= 2) {
        if (vint_mode) {
          int64_t b = bits_i64(stack[sp-1]);
          if (b != 0) {
            stack[sp-2] = (double)(bits_i64(stack[sp-2]) / b);
          } else {
            stack[sp-2] = silence_value();  // división por cero → silencio
            ANOM();
          }
          sp--;
        } else {
          if (stack[sp-1] != 0) {
            stack[sp-2] = stack[sp-2] / stack[sp-1];
          } else {
            stack[sp-2] = silence_value();  // división por cero → silencio
            ANOM();
          }
          sp--;
        }
      } else ANOM();
      break;
    case OP_MOD:
      if (sp >= 2) {
        if (vint_mode) {
          int64_t b = bits_i64(stack[sp-1]);
          if (b != 0) {
            stack[sp-2] = (double)(bits_i64(stack[sp-2]) % b);
          } else {
            stack[sp-2] = silence_value();  // módulo por cero → silencio
            ANOM();
          }
          sp--;
        } else {
          if (stack[sp-1] != 0) {
            stack[sp-2] = fmod(stack[sp-2], stack[sp-1]);
          } else {
            stack[sp-2] = silence_value();  // módulo por cero → silencio
            ANOM();
          }
          sp--;
        }
      } else ANOM();
      break;
    case OP_ABS:
      if (sp >= 1) {
        stack[sp-1] = vint_mode ? (double)llabs(bits_i64(stack[sp-1])) : fabs(stack[sp-1]);
      } else ANOM();
      break;
    case OP_MIN: if(sp>=2){stack[sp-2]=(stack[sp-2]<stack[sp-1])?stack[sp-2]:stack[sp-1]; sp--;}else ANOM();break;
    case OP_MAX: if(sp>=2){stack[sp-2]=(stack[sp-2]>stack[sp-1])?stack[sp-2]:stack[sp-1]; sp--;}else ANOM();break;

      // -- funciones para enteros  -------------------------------------
    case OP_SIN_DEG_MUL: if(sp>=2){double r=sin(stack[sp-2]*0.0174533)*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_COS_DEG_MUL: if(sp>=2){double r=cos(stack[sp-2]*0.0174533)*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_TAN_DEG_MUL: if(sp>=2){double r=tan(stack[sp-2]*0.0174533)*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_SIN_RAD_MUL: if(sp>=2){double r=sin(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_COS_RAD_MUL: if(sp>=2){double r=cos(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_TAN_RAD_MUL: if(sp>=2){double r=tan(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_SIN_HIP_MUL: if(sp>=2){double r=sinh(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_COS_HIP_MUL: if(sp>=2){double r=cosh(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_TAN_HIP_MUL: if(sp>=2){double r=tanh(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_LOG_MUL:     if(sp>=2){double r=log(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_EXP_MUL:     if(sp>=2){double r=exp(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;
    case OP_SQRT_MUL:    if(sp>=2){double r=sqrt(stack[sp-2])*stack[sp-1]; stack[sp-2]=vint_mode?to_int32(r):r; sp--;}else ANOM();break;

      // -- funciones para decimales  -------------------------------------
    case OP_SIN_DEG: if(sp>=1){double r=sin(stack[sp-1]*0.0174533); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_COS_DEG: if(sp>=1){double r=cos(stack[sp-1]*0.0174533); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_TAN_DEG: if(sp>=1){double r=tan(stack[sp-1]*0.0174533); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_SIN_RAD: if(sp>=1){double r=sin(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_COS_RAD: if(sp>=1){double r=cos(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_TAN_RAD: if(sp>=1){double r=tan(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_SIN_HIP: if(sp>=1){double r=sinh(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_COS_HIP: if(sp>=1){double r=cosh(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_TAN_HIP: if(sp>=1){double r=tanh(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_LOG:     if(sp>=1){double r=log(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_EXP:     if(sp>=1){double r=exp(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;
    case OP_SQRT:    if(sp>=1){double r=sqrt(stack[sp-1]); stack[sp-1]=vint_mode?to_int32(r):r; }else ANOM();break;

      // -- bit a bit (siempre enteras, wraparound en modo entero) --------
    case OP_AND: if(sp>=2){int64_t r=bits_i64(stack[sp-2])&bits_i64(stack[sp-1]); stack[sp-2]=vint_mode?wrap_i64_to_i32(r):(double)r; sp--;}else ANOM();break;
    case OP_OR:  if(sp>=2){int64_t r=bits_i64(stack[sp-2])|bits_i64(stack[sp-1]); stack[sp-2]=vint_mode?wrap_i64_to_i32(r):(double)r; sp--;}else ANOM();break;
    case OP_XOR: if(sp>=2){int64_t r=bits_i64(stack[sp-2])^bits_i64(stack[sp-1]); stack[sp-2]=vint_mode?wrap_i64_to_i32(r):(double)r; sp--;}else ANOM();break;
    case OP_NEGATE: if(sp>=1){int64_t r=~bits_i64(stack[sp-1]); stack[sp-1]=vint_mode?wrap_i64_to_i32(r):(double)r;}else ANOM();break;

    case OP_LSHIFT: if(sp>=2){
        // Modo entero (bytebeat): cuenta mascara a 5 bits, como JavaScript
        // y la tradicion bytebeat (cuentas negativas incluidas, via
        // complemento a dos: -1 & 31 = 31). Modo float: cuenta clampeta
        // 0..63 y con >= 32 satura: todos los bits salen de la palabra y
        // el resultado es 0.
        int sh=stack_int(stack[sp-1]);
        uint64_t a=(uint64_t)(uint32_t)bits_i32(stack[sp-2]);
        if (vint_mode) sh &= 31;
        else { if(sh<0)sh=0; if(sh>63)sh=63; }
        uint64_t r=(a<<sh) & 0xFFFFFFFFu;
        stack[sp-2]=vint_mode?wrap_i64_to_i32((int64_t)r):(double)r; sp--;
      }else ANOM();break;

      /*
    case OP_LSHIFT: if(sp>=2){
        // Cuenta clampeta; el shift se hace en 64 bits (definido hasta 63) y
        // se mascara a 32: los bits que salen de la palabra se pierden, y con
        // cuenta >= 32 el resultado es 0, como documenta el lenguaje.
        int sh=stack_int(stack[sp-1]); if(sh<0)sh=0; if(sh>63)sh=63;
        uint64_t a=(uint64_t)(uint32_t)bits_i32(stack[sp-2]);
        uint64_t r=(a<<sh) & 0xFFFFFFFFu;
        stack[sp-2]=vint_mode?wrap_i64_to_i32((int64_t)r):(double)r; sp--;
      }else ANOM();break;
      */
    case OP_RSHIFT: if(sp>=2){
        // Aritmetico sobre el valor de 32 bits sign-extendido a 64.
        // Modo entero: cuenta mascara & 31 como JS. Modo float: clamp
        // 0..63 y con >= 32 satura al signo (0 o -1).
        int sh=stack_int(stack[sp-1]);
        int64_t a=(int64_t)bits_i32(stack[sp-2]);
        if (vint_mode) sh &= 31;
        else { if(sh<0)sh=0; if(sh>63)sh=63; }
        int64_t r=a>>sh;
        stack[sp-2]=vint_mode?wrap_i64_to_i32(r):(double)r; sp--;
      }else ANOM();break;

      /*
    case OP_RSHIFT: if(sp>=2){
        // Aritmetico sobre el valor de 32 bits sign-extendido a 64: definido
        // hasta cuenta 63, y con cuenta >= 32 satura al signo (0 o -1).
        int sh=stack_int(stack[sp-1]); if(sh<0)sh=0; if(sh>63)sh=63;
        int64_t a=(int64_t)bits_i32(stack[sp-2]);
        int64_t r=a>>sh;
        stack[sp-2]=vint_mode?wrap_i64_to_i32(r):(double)r; sp--;
      }else ANOM();break;
      */




      // -- comparadores (devuelven -1 verdadero / 0 falso) ---------------
    case OP_EQUAL:             if(sp>=2){stack[sp-2]=(stack[sp-2]==stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;
    case OP_NOT_EQUAL:         if(sp>=2){stack[sp-2]=(stack[sp-2]!=stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;
    case OP_LESS_THAN:         if(sp>=2){stack[sp-2]=(stack[sp-2]< stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;
    case OP_LESS_EQUAL_THAN:   if(sp>=2){stack[sp-2]=(stack[sp-2]<=stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;
    case OP_GREATER_EQUAL_THAN:if(sp>=2){stack[sp-2]=(stack[sp-2]>=stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;
    case OP_GREATER_THAN:      if(sp>=2){stack[sp-2]=(stack[sp-2]> stack[sp-1])?-1.0:0.0;sp--;}else ANOM();break;

      // -- getters de audio ----------------------------------------------
    case OP_T:       if(sp<STACK_SIZE) stack[sp++]=(double)sample_index; else ANOM(); break;
    case OP_SR_GET:  if(sp<STACK_SIZE) stack[sp++]=(double)sample_rate;  else ANOM(); break;
    case OP_CH_GET:  if(sp<STACK_SIZE) stack[sp++]=(double)num_channels; else ANOM(); break;
    case OP_BUF_GET: if(sp<STACK_SIZE) stack[sp++]=(double)buf_size;     else ANOM(); break;
    case OP_VMIN_GET:if(sp<STACK_SIZE) stack[sp++]=vmin;                 else ANOM(); break;
    case OP_VMAX_GET:if(sp<STACK_SIZE) stack[sp++]=vmax;                 else ANOM(); break;

      // -- setters de audio ----------------------------------------------
    case OP_TIME_SET:
      if(sp>0){ double v=stack[--sp]; if(isfinite(v)) sample_index=(int64_t)v; else ANOM(); } else ANOM();
      break;
    case OP_SR_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v>0){ pending_sample_rate=v; stream_dirty=1; } } else ANOM();
      break;
    case OP_CH_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v>=1&&v<=MAX_CHANNELS){ pending_num_channels=v; stream_dirty=1; } } else ANOM();
      break;
    case OP_BUF_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v>0){ pending_buf_size=v; stream_dirty=1; } } else ANOM();
      break;
    case OP_VMIN_SET:
      if(sp>0){ double v=stack[--sp]; if(isfinite(v)) vmin=v; else ANOM(); } else ANOM();
      break;
    case OP_VMAX_SET:
      if(sp>0){ double v=stack[--sp]; if(isfinite(v)) vmax=v; else ANOM(); } else ANOM();
      break;
    case OP_VINT_SET:
      if(sp>0){ vint_mode=(stack_int(stack[--sp])!=0)?1:0; } else ANOM();
      break;
      // OUT: "valor canal OUT" — canal viene en el tope, valor debajo.
      // Escribe el valor en sample_out[canal] si el canal esta en rango.
    case OP_OUT:
      if(sp>=2){
        int channel=stack_int(stack[--sp]);
        double value=stack[--sp];
        if(channel>=0 && channel<num_channels) sample_out[channel]=value;
        else ANOM();
      } else ANOM();
      break;
    case OP_DCB_SET:
      if(sp>0){ dcb_enabled=(stack_int(stack[--sp])!=0)?1:0; } else ANOM();
      break;
    case OP_SAT_SET:
      if(sp>0){ sat_enabled=(stack_int(stack[--sp])!=0)?1:0; } else ANOM();
      break;
    case OP_LP_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>99)v=99; lp_amount=v; } else ANOM();
      break;
    case OP_MUTE_SET:
      if(sp>0){
        int nuevo=(stack_int(stack[--sp])!=0)?1:0;
        if(nuevo!=mute_enabled){
          fprintf(stderr,"    Audio | mute %s\n", nuevo ? "on" : "off");
        }
        mute_enabled=nuevo;
      } else ANOM();
      break;

      // -- Anomalías - Errores ------------------------------------------
    case OP_STRICT_SET:
      if(sp>0){
        int nuevo=(stack_int(stack[--sp])!=0)?1:0;
        if(nuevo!=strict_enabled){
          fprintf(stderr,"   Strict | %s\n",
                  nuevo ? "on"
                  : "off");
        }
        strict_enabled=nuevo;
      } else ANOM();
      break;

      // -- Controles de visualización - Scope ---------------------------
    case OP_SCOPE_SET:
      if(sp>0){ scope_enabled=(stack_int(stack[--sp])!=0)?1:0; } else ANOM();
      break;
    case OP_ZOOM_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<1)v=1; if(v>2000)v=2000; scope_zoom=v; } else ANOM();
      break;
    case OP_SCOLOR_SET:
      if(sp>=2){
        int channel=stack_int(stack[--sp]);
        int str_idx=stack_int(stack[--sp]);
        if(channel>=0 && channel<MAX_CHANNELS && strtab_is_valid(str_idx)){
          int r,g,b;
          if(scope_parse_color(strtab_get(str_idx),&r,&g,&b)){
            scope_set_color(channel,r,g,b);
          } else {
            fprintf(stderr,"   SCOLOR | formato invalido '%s' (usar \"#rrggbb\")\n",
                    strtab_get(str_idx));
            ANOM();
          }
        } else ANOM();
      } else ANOM();
      break;
    case OP_SSTROKE_SET:
      if(sp>=2){
        int channel=stack_int(stack[--sp]);
        int width=stack_int(stack[--sp]);
        scope_set_stroke(channel,width);
      } else ANOM();
      break;
    case OP_STYPE_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>6)v=6; scope_type=v; } else ANOM();
      break;
    case OP_SRES_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<1)v=1; if(v>16)v=16; scope_resolution=v; } else ANOM();
      break;
    case OP_SFFT_SET:
      if(sp>0){
        int v=stack_int(stack[--sp]);
        // Redondear a la potencia de 2 válida más cercana
        if(v<=256) v=256; else if(v<=512) v=512;
        else if(v<=1024) v=1024; else v=2048;
        scope_fft_size=v;
      } else ANOM();
      break;
    case OP_SWINX_SET:
      if(sp>0){ scope_set_pos_x(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SWINY_SET:
      if(sp>0){ scope_set_pos_y(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SWINW_SET:
      if(sp>0){ scope_set_size_w(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SWINH_SET:
      if(sp>0){ scope_set_size_h(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SFULLSCR_SET:
      if(sp>0){ scope_set_fullscreen(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SDECOR_SET:
      if(sp>0){ scope_set_decorated(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_SBGCOLOR_SET:
      if(sp>=1){
        int str_idx=stack_int(stack[--sp]);
        if(strtab_is_valid(str_idx)){
          int r,g,b;
          if(scope_parse_color(strtab_get(str_idx),&r,&g,&b)){
            scope_set_bg_color(r,g,b);
          } else {
            fprintf(stderr," SBGCOLOR | formato invalido '%s' (usar \"#rrggbb\")\n",
                    strtab_get(str_idx));
            ANOM();
          }
        } else ANOM();
      } else ANOM();
      break;
    case OP_SWINTOP_SET:
      if(sp>0){ scope_set_always_on_top(stack_int(stack[--sp])); } else ANOM();
      break;
    case OP_WINX_GET:
      if(sp<STACK_SIZE) stack[sp++]=(double)scope_get_winx(); else ANOM();
      break;
    case OP_WINY_GET:
      if(sp<STACK_SIZE) stack[sp++]=(double)scope_get_winy(); else ANOM();
      break;
    case OP_WINW_GET:
      if(sp<STACK_SIZE) stack[sp++]=(double)scope_get_winw(); else ANOM();
      break;
    case OP_WINH_GET:
      if(sp<STACK_SIZE) stack[sp++]=(double)scope_get_winh(); else ANOM();
      break;
    case OP_SCRW_GET: {
      int sw = 0, sh = 0;
      scope_screen_size(&sw, &sh);
      if(sp<STACK_SIZE) stack[sp++]=(double)sw; else ANOM();
      break;
    }
    case OP_SCRH_GET: {
      int sw = 0, sh = 0;
      scope_screen_size(&sw, &sh);
      if(sp<STACK_SIZE) stack[sp++]=(double)sh; else ANOM();
      break;
    }

      // -- Controle de delay --------------------------------------------
    case OP_DELAY_SET:
      if(sp>0){ delay_enabled=(stack_int(stack[--sp])!=0)?1:0; } else ANOM();
      break;
    case OP_DTIME_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<1)v=1; if(v>2000)v=2000; delay_time=v; } else ANOM();
      break;
    case OP_DFEED_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>100)v=100; delay_feed=v; } else ANOM();
      break;
    case OP_DMIX_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>100)v=100; delay_mix=v; } else ANOM();
      break;
    case OP_DFLO_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>20000)v=20000; delay_feed_lo=v; } else ANOM();
      break;
    case OP_DFHI_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; if(v>20000)v=20000; delay_feed_hi=v; } else ANOM();
      break;

      // -- samples -------------------------------------------------------
      // smpload: "path" slot smpload — carga un archivo raw en el slot.
    case OP_SMP_LOAD:
      if (sp >= 2) {
        int slot    = stack_int(stack[--sp]);
        int str_idx = stack_int(stack[--sp]);
        if (strtab_is_valid(str_idx)) {
          sample_load(strtab_get(str_idx), slot);
        } else {
          fprintf(stderr, "  smpload | índice de string invalido (%d)\n", str_idx);
          ANOM();
        }
      } else ANOM();
      break;
    case OP_SMP_REMOVE:
      if (sp >= 1) {
        int slot = stack_int(stack[--sp]);
        sample_remove(slot);
      } else ANOM();
      break;
    case OP_SMP_RESET:
      sample_reset();
      break;
      // smploop: "slot smploop" — ajusta el largo del sample al
      // zero-crossing mas cercano desde el final. Elimina clicks en loops.
    case OP_SMP_LOOP:
      if (sp >= 1) {
        int slot = stack_int(stack[--sp]);
        int new_len = sample_loop(slot);
        if (sp < STACK_SIZE) stack[sp++] = (double)new_len;
        else ANOM();
      } else ANOM();
      break;
      // smpgain: "gain slot smpgain" — multiplica todas las muestras del
      // sample por gain. Util para normalizar samples con volumen bajo.
    case OP_SMP_GAIN:
      if (sp >= 2) {
        int slot = stack_int(stack[--sp]);
        float gain = (float)stack[--sp];
        sample_gain(slot, gain);
      } else ANOM();
      break;
      // smpmix: "src_slot dst_slot smpmix" — mezcla src_slot sobre
      // dst_slot (suma con clip a [-1, 1]). Los dos samples deben tener
      // el mismo largo; si no, mezcla hasta el minimo de los dos.
    case OP_SMP_MIX:
      if (sp >= 2) {
        int dst_slot = stack_int(stack[--sp]);
        int src_slot = stack_int(stack[--sp]);
        sample_mix(dst_slot, src_slot);
      } else ANOM();
      break;
      // smpreverse: "slot smpreverse" — invierte el sample en el lugar.
    case OP_SMP_REVERSE:
      if (sp >= 1) {
        int slot = stack_int(stack[--sp]);
        sample_reverse(slot);
      } else ANOM();
      break;

      // sample: posicion slot sample — valor en esa posicion (wrapping por
      // modulo), escalado a [VMIN, VMAX]. Entero en modo int, float en float.
    case OP_SMP_GET:
      if (sp >= 2) {
        int slot = stack_int(stack[--sp]);
        double pos  = stack[--sp];
        double val  = sample_get(slot, pos);
        if (sp < STACK_SIZE) stack[sp++] = vint_mode ? to_int32(val) : val;
        else ANOM();
      } else ANOM();
      break;
      // smplen: slot smplen — largo del sample en muestras.
      // smpremove: slot smpremove — libera la memoria del slot.
      // smpreset: libera la memoria de todos los slots.
      // samplepitch: "freq slot samplepitch" — sample con pitch en Hz.
      // Calcula la posicion internamente usando sample_index y sample_rate,
      // como los osciladores. Convierte el sample en un instrumento melodico.
    case OP_SMP_PITCH_GET:
      if (sp >= 2) {
        int slot = stack_int(stack[--sp]);
        double freq = stack[--sp];
        int len = sample_length(slot);
        if (len > 0) {
          double phase = fmod((double)sample_index * freq / (double)sample_rate, 1.0);
          if (phase < 0.0) phase += 1.0;
          double pos = phase * (double)len;
          double val = sample_get(slot, pos);
          if (sp < STACK_SIZE) stack[sp++] = vint_mode ? to_int32(val) : val;
          else ANOM();
        } else {
          if (sp < STACK_SIZE) stack[sp++] = silence_value();
          else ANOM();
        }
      } else ANOM();
      break;
    case OP_SMP_LEN:
      if (sp >= 1) {
        int slot = stack_int(stack[--sp]);
        if (sp < STACK_SIZE) stack[sp++] = (double)sample_length(slot);
        else ANOM();
      } else ANOM();
      break;
      // smplive: n canal slot smplive — captura n muestras del audio
      // procesado del canal y las publica atomicamente en el slot.
    case OP_SMP_LIVE:
      if (sp >= 3) {
        int slot = stack_int(stack[--sp]);
        int ch   = stack_int(stack[--sp]);
        int n    = stack_int(stack[--sp]);
        if (!sample_live(slot, ch, n)) ANOM();
      } else ANOM();
      break;
      // SMPFADE: valor SMPFADE — samples de crossfade en el punto de loop
      // de los samples (0 = desactivado). Suaviza la transicion final->
      // principio del loop, util cuando el corte no es perfecto.
    case OP_SMP_FADE_SET:
      if(sp>0){ int v=stack_int(stack[--sp]); if(v<0)v=0; smp_crossfade_len=v; } else ANOM();
      break;

      // -- osciladores: consumen FRECUENCIA en Hz, calculan fase internamente
      // usando sample_index y sample_rate. Funcionan igual en modo entero
      // y float porque la fase se computa con doubles dentro del opcode.
    case OP_SINE:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double norm=sin(phase*2.0*M_PI);
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
      // Triangle: sube de -1 a 1 en [0, 0.5], baja de 1 a -1 en [0.5, 1].
    case OP_TRIANGLE:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double norm;
        if (phase < 0.5) {
          norm = -1.0 + phase * 4.0;
        } else {
          norm =  1.0 - (phase - 0.5) * 4.0;
        }
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
    case OP_SAW:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double norm=phase*2.0-1.0;
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
    case OP_SAW_PB:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double dt = freq / (double)sample_rate;
        double norm = phase * 2.0 - 1.0;
        // PolyBLEP: suaviza la discontinuidad en phase=0/1
        norm -= polyblep(phase, dt);
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
    case OP_SQUARE:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double norm=(phase<0.5)?1.0:-1.0;
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
    case OP_SQUARE_PB:
      if(sp>=1){
        double freq=stack[--sp];
        double phase=fmod((double)sample_index*freq/(double)sample_rate,1.0);
        if(phase<0.0) phase+=1.0;
        double dt = freq / (double)sample_rate;
        double norm = (phase < 0.5) ? 1.0 : -1.0;
        // PolyBLEP: dos discontinuidades (en 0 y en 0.5)
        norm += polyblep(phase, dt);
        norm -= polyblep(fmod(phase + 0.5, 1.0), dt);
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        if(sp<STACK_SIZE) stack[sp++]=vint_mode?to_int32(r):r; else ANOM();
      } else ANOM();
      break;
      // noise: ruido blanco, no consume nada. Empuja un valor aleatorio
      // uniforme en [VMIN, VMAX].
    case OP_NOISE:
      if(sp<STACK_SIZE){
        noise_state=noise_state*1103515245u+12345u;
        double norm=((double)(noise_state>>16)/32768.0)-1.0;
        double center=(vmin+vmax)*0.5, amp=(vmax-vmin)*0.5;
        double r=center+norm*amp;
        stack[sp++]=vint_mode?to_int32(r):r;
      } else ANOM();
      break;

      // -- grabacion -----------------------------------------------------
    case OP_RECFILE:
      if(sp>=1){
        int idx=stack_int(stack[--sp]);
        if(strtab_is_valid(idx)) recorder_set_path(strtab_get(idx));
        else { fprintf(stderr,"  recfile | índice de string inválido (%d)\n",idx); ANOM(); }
      } else ANOM();
      break;
    case OP_RECSTART:
      recorder_start();
      break;
    case OP_RECSTOP:
      recorder_stop();
      break;

      // -- control de aplicacion -----------------------------------------
    case OP_BYE:
      if(running) *running=0;
      break;
      // reload: fuerza una recarga del archivo principal aunque no haya
      // cambiado en disco. Sin operandos: si se la llama, actua.
    case OP_RELOAD:
      force_reload = 1;
      break;
      // default: restablece toda la configuracion del sistema.
    case OP_DEFAULT:
      state_reset_defaults();
      scope_reset_defaults();
      audio_reset_processing();
      interpreter_reset_anomalies();
      break;
      // showlimits: resumen de uso contra los limites del sistema
      // (limits.h). Informativo: sin efecto de pila, nunca falla.
      // Los numeros de pila/loops son del instante de la llamada;
      // los de bloques/anclas, de la ultima compilacion.
    case OP_SHOW_LIMITS:
      printf("  Limites | Bloques:  a %d  a. %d  b %d  b. %d  s %d   (max %d inst c/u)\n",
             prog_app.len, prog_app_post.len, prog_buf.len,
             prog_buf_post.len, prog_sample.len, MAX_INST);
      printf("          | Anclas:   a %d  a. %d  b %d  b. %d  s %d   (max %d c/u)\n",
             prog_app.n_anchors, prog_app_post.n_anchors,
             prog_buf.n_anchors, prog_buf_post.n_anchors,
             prog_sample.n_anchors, MAX_ANCHORS);
      printf("          | Audio:    slots %d/%d  canales %d/%d  sr %d  buf %d\n",
             sample_used_slots(), MAX_SMP_SLOTS,
             num_channels, MAX_CHANNELS, sample_rate, buf_size);
      printf("          | Pila:     stack %d/%d  loops %d/%d\n",
             sp, STACK_SIZE, ldepth, MAX_LOOP_DEPTH);
      printf("          | Codigo:   source %d/%d B  defs %d/%d  vars %d  strings %d/%d\n",
             (int)strlen(rpn_source), RPN_BUF_SIZE - 1,
             parser_dict_count(), MAX_WORDS,
             parser_var_slots_used(), strtab_count(), MAX_STRINGS);
      fflush(stdout);
      break;
      // log: "path segundos log" — log detallado de anomalias.
      // Con segundos <= 0 corta el log activo sin abrir nada.
    case OP_ANOM_LOG:
      if (sp >= 2) {
        int secs = stack_int(stack[--sp]);
        int idx  = stack_int(stack[--sp]);
        if (secs <= 0) {
          interpreter_log_stop();
        } else if (strtab_is_valid(idx)) {
          if (!interpreter_log_start(strtab_get(idx), (double)secs)) ANOM();
        } else {
          fprintf(stderr, "      log | índice de string inválido (%d)\n", idx);
          ANOM();
        }
      } else ANOM();
      break;
      // BCASC: modo bloques en cascada. Afecta a la PROXIMA carga, no a
      // la que ejecuta esta palabra (la compilacion precede al {a}).
    case OP_BCASC_SET:
      if(sp>0){
        int nuevo=(stack_int(stack[--sp])!=0)?1:0;
        if(nuevo!=bcasc_mode){
          fprintf(stderr,"    Bcasc | %s\n",
                  nuevo ? "on (proxima carga: cascada)"
                  : "off (proxima carga: reemplazo completo)");
        }
        bcasc_mode=nuevo;
      } else ANOM();
      break;
    case OP_BCASC_GET:
      if(sp<STACK_SIZE) stack[sp++]=(double)bcasc_mode; else ANOM();
      break;
    case OP_CALL: {
      // '= nombre: llamada en runtime a la entrada del diccionario
      // (late binding: ejecuta la definicion vigente).
      if (depth + 1 >= MAX_CALL_DEPTH) { ANOM(); break; }
      const Inst *cc = NULL;
      int cl = 0;
      int *clm = NULL, *cijm = NULL, *cejm = NULL;
      if (!dict_entry_code(ins->val, &cc, &cl, &clm, &cijm, &cejm)) { ANOM(); break; }
      *sp_ptr = sp;
      exec_core(ctx, cc, cl, clm, cijm, cejm, stack, sp_ptr, running,
                depth + 1, steps, step_limit, tripped);
      sp = *sp_ptr;
      break;
    }
    }
  }
  *sp_ptr = sp;
  cur_lframes    = save_lf;
  cur_ldepth_ptr = save_ld;
}

void run_program(ExecContext ctx, Program *prog, int *running) {
  double stack[STACK_SIZE];
  int sp = 0;
  if (!control_maps_valid[ctx]) rebuild_control_maps(ctx, prog);
  int steps = 0;
  int tripped = 0;
  exec_core(ctx, prog->inst, prog->len,
            loop_map[ctx], if_jump_map[ctx], else_jump_map[ctx],
            stack, &sp, running, 0,
            &steps, run_step_limit_for(ctx), &tripped);
}
