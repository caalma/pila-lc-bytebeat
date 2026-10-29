// -------------------------------------------------------------------------
// parser.c — Tokeniza el codigo fuente RPN (.p), elimina comentarios,
// expande definiciones de funcion (`: nombre ... ;`), rutea el codigo a los
// cinco bloques de contexto ({a}/{a.}/{b}/{b.}/{s}) y produce los cinco
// programas compilados. Es la unica capa que conoce la sintaxis textual del
// lenguaje; el interprete (interpreter.c) solo conoce OpType/Inst/Program.
//
// load_rpn() trabaja en siete fases:
//   0. strip_dead_source()     quita el contenido entre "(" y ")" (anidable)
//                              quita el contenido de bloques "{x ... }" con
//                              marcador no reconocido, ANTES de que las
//                              definiciones de la fase 3 puedan verlo
//   1. tokenize()              texto crudo -> lista de tokens (respeta END_CODE)
//   2. expand_macros()         resuelve `: ;` (funciones), `:=`/`=:` (alias
//                              y variables), sustituye usos por el cuerpo
//                              ya resuelto. Comparten un diccionario
//                              PERSISTENTE entre cargas (ver `dict[]`)
//   2b. fold_constants()       pliega operaciones puras entre literales
//                              numericos ya conocidos, solo las que dan el
//                              mismo resultado en modo entero y float
//   3. route_blocks()          reparte los tokens en app/app.post/buf/
//                              buf.post/sample segun los marcadores
//                              `{a` `{a.` `{b` `{b.` `{s` `}` (sin anidar)
//   4. compile_tokens()        cada lista de tokens -> su Program (lookup
//                              token/is_numeric_token, mas el caso especial
//                              de los tokens sinteticos de variables)
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <math.h>
#include "parser.h"
#include "state.h"
#include "opcodes.h"
#include "diag.h"
#include "strtab.h"
#include "limits.h"
#include "interpreter.h"

// -- Control de script truncado
int load_truncated = 0;

// Reporta un diagnostico tanto por stderr (feedback inmediato en terminal)
// como al buffer de diag.h (para consultar cuantos errores hubo via
// diag_count). Mismos argumentos que fprintf, sin el FILE*.
#define DIAGF(...) do { fprintf(stderr, __VA_ARGS__); diag_add(__VA_ARGS__); } while (0)

// -- Literales numericos con base explicita --------------------------------
// Formato: <digitos>'<base-1>, donde base-1 es el digito maximo permitido
// (un solo caracter). El mapeo de caracteres a valor es:
//   '0'-'9' -> 0..9,  'a'-'z' -> 10..35,  'A'-'Z' -> 36..61
// Ejemplos: 1010110'1 (binario), 1203003'4 (base 5), ff00b1'f (hex),
//           DEADBEEF'Z (base 62). Sin sufijo, decimal estandar.
// El signo negativo va al inicio: -ff'f = -255.
static int digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 36;
  return -1;
}

// Devuelve 1 si `tok` es un literal numerico valido y escribe su valor en
// *out (si out != NULL). Acepta decimal estandar (-?[0-9]+) y la forma
// <digitos>'<base-1>. Un cuerpo con un digito >= base hace que el token
// NO se considere numerico (para no comerse nombres de funciones/alias
// que casualmente tengan un guion bajo seguido de un digito).
static int parse_numeric_token(const char *tok, int *out) {
  if (!tok || !tok[0]) return 0;
  const char *p = tok;
  int negative = 0;
  if (*p == '-') { negative = 1; p++; }
  else if (*p == '+') { p++; }
  if (!*p) return 0;

  const char *us = strchr(p, '\'');
  int base;
  const char *digits_start = p;
  const char *digits_end;

  if (us) {
    // El sufijo debe ser exactamente un caracter despues de "'"
    if (us[1] == '\0' || us[2] != '\0') return 0;
    int sv = digit_value(us[1]);
    if (sv < 1) return 0;   // base minima 2 (sufijo '1 o mayor)
    base = sv + 1;
    digits_end = us;
  } else {
    base = 10;
    digits_end = p + strlen(p);
  }

  if (digits_start == digits_end) return 0;


  // Acumulacion en unsigned: el overflow signed de long long es UB
  // (un literal de 19+ digitos decimales supera LLONG_MAX, y en bases
  // altas alcanza con menos), mientras que el unsigned envuelve mod
  // 2^64 por definicion. Como 2^32 divide a 2^64, los 32 bits bajos
  // conservan el wrap documentado del lenguaje para cualquier largo.
  unsigned long long acc = 0;
  for (const char *q = digits_start; q < digits_end; q++) {
    int dv = digit_value(*q);
    if (dv < 0 || dv >= base) return 0;
    acc = acc * (unsigned long long)base + (unsigned long long)dv;
  }
  if (negative) acc = 0ULL - acc;          // negacion unsigned: definida
  if (out) *out = (int)(int32_t)(uint32_t)acc;
  return 1;
}

// -- Tabla de tokens -> opcode ---------------------------------------------
typedef struct { const char *tok; OpType op; } TokenDef;
static const TokenDef TOKEN_TABLE[] = {
  // pila - operadores
  { "e",    OP_ELIM },
  { "d",    OP_DUP },
  { "nd",   OP_NDUP },
  { "r",    OP_ROT },
  { "nr",   OP_NROT },
  { "cs",   OP_CLEAR_STACK },
  { "ls",   OP_STACK_LEN },
  // pila - informadores (no destructivos)
  { "::",   OP_SHOW_STACK },
  { ".:",   OP_PRINT },
  { "_:",   OP_PRINT_STR },
  // estructuras de control
  { "?",    OP_IF_THEN_ELSE },
  { "?[",   OP_IF_START },
  { "?!",   OP_ELSE_START },
  { "]?",   OP_IF_END },
  { "@[",   OP_LOOP_START },
  { "@!",   OP_LEAVE },
  { "]@",   OP_LOOP_END },
  // estructuras de flujo - getters
  { "i",    OP_LOOP_I },
  { "j",    OP_LOOP_J },
  // aritmética - operadores básicos
  { "+",    OP_ADD },
  { "-",    OP_SUB },
  { "*",    OP_MUL },
  { "/",    OP_DIV },
  { "%",    OP_MOD },
  // aritméticas - funciones para enteros
  { "gsin*",  OP_SIN_DEG_MUL },
  { "gcos*",  OP_COS_DEG_MUL },
  { "gtan*",  OP_TAN_DEG_MUL },
  { "rsin*",  OP_SIN_RAD_MUL },
  { "rcos*",  OP_COS_RAD_MUL },
  { "rtan*",  OP_TAN_RAD_MUL },
  { "hsin*",  OP_SIN_HIP_MUL },
  { "hcos*",  OP_COS_HIP_MUL },
  { "htan*",  OP_TAN_HIP_MUL },
  { "logn*",  OP_LOG_MUL },
  { "exp*",   OP_EXP_MUL },
  { "sqrt*",  OP_SQRT_MUL },
  // aritméticas - funciones para enteros
  { "gsin",   OP_SIN_DEG },
  { "gcos",   OP_COS_DEG },
  { "gtan",   OP_TAN_DEG },
  { "rsin",   OP_SIN_RAD },
  { "rcos",   OP_COS_RAD },
  { "rtan",   OP_TAN_RAD },
  { "hsin",   OP_SIN_HIP },
  { "hcos",   OP_COS_HIP },
  { "htan",   OP_TAN_HIP },
  { "logn",   OP_LOG },
  { "exp",    OP_EXP },
  { "sqrt",   OP_SQRT },
  // artimética - funciones
  { "abs",    OP_ABS },
  { "min",    OP_MIN },
  { "max",    OP_MAX },
  // aritmética/binaria - operadores
  { "&",    OP_AND },
  { "|",    OP_OR },
  { "^",    OP_XOR },
  { "~",    OP_NEGATE },
  { "<<",   OP_LSHIFT },
  { ">>",   OP_RSHIFT },
  // lógica - comparadores
  { "==",   OP_EQUAL },
  { "!=",   OP_NOT_EQUAL },
  { "<",    OP_LESS_THAN },
  { "<=",   OP_LESS_EQUAL_THAN },
  { ">",    OP_GREATER_THAN },
  { ">=",   OP_GREATER_EQUAL_THAN },
  // audio - getters
  { "t",     OP_T },
  { "sr",    OP_SR_GET },
  { "ch",    OP_CH_GET },
  { "buf",   OP_BUF_GET },
  { "vmin",  OP_VMIN_GET },
  { "vmax",  OP_VMAX_GET },
  // audio - setters
  { "T",     OP_TIME_SET },
  { "SR",    OP_SR_SET },
  { "CH",    OP_CH_SET },
  { "BUF",   OP_BUF_SET},
  { "VMIN",  OP_VMIN_SET },
  { "VMAX",  OP_VMAX_SET },
  { "VINT",  OP_VINT_SET },
  // salida - setters
  { "OUT",   OP_OUT },
  { "DCB",   OP_DCB_SET },
  { "SAT",   OP_SAT_SET },
  { "LP",    OP_LP_SET },
  { "MUTE",  OP_MUTE_SET },
  // anomalias - errores - setters
  { "STRICT", OP_STRICT_SET },
  // visualizador de onda - setters
  { "SCOPE",    OP_SCOPE_SET },
  { "SZOOM",    OP_ZOOM_SET },
  { "SCOLOR",   OP_SCOLOR_SET },
  { "SSTROKE",  OP_SSTROKE_SET },
  { "STYPE",    OP_STYPE_SET },
  { "SRES",     OP_SRES_SET },
  { "SFFT",     OP_SFFT_SET },
  { "SWINX",    OP_SWINX_SET },
  { "SWINY",    OP_SWINY_SET },
  { "SWINW",    OP_SWINW_SET },
  { "SWINH",    OP_SWINH_SET },
  { "SFULLSCR", OP_SFULLSCR_SET },
  { "SDECOR",   OP_SDECOR_SET },
  { "SBGCOLOR", OP_SBGCOLOR_SET },
  { "SWINTOP",  OP_SWINTOP_SET },
  // pantalla getters
  { "scrw",   OP_SCRW_GET },
  { "scrh",   OP_SCRH_GET },
  { "winx",   OP_WINX_GET },
  { "winy",   OP_WINY_GET },
  { "winw",   OP_WINW_GET },
  { "winh",   OP_WINH_GET },
  // delay - setters
  { "DELAY",  OP_DELAY_SET },
  { "DTIME",  OP_DTIME_SET },
  { "DFEED",  OP_DFEED_SET },
  { "DMIX",   OP_DMIX_SET },
  { "DFLO",   OP_DFLO_SET },
  { "DFHI",   OP_DFHI_SET },
  // samples - operadores
  { "smpload",    OP_SMP_LOAD },
  { "smpremove",  OP_SMP_REMOVE },
  { "smpreset",   OP_SMP_RESET },
  { "smploop",    OP_SMP_LOOP },
  { "smpgain",    OP_SMP_GAIN },
  { "smpmix",     OP_SMP_MIX },
  { "smpreverse", OP_SMP_REVERSE },
  // samples - getters
  { "sample",      OP_SMP_GET },
  { "samplepitch", OP_SMP_PITCH_GET },
  { "smplen",      OP_SMP_LEN },
  { "smplive",     OP_SMP_LIVE },
  // setters - samples
  { "SMPFADE",  OP_SMP_FADE_SET },
  // generadores - osciladores
  { "sine",     OP_SINE },
  { "triangle", OP_TRIANGLE },
  { "saw",      OP_SAW },
  { "sawpb",    OP_SAW_PB },
  { "square",   OP_SQUARE },
  { "squarepb", OP_SQUARE_PB },
  { "noise",    OP_NOISE },
  // grabación - operadores
  { "recfile",  OP_RECFILE },
  { "recstart", OP_RECSTART },
  { "recstop",  OP_RECSTOP },
  // aplicación - controles - setters
  { "bye",        OP_BYE },
  { "reload",     OP_RELOAD },
  { "default",    OP_DEFAULT },
  { "showlimits", OP_SHOW_LIMITS },
  { "anomlog",    OP_ANOM_LOG },
  { "BCASC",      OP_BCASC_SET },
  // aplicación - controles - getters
  { "bcasc",      OP_BCASC_GET },

};

#define TOKEN_TABLE_LEN (sizeof(TOKEN_TABLE) / sizeof(TOKEN_TABLE[0]))

static int lookup_token(const char *tok, OpType *op) {
  for (size_t i = 0; i < TOKEN_TABLE_LEN; i++) {
    if (strcmp(tok, TOKEN_TABLE[i].tok) == 0) {
      *op = TOKEN_TABLE[i].op;
      return 1;
    }
  }
  return 0;
}

static int is_numeric_token(const char *tok) {
  return parse_numeric_token(tok, NULL);
}

// -- Fase 0: preprocesado a nivel de caracter ------------------------------
// Elimina comentarios "( ... )" (anidables) y bloques apagados "{x ... }"
// (marcador no reconocido) ANTES de tokenizar. Asi ningun string literal
// que aparezca adentro llega a internarse en strtab (que era lo que corria
// los indices de los strings reales del programa).
//
// Maquina de estados:
//   NORMAL:  '"' abre STRING (se copia); '(' abre COMMENT (depth 1, se
//            descarta); '{' con marcador reconocido se copia y sigue en
//            NORMAL; '{' con marcador no reconocido abre IGNORE.
//   STRING:  copia todo hasta el '"' de cierre; parentesis y llaves adentro
//            son texto del string, no delimitadores.
//   COMMENT: descarta todo; '(' anida, ')' desanida y con depth 0 vuelve a
//            NORMAL. Las comillas adentro NO abren string: un comentario
//            con comillas desbalanceadas no rompe el cierre.
//   IGNORE:  descarta todo hasta el primer '}' que no este dentro de un
//            string; ahi vuelve a NORMAL.
//   IGNSTR:  string dentro de bloque apagado: se descarta hasta su cierre.

#define MAX_TOKEN_LEN     32
#define MAX_RAW_TOKENS  8192

typedef char Token[MAX_TOKEN_LEN];

static int is_known_block_name(const char *m) {
  return strcmp(m, "a") == 0 || strcmp(m, "b") == 0 || strcmp(m, "s") == 0 ||
    strcmp(m, "a.") == 0 || strcmp(m, "b.") == 0;
}

static void strip_dead_source(const char *src, char *out, size_t out_max) {
  enum { ST_NORMAL, ST_STRING, ST_COMMENT, ST_IGNORE, ST_IGNSTR };
  int st = ST_NORMAL;
  int depth = 0;
  size_t o = 0;
  size_t i = 0;
  while (src[i]) {
    char c = src[i];
    switch (st) {
    case ST_STRING:
      if (o + 1 < out_max) out[o++] = c;
      if (c == '"') st = ST_NORMAL;
      i++;
      break;
    case ST_COMMENT:
      if (c == '(') depth++;
      else if (c == ')') { depth--; if (depth == 0) st = ST_NORMAL; }
      i++;
      break;
    case ST_IGNORE:
      if (c == '"') st = ST_IGNSTR;
      else if (c == '}') st = ST_NORMAL;
      i++;
      break;
    case ST_IGNSTR:
      if (c == '"') st = ST_IGNORE;
      i++;
      break;
    default: /* ST_NORMAL */
      if (c == '"') {
        st = ST_STRING;
        if (o + 1 < out_max) out[o++] = c;
        i++;
      } else if (c == '(') {
        st = ST_COMMENT;
        depth = 1;
        i++;
      } else if (c == '{') {
        // Leer el marcador hasta whitespace o '}'
        size_t q = i + 1;
        while (src[q] && !isspace((unsigned char)src[q]) && src[q] != '}') q++;
        size_t mlen = q - (i + 1);
        char marker[MAX_TOKEN_LEN];
        if (mlen >= sizeof(marker)) mlen = sizeof(marker) - 1;
        memcpy(marker, src + i + 1, mlen);
        marker[mlen] = '\0';
        if (is_known_block_name(marker)) {
          for (size_t r = i; r < q; r++) if (o + 1 < out_max) out[o++] = src[r];
          i = q;
        } else {
          st = ST_IGNORE;
          i = q;
        }
      } else {
        if (o + 1 < out_max) out[o++] = c;
        i++;
      }
      break;
    }
  }
  if (depth > 0) DIAGF("    Aviso | %d comentario(s) '(' sin ')' correspondiente\n", depth);
  if (st == ST_IGNORE || st == ST_IGNSTR)

    DIAGF("    Aviso | un bloque apagado ('{x ... }') quedo sin '}' de cierre\n");
  out[o] = '\0';
}

// -- Fase 1: tokenizacion ------------------------------------------------
// Reparte `src` en tokens separados por whitespace en `out`, hasta
// MAX_RAW_TOKENS o hasta encontrar END_CODE ("..."). Los strings literales
// "texto con espacios" se reconocen como una unidad: se internan en la
// tabla de strings y el token resultante es el indice en decimal.
static int tokenize(const char *src, Token *out, int max_tokens) {
  int n = 0;
  const char *p = src;
  while (*p && n < max_tokens) {
    while (isspace((unsigned char)*p)) p++;
    if (!*p) break;
    // String literal: "texto"
    if (*p == '"') {
      p++; // saltar la comilla de apertura
      const char *start = p;
      while (*p && *p != '"') p++;
      size_t str_len = (size_t)(p - start);
      if (!*p) {
        DIAGF("    Aviso | string sin '\"' de cierre\n");
      } else {
        p++; // saltar la comilla de cierre
      }
      if (str_len >= MAX_STRING_LEN) {
        DIAGF("    Aviso | string truncado (limite %d chars)\n", MAX_STRING_LEN - 1);
        str_len = MAX_STRING_LEN - 1;
      }
      char buf[MAX_STRING_LEN];
      memcpy(buf, start, str_len);
      buf[str_len] = '\0';
      int idx = strtab_intern(buf);
      if (idx >= 0) {
        snprintf(out[n], MAX_TOKEN_LEN, "%d", idx);
        n++;
      }
      continue;
    }
    const char *start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);
    if (len >= MAX_TOKEN_LEN) {
      DIAGF("    Aviso | Token demasiado largo (%zu chars), omitido\n", len);
      continue;
    }
    memcpy(out[n], start, len);
    out[n][len] = '\0';
    if (strcmp(out[n], END_CODE) == 0) break;
    n++;
  }
  return n;
}


// -- Fase 2: diccionario persistente — funciones (`: nombre ... ;`) y alias
// (`nombre := destino` / `origen =: nombre`) ----------------------------
// Constantes internas de implementación (no son parte de la API del lenguaje).
#define MAX_FUNC_BODY_TOKENS  256
#define MAX_EXPANDED_TOKENS  4096

typedef enum { DICT_FUNC, DICT_ALIAS, DICT_VAR } DictKind;
typedef struct {
    char     name[MAX_TOKEN_LEN];
    Token    body[MAX_FUNC_BODY_TOKENS];
    int      body_len;
    DictKind kind;
    int      var_slot;
    // Codigo compilado del cuerpo (funciones/alias) para OP_CALL (,_):
    // se reconstruye en cada (re)definicion, asi el runtime ejecuta
    // siempre la version vigente sin caches que invalidar.
    Inst    *code;
    int      code_len;
    int     *cm_lm, *cm_ijm, *cm_ejm;
} DictEntry;

static DictEntry dict[MAX_WORDS];
static int       dict_count;    // persiste entre cargas
static int       next_var_slot; // idem

// Contadores del diccionario persistente, para `showlimits`.
int parser_dict_count(void)     { return dict_count; }
int parser_var_slots_used(void) { return next_var_slot; }

static void entry_compile(DictEntry *e);   // definida despues de compile_tokens
static void entry_free_code(DictEntry *e) {
    free(e->code);    e->code    = NULL;
    free(e->cm_lm);   e->cm_lm   = NULL;
    free(e->cm_ijm);  e->cm_ijm  = NULL;
    free(e->cm_ejm);  e->cm_ejm  = NULL;
    e->code_len = 0;
}



// -- BCASC: redefiniciones vistas durante esta carga -----------------------
// Una funcion/alias redefinida con bcasc_mode activo no alcanza a los
// bloques retenidos de cargas previas (inlinearon el cuerpo viejo en su
// propia compilacion). Los nombres se juntan aca y se avisan despues de
// route_blocks, que es cuando se sabe si algo quedo retenido.
#define BCASC_MAX_REDEFS 8
static char bcasc_redefs[BCASC_MAX_REDEFS][MAX_TOKEN_LEN];
static int  bcasc_n_redefs = 0;

#define SYNTH_PREFIX '\x01'
#define SYNTH_STORE  'S'
#define SYNTH_LOAD   'L'

static void make_synthetic(Token out, char op, int slot) {
  snprintf(out, MAX_TOKEN_LEN, "%c%c%d", SYNTH_PREFIX, op, slot);
}

#define SYNTH_CALL   'C'
static void make_synth_call(Token out, const char *name) {
    snprintf(out, MAX_TOKEN_LEN, "%c%c%s", SYNTH_PREFIX, SYNTH_CALL, name);
}

static int find_entry(const char *name) {
  for (int i = 0; i < dict_count; i++)
    if (strcmp(dict[i].name, name) == 0) return i;
  return -1;
}

static int is_reserved_name(const char *name) {
    OpType dummy;
    return lookup_token(name, &dummy) ||
      is_numeric_token(name) ||
      strcmp(name, ",_") == 0;
}

static const char *kind_name(DictKind k) {
  switch (k) {
  case DICT_FUNC:  return "funcion";
  case DICT_VAR:   return "variable";
  default:         return "alias";
  }
}

static int dict_register(DictEntry *entry) {
  if (is_reserved_name(entry->name)) {
    DIAGF("    Aviso | No se puede definir '%s': ya es una palabra del lenguaje\n", entry->name);
    return 0;
  }
  int existing = find_entry(entry->name);
  if (existing >= 0) {
    if (dict[existing].kind != entry->kind) {
      DIAGF("    Aviso | '%s' ya existe como %s, no se puede redefinir como %s\n",
            entry->name, kind_name(dict[existing].kind), kind_name(entry->kind));
      return 0;
    }
    if (bcasc_mode && bcasc_n_redefs < BCASC_MAX_REDEFS) {
      strncpy(bcasc_redefs[bcasc_n_redefs], entry->name, MAX_TOKEN_LEN - 1);
      bcasc_redefs[bcasc_n_redefs][MAX_TOKEN_LEN - 1] = '\0';
      bcasc_n_redefs++;
    }
    entry_free_code(&dict[existing]);   // antes de pisar la struct
    dict[existing] = *entry;
    entry_compile(&dict[existing]);
    return 1;
  }
  if (dict_count >= MAX_WORDS) {
    DIAGF("    Aviso | Limite de %d palabras (funciones+alias+variables) alcanzado, '%s' se ignora\n", MAX_WORDS, entry->name);
    return 0;
  }
  dict[dict_count] = *entry;
  entry_compile(&dict[dict_count]);       // despues de insertar: la
  dict_count++;                           // autoreferencia ya resuelve
  return 1;
}

static void resolve_token_body(const char *token, Token *dst, int *dst_len, int max_len) {
  int fi = find_entry(token);
  if (fi >= 0) {
    for (int k = 0; k < dict[fi].body_len && *dst_len < max_len; k++)
      memcpy(dst[(*dst_len)++], dict[fi].body[k], MAX_TOKEN_LEN);
  } else if (*dst_len < max_len) {
    memcpy(dst[(*dst_len)++], token, MAX_TOKEN_LEN);
  }
}

static int expand_macros(Token *raw, int raw_len, Token *out, int max_out) {
  int out_len = 0;
  int i = 0;
  while (i < raw_len) {
    // Cita: ",_ nombre" — no inlinear: emitir token sintetico que
    // compile_tokens resuelve a OP_CALL (llamada late-binding).
    if (strcmp(raw[i], ",_") == 0) {
      if (i + 1 >= raw_len) { DIAGF("    Aviso | ,_ sin nombre\n"); i++; continue; }
      if (out_len >= max_out) {
        DIAGF("    Aviso | Expansion de macros truncada (limite %d tokens)\n", max_out);
        break;
      }
      make_synth_call(out[out_len++], raw[i + 1]);
      i += 2;
      continue;
    }

    // alias por la derecha: "nombre := destino"
    if (i + 1 < raw_len && strcmp(raw[i + 1], ":=") == 0) {
      if (strcmp(raw[i + 2], ",_") == 0) {
        DIAGF("    Aviso | ':=' no admite ,_ como destino\n");
        i += 3;
        continue;
      }
      if (i + 2 >= raw_len) { DIAGF("    Aviso | ':=' sin destino\n"); i += 2; continue; }
      DictEntry e = {0};
      e.kind = DICT_ALIAS;
      strncpy(e.name, raw[i], MAX_TOKEN_LEN - 1);
      resolve_token_body(raw[i + 2], e.body, &e.body_len, MAX_FUNC_BODY_TOKENS);
      dict_register(&e);
      i += 3;
      continue;
    }
    // variable en runtime: "... =: nombre"
    if (strcmp(raw[i], "=:") == 0) {
      if (i + 1 >= raw_len) { DIAGF("    Aviso | '=:' sin nombre\n"); i++; continue; }
      const char *name = raw[i + 1];
      int existing = find_entry(name);
      int slot;
      if (existing >= 0 && dict[existing].kind == DICT_VAR) {
        slot = dict[existing].var_slot;
      } else if (existing >= 0) {
        DIAGF("    Aviso | '%s' ya existe como %s, no se puede redefinir como variable\n",
              name, kind_name(dict[existing].kind));
        i += 2;
        continue;
      } else if (is_reserved_name(name)) {
        DIAGF("    Aviso | No se puede definir '%s': ya es una palabra del lenguaje\n", name);
        i += 2;
        continue;
      } else if (next_var_slot >= MAX_WORDS) {
        DIAGF("    Aviso | Limite de %d variables alcanzado, '%s' se ignora\n", MAX_WORDS, name);
        i += 2;
        continue;
      } else if (dict_count >= MAX_WORDS) {
        DIAGF("    Aviso | Limite de %d palabras (funciones+alias+variables) alcanzado, '%s' se ignora\n", MAX_WORDS, name);
        i += 2;
        continue;
      } else {
        slot = next_var_slot++;
      }
      DictEntry e = {0};
      e.kind = DICT_VAR;
      e.var_slot = slot;
      strncpy(e.name, name, MAX_TOKEN_LEN - 1);
      make_synthetic(e.body[0], SYNTH_LOAD, slot);
      e.body_len = 1;
      if (existing >= 0) dict[existing] = e; else dict[dict_count++] = e;
      if (out_len >= max_out) {
        DIAGF("    Aviso | Expansion de macros truncada (limite %d tokens)\n", max_out);
        break;
      }
      make_synthetic(out[out_len++], SYNTH_STORE, slot);
      i += 2;
      continue;
    }
    if (strcmp(raw[i], ":") == 0) {
      i++;
      if (i >= raw_len) { DIAGF("    Aviso | ':' sin nombre de funcion\n"); break; }
      DictEntry e = {0};
      e.kind = DICT_FUNC;
      strncpy(e.name, raw[i], MAX_TOKEN_LEN - 1);
      i++;
      while (i < raw_len && strcmp(raw[i], ";") != 0) {
        if (strcmp(raw[i], ",_") == 0 && i + 1 < raw_len) {
          if (e.body_len < MAX_FUNC_BODY_TOKENS)
            make_synth_call(e.body[e.body_len++], raw[i + 1]);
          else
            DIAGF("    Aviso | Cuerpo de funcion '%s' truncado (max %d tokens)\n", e.name, MAX_FUNC_BODY_TOKENS);
          i += 2;
          continue;
        }
        if (e.body_len < MAX_FUNC_BODY_TOKENS) {
          resolve_token_body(raw[i], e.body, &e.body_len, MAX_FUNC_BODY_TOKENS);
        } else {
          DIAGF("    Aviso | Cuerpo de funcion '%s' truncado (max %d tokens)\n", e.name, MAX_FUNC_BODY_TOKENS);
        }
        i++;
      }
      if (i < raw_len) i++; // consumir el ';'
      else DIAGF("    Aviso | Funcion '%s' sin ';' de cierre\n", e.name);
      dict_register(&e);
      continue;
    }
    if (strcmp(raw[i], ";") == 0) {
      DIAGF("    Aviso | ';' sin ':' de apertura, omitido\n");
      i++;
      continue;
    }
    int fi = find_entry(raw[i]);
    if (fi >= 0) {
      if (out_len + dict[fi].body_len > max_out) {
        DIAGF("    Aviso | Expansion de macros truncada (limite %d tokens)\n", max_out);
        break;
      }
      for (int k = 0; k < dict[fi].body_len; k++)
        memcpy(out[out_len++], dict[fi].body[k], MAX_TOKEN_LEN);
    } else {
      if (out_len >= max_out) {
        DIAGF("    Aviso | Expansion de macros truncada (limite %d tokens)\n", max_out);
        break;
      }
      memcpy(out[out_len++], raw[i], MAX_TOKEN_LEN);
    }
    i++;
  }
  return out_len;
}

// -- Fase 2b: plegado de constantes --------------------------------------
// Plegado conservador: solo pliega operaciones cuando el resultado entero
// (con wraparound a 32 bits) y el resultado float (sin wraparound) coinciden.
// Esto preserva el modo float: operaciones como 2000000000 2 * no se pliegan
// porque el resultado int (-294967296) difiere del float (4e9). El runtime
// hace la operación según vint_mode, así que el comportamiento es correcto
// en ambos modos aunque no haya optimización en parse-time.

static int fold_binary(OpType op, int a, int b, int *out) {
  switch (op) {
  case OP_ADD: {
    int64_t ri = (int64_t)a + (int64_t)b;
    int32_t r32 = (int32_t)(uint32_t)ri;
    double rf = (double)a + (double)b;
    if ((double)r32 != rf) return 0;
    *out = r32; return 1;
  }
  case OP_SUB: {
    int64_t ri = (int64_t)a - (int64_t)b;
    int32_t r32 = (int32_t)(uint32_t)ri;
    double rf = (double)a - (double)b;
    if ((double)r32 != rf) return 0;
    *out = r32; return 1;
  }
  case OP_MUL: {
    int64_t ri = (int64_t)a * (int64_t)b;
    int32_t r32 = (int32_t)(uint32_t)ri;
    double rf = (double)a * (double)b;
    if ((double)r32 != rf) return 0;
    *out = r32; return 1;
  }
  case OP_DIV:
    if (b == 0) return 0;
    if (b == -1 && a == INT32_MIN) return 0;
    {
      int32_t r32 = a / b;
      double rf = (double)a / (double)b;
      if ((double)r32 != rf) return 0;
      *out = r32; return 1;
    }
  case OP_MOD:
    if (b == 0) return 0;
    if (b == -1 && a == INT32_MIN) { *out = 0; return 1; }
    {
      int32_t r32 = a % b;
      double rf = fmod((double)a, (double)b);
      if ((double)r32 != rf) return 0;
      *out = r32; return 1;
    }
  case OP_AND:
  case OP_OR:
  case OP_XOR:
    // Bitwise siempre es entero; el resultado es el mismo en ambos modos
    switch (op) {
    case OP_AND: *out = a & b; break;
    case OP_OR:  *out = a | b; break;
    case OP_XOR: *out = a ^ b; break;
    default: break;
    }
    return 1;

  case OP_LSHIFT: {
    // vint: cuenta & 31 (JS). float: clamp 0..63, >= 32 da 0.
    // Solo se pliega si ambas semanticas coinciden (cuentas 0..31,
    // o casos degenerados como operando 0).
    int bv = b & 31;
    int32_t rv = (int32_t)((uint32_t)a << bv);
    int bf = b; if (bf < 0) bf = 0; if (bf > 63) bf = 63;
    int32_t rf = (bf >= 32) ? 0 : (int32_t)((uint32_t)a << bf);
    if (rv != rf) return 0;
    *out = rv; return 1;
  }

    /*
  case OP_LSHIFT:
    if (b < 0 || b > 63) return 0;
    if (b >= 32) { *out = 0; return 1; }
    *out = (int)((uint32_t)a << b);
    return 1;
    */

  case OP_RSHIFT: {
    int bv = b & 31;
    int32_t rv = a >> bv;
    int bf = b; if (bf < 0) bf = 0; if (bf > 63) bf = 63;
    int32_t rf = (bf >= 32) ? ((a < 0) ? -1 : 0) : (a >> bf);
    if (rv != rf) return 0;
    *out = rv; return 1;
  }

    /*
  case OP_RSHIFT:
    if (b < 0 || b > 63) return 0;
    if (b >= 32) { *out = (a < 0) ? -1 : 0; return 1; }
    *out = a >> b;
    return 1;
    */

  case OP_EQUAL:
  case OP_NOT_EQUAL:
  case OP_LESS_THAN:
  case OP_LESS_EQUAL_THAN:
  case OP_GREATER_THAN:
  case OP_GREATER_EQUAL_THAN:
    // Comparaciones: el resultado es -1 o 0, igual en ambos modos
    switch (op) {
    case OP_EQUAL:             *out = (a == b) ? -1 : 0; break;
    case OP_NOT_EQUAL:         *out = (a != b) ? -1 : 0; break;
    case OP_LESS_THAN:         *out = (a <  b) ? -1 : 0; break;
    case OP_LESS_EQUAL_THAN:   *out = (a <= b) ? -1 : 0; break;
    case OP_GREATER_THAN:      *out = (a >  b) ? -1 : 0; break;
    case OP_GREATER_EQUAL_THAN:*out = (a >= b) ? -1 : 0; break;
    default: break;
    }
    return 1;
  default:
    return 0;
  }
}

static int fold_unary(OpType op, int a, int *out) {
  switch (op) {
  case OP_NEGATE:
    // Bitwise NOT: siempre entero, mismo resultado en ambos modos
    *out = ~a;
    return 1;
  case OP_ABS: {
    // abs(INT32_MIN) en int64 da 2147483648, que como int32 es INT32_MIN
    // (negativo). En float, fabs(-2147483648.0) = 2147483648.0 (positivo).
    // Difieren: no plegar.
    int64_t ri = llabs((int64_t)a);
    int32_t r32 = (int32_t)(uint32_t)ri;
    double rf = fabs((double)a);
    if ((double)r32 != rf) return 0;
    *out = r32;
    return 1;
  }
  default:
    return 0;
  }
}

static int fold_constants(Token *in, int n_in, Token *out, int max_out) {
  int out_len = 0;
  for (int i = 0; i < n_in; i++) {
    if (out_len >= max_out) {
      DIAGF("    Aviso | código truncado plegando constantes (limite %d tokens)\n", max_out);
      break;
    }
    memcpy(out[out_len++], in[i], MAX_TOKEN_LEN);
    int folded;
    do {
      folded = 0;
      OpType op;
      if (out_len >= 3 &&
          is_numeric_token(out[out_len - 3]) &&
          is_numeric_token(out[out_len - 2]) &&
          lookup_token(out[out_len - 1], &op)) {
        int a, b, result;
        parse_numeric_token(out[out_len - 3], &a);
        parse_numeric_token(out[out_len - 2], &b);
        if (fold_binary(op, a, b, &result)) {
          snprintf(out[out_len - 3], MAX_TOKEN_LEN, "%d", result);
          out_len -= 2;
          folded = 1;
          continue;
        }
      }
      if (out_len >= 2 &&
          is_numeric_token(out[out_len - 2]) &&
          lookup_token(out[out_len - 1], &op)) {
        int a, result;
        parse_numeric_token(out[out_len - 2], &a);
        if (fold_unary(op, a, &result)) {
          snprintf(out[out_len - 2], MAX_TOKEN_LEN, "%d", result);
          out_len -= 1;
          folded = 1;
        }
      }
    } while (folded);
  }
  return out_len;
}

// -- Fase 3: ruteo de bloques {a} {a.} {b} {b.} {s} ----------------------
typedef enum { BLK_SAMPLE, BLK_APP, BLK_APP_POST, BLK_BUF, BLK_BUF_POST, BLK_IGNORE } BlockState;


static int route_blocks(Token *in, int n_in,
                        Token *app, int max_app, int *n_app,
                        Token *app_post, int max_app_post, int *n_app_post,
                        Token *buf, int max_buf, int *n_buf,
                        Token *buf_post, int max_buf_post, int *n_buf_post,
                        Token *sample, int max_sample, int *n_sample,
                        int *defined) {
    BlockState state = BLK_SAMPLE;
    *n_app = *n_app_post = *n_buf = *n_buf_post = *n_sample = 0;
    for (int i = 0; i < 5; i++) defined[i] = 0;  // solo inicializacion de flags
    for (int i = 0; i < n_in; i++) {             // el recorrido real de tokens
        const char *t = in[i];
        if (state == BLK_IGNORE) {
            if (strcmp(t, "}") == 0) state = BLK_SAMPLE;
            continue;
        }
        if (state == BLK_SAMPLE) {
            if (strcmp(t, "{a") == 0)  { state = BLK_APP;      defined[0] = 1; continue; }
            if (strcmp(t, "{a.") == 0) { state = BLK_APP_POST; defined[1] = 1; continue; }
            if (strcmp(t, "{b") == 0)  { state = BLK_BUF;      defined[2] = 1; continue; }
            if (strcmp(t, "{b.") == 0) { state = BLK_BUF_POST; defined[3] = 1; continue; }
            if (strcmp(t, "{s") == 0)  { defined[4] = 1; continue; }
            if (t[0] == '{')           { state = BLK_IGNORE; continue; }
            if (strcmp(t, "}") == 0)   { continue; }
        } else if (strcmp(t, "}") == 0) {
            state = BLK_SAMPLE;
            continue;
        }
        Token *bucket; int *n; int max;
        switch (state) {
        case BLK_APP:      bucket = app;      n = n_app;      max = max_app;      break;
        case BLK_APP_POST: bucket = app_post; n = n_app_post; max = max_app_post; break;
        case BLK_BUF:      bucket = buf;      n = n_buf;      max = max_buf;      break;
        case BLK_BUF_POST: bucket = buf_post; n = n_buf_post; max = max_buf_post; break;
        default:           bucket = sample;   n = n_sample;   max = max_sample;   break;
        }
        if (state == BLK_SAMPLE) defined[4] = 1;   // codigo suelto define {s}
        if (*n >= max) {
            DIAGF("    Aviso | bloque truncado (limite %d tokens)\n", max);
            continue;
        }
        memcpy(bucket[*n], t, MAX_TOKEN_LEN);
        (*n)++;
    }
    return 0;
}

// -- Anclas #nombre y jumps ->nombre -------------------------------------
typedef struct {
  char name[MAX_TOKEN_LEN];
  int  id;
  int  declared;
  int  declared_pc;
} AnchorEntry;

static int find_or_add_anchor(AnchorEntry *anchors, int *n_anchors, const char *name) {
  for (int i = 0; i < *n_anchors; i++)
    if (strcmp(anchors[i].name, name) == 0) return i;
  int i = *n_anchors;
  if (i >= MAX_ANCHORS) return -1;
  strncpy(anchors[i].name, name, MAX_TOKEN_LEN - 1);
  anchors[i].name[MAX_TOKEN_LEN - 1] = '\0';
  anchors[i].id = i;
  anchors[i].declared = 0;
  anchors[i].declared_pc = -1;
  (*n_anchors)++;
  return i;
}

// -- Fase 4: compilacion a Program ---------------------------------------
static void compile_tokens(Token *tokens, int n_tokens, Program *prog) {
  prog->len = 0;
  AnchorEntry anchors[MAX_ANCHORS];
  int n_anchors = 0;
  int depth = 0;
  for (int i = 0; i < n_tokens && prog->len < MAX_INST; i++) {
    const char *tok = tokens[i];
    Inst *inst = &prog->inst[prog->len];
    OpType op;

    if (tok[0] == SYNTH_PREFIX) {
      if (tok[1] == SYNTH_CALL) {
        // Cita ,_ nombre: se resuelve ACA, con el diccionario
        // completo (por eso funcionan las referencias hacia
        // adelante y la recursion).
        const char *name = tok + 2;
        int fi = find_entry(name);
        if (fi < 0) {
          DIAGF("    Aviso | ,_ %s: sin definicion (omitido)\n", name);
          continue;
        }
        if (dict[fi].kind == DICT_VAR) {
          DIAGF("    Aviso | ,_ %s: citar una variable no tiene sentido (ya es dinamica); usala directa\n", name);
          continue;
        }
        inst->op  = OP_CALL;
        inst->val = fi;
      } else {
        inst->op  = (tok[1] == SYNTH_STORE) ? OP_VAR_STORE : OP_VAR_LOAD;
        inst->val = atoi(tok + 2);
      }
    } else if (tok[0] == '#' && tok[1] != '\0') {
      const char *name = tok + 1;
      if (depth > 0) {
        DIAGF("    Aviso | No se puede declarar el ancla '#%s' dentro de un @[ ]@ o ?[ ]?\n", name);
        continue;
      }
      int ai = find_or_add_anchor(anchors, &n_anchors, name);
      if (ai < 0) { DIAGF("    Aviso | Limite de %d anclas alcanzado, '#%s' se ignora\n", MAX_ANCHORS, name); continue; }
      if (anchors[ai].declared) {
        DIAGF("    Aviso | Ancla '#%s' repetida, se ignora (queda la primera)\n", name);
        continue;
      }
      anchors[ai].declared    = 1;
      anchors[ai].declared_pc = prog->len;
      inst->op  = OP_ANCHOR;
      inst->val = anchors[ai].id;
    } else if (tok[0] == '-' && tok[1] == '>' && tok[2] != '\0') {
      const char *name = tok + 2;
      if (depth > 0) {
        DIAGF("    Aviso | No se puede usar '->%s' dentro de un @[ ]@ o ?[ ]?\n", name);
        continue;
      }
      int ai = find_or_add_anchor(anchors, &n_anchors, name);
      if (ai < 0) { DIAGF("    Aviso | Limite de %d anclas alcanzado, '->%s' se ignora\n", MAX_ANCHORS, name); continue; }
      inst->op  = OP_JUMP;
      inst->val = anchors[ai].id;
    } else if (lookup_token(tok, &op)) {
      inst->op = op;
      if (op == OP_LOOP_START || op == OP_IF_START) depth++;
      else if (op == OP_LOOP_END || op == OP_IF_END) { if (depth > 0) depth--; }
    } else if (is_numeric_token(tok)) {
      int val;
      parse_numeric_token(tok, &val);
      inst->op  = OP_PUSH;
      inst->val = val;
    } else {
      DIAGF("    Aviso | Token desconocido: %s (omitido)\n", tok);
      continue;
    }
    prog->len++;
  }
  // Segunda pasada: resolver cada OP_JUMP al pc real de su ancla.
  for (int k = 0; k < prog->len; k++) {
    if (prog->inst[k].op != OP_JUMP) continue;
    int id = prog->inst[k].val;
    int target = -1;
    for (int a = 0; a < n_anchors; a++) {
      if (anchors[a].id == id && anchors[a].declared) { target = anchors[a].declared_pc; break; }
    }
    if (target < 0) {
      const char *name = "?";
      for (int a = 0; a < n_anchors; a++) if (anchors[a].id == id) { name = anchors[a].name; break; }
      DIAGF("    Aviso | '->%s' sin ancla '#%s' correspondiente\n", name, name);
    }
    prog->inst[k].val = target;
  }

  prog->n_anchors = n_anchors;
}

// -- Compilacion de cuerpos del diccionario (para OP_CALL / ,_) ----------
static void entry_compile(DictEntry *e) {
    entry_free_code(e);
    Program tmp;                      // en pila: MAX_INST insts
    compile_tokens(e->body, e->body_len, &tmp);
    if (tmp.len <= 0) return;         // cuerpo vacio: llamada no-op
    e->code = (Inst *)malloc(sizeof(Inst) * (size_t)tmp.len);
    if (!e->code) { e->code_len = 0; return; }
    memcpy(e->code, tmp.inst, sizeof(Inst) * (size_t)tmp.len);
    e->code_len = tmp.len;
    e->cm_lm  = (int *)malloc(sizeof(int) * (size_t)tmp.len);
    e->cm_ijm = (int *)malloc(sizeof(int) * (size_t)tmp.len);
    e->cm_ejm = (int *)malloc(sizeof(int) * (size_t)tmp.len);
    if (!e->cm_lm || !e->cm_ijm || !e->cm_ejm) { entry_free_code(e); return; }
    int ul = 0, ui = 0;
    interpreter_build_maps(e->code, e->code_len,
                           e->cm_lm, e->cm_ijm, e->cm_ejm, &ul, &ui);
    if (ul > 0) DIAGF("    Aviso | funcion '%s': %d loop(s) @[ sin ]@\n", e->name, ul);
    if (ui > 0) DIAGF("    Aviso | funcion '%s': %d if(s) ?[ sin ]?\n", e->name, ui);
}

// Acceso del interprete al codigo compilado de una entrada (OP_CALL).
// Un cuerpo vacio es valido: len 0 con mapas NULL = llamada no-op.
int dict_entry_code(int slot, const Inst **code, int *len,
                    int **lm, int **ijm, int **ejm) {
    if (slot < 0 || slot >= dict_count) return 0;
    DictEntry *e = &dict[slot];
    if (e->kind == DICT_VAR) return 0;
    if (e->code_len > 0 && (!e->cm_lm || !e->cm_ijm || !e->cm_ejm)) return 0;
    *code = e->code;
    *len  = e->code_len;
    *lm   = e->cm_lm;
    *ijm  = e->cm_ijm;
    *ejm  = e->cm_ejm;
    return 1;
}

// -- Buffers de trabajo de load_rpn --------------------------------------
static Token raw_tokens[MAX_RAW_TOKENS];
static Token expanded_tokens[MAX_EXPANDED_TOKENS];
static Token folded_tokens[MAX_EXPANDED_TOKENS];
static Token app_tokens[MAX_EXPANDED_TOKENS];
static Token app_post_tokens[MAX_EXPANDED_TOKENS];
static Token buf_tokens[MAX_EXPANDED_TOKENS];
static Token buf_post_tokens[MAX_EXPANDED_TOKENS];
static Token sample_tokens[MAX_EXPANDED_TOKENS];
static char preprocessed[RPN_BUF_SIZE];

void load_rpn(const char *path) {
  diag_reset();
  bcasc_n_redefs = 0;
  FILE *f = fopen(path, "r");
  if (!f) {
    DIAGF("    ERROR | Abriendo %s: %s\n", path, strerror(errno));
    return;
  }

  size_t len = fread(rpn_source, 1, RPN_BUF_SIZE - 1, f);
  rpn_source[len] = '\0';
  // Deteccion de truncamiento: un byte extra ANTES de fclose dice si
  // quedaba contenido sin leer. (feof despues de fclose es UB: el FILE
  // ya fue liberado y sus flags quedan a merced del allocator.)
  int hay_mas = (fgetc(f) != EOF);
  fclose(f);
  load_truncated = 0;
  if (hay_mas) {
    load_truncated = 1;
    DIAGF("    ERROR | Archivo '%s' excede el límite de %d bytes; se truncó la lectura\n", path, RPN_BUF_SIZE - 1);
    if (checkscript_mode) {
      DIAGF("    Aviso | Modo checkscript activo: abortando por truncamiento\n");
      // No continuamos con el parseo; el archivo está incompleto
      return;   // en modo check, ni compila
    }
  }


  if (len == 0) { DIAGF("    Aviso | Archivo vacio.\n"); return; }

  strip_dead_source(rpn_source, preprocessed, sizeof(preprocessed));
  int n_raw = tokenize(preprocessed, raw_tokens, MAX_RAW_TOKENS);
  int n_exp = expand_macros(raw_tokens, n_raw, expanded_tokens, MAX_EXPANDED_TOKENS);
  int n_fold = fold_constants(expanded_tokens, n_exp, folded_tokens, MAX_EXPANDED_TOKENS);

  int n_app, n_app_post, n_buf, n_buf_post, n_sample;
  int defined[5];
  route_blocks(folded_tokens, n_fold,
               app_tokens,      MAX_EXPANDED_TOKENS, &n_app,
               app_post_tokens, MAX_EXPANDED_TOKENS, &n_app_post,
               buf_tokens,      MAX_EXPANDED_TOKENS, &n_buf,
               buf_post_tokens, MAX_EXPANDED_TOKENS, &n_buf_post,
               sample_tokens,   MAX_EXPANDED_TOKENS, &n_sample,
               defined);

  // BCASC: solo se compila lo que el archivo define; el resto conserva
  // su Program de la carga propia. Sin bcasc, comportamiento de siempre.
  if (!bcasc_mode || defined[0]) compile_tokens(app_tokens,      n_app,      &prog_app);
  if (!bcasc_mode || defined[1]) compile_tokens(app_post_tokens, n_app_post, &prog_app_post);
  if (!bcasc_mode || defined[2]) compile_tokens(buf_tokens,      n_buf,      &prog_buf);
  if (!bcasc_mode || defined[3]) compile_tokens(buf_post_tokens, n_buf_post, &prog_buf_post);
  if (!bcasc_mode || defined[4]) compile_tokens(sample_tokens,   n_sample,   &prog_sample);

  // BCASC: aviso de redefiniciones que los bloques retenidos no adoptan
  // (solo si algo quedo retenido: si el archivo redefine todo, no hay
  // mezcla de epocas y el aviso seria ruido).
  if (bcasc_mode && bcasc_n_redefs > 0 &&
      (!defined[0] || !defined[1] || !defined[2] || !defined[3] || !defined[4])) {
    fprintf(stderr,
            "    Aviso | bcasc: definiciones actualizadas que los bloques\n"
            "          | retenidos NO adoptan (las inlinearon en su carga):\n");
    for (int i = 0; i < bcasc_n_redefs; i++)
      fprintf(stderr, "          |   %s\n", bcasc_redefs[i]);
  }

  printf("          | app{ %d/%d }  buf{ %d/%d }  sample{ words:%d defs:%d }\n",
         prog_app.len, prog_app_post.len, prog_buf.len, prog_buf_post.len, prog_sample.len, dict_count);

  if (bcasc_mode) {
    char ret[64]; ret[0] = '\0';
    if (!defined[0]) strncat(ret, "{a} ",  sizeof(ret) - strlen(ret) - 1);
    if (!defined[1]) strncat(ret, "{a.} ", sizeof(ret) - strlen(ret) - 1);
    if (!defined[2]) strncat(ret, "{b} ",  sizeof(ret) - strlen(ret) - 1);
    if (!defined[3]) strncat(ret, "{b.} ", sizeof(ret) - strlen(ret) - 1);
    if (!defined[4]) strncat(ret, "{s} ",  sizeof(ret) - strlen(ret) - 1);
    printf("          | bcasc | retenidos: %s\n", ret[0] ? ret : "ninguno");
  }

  fflush(stdout);
  needs_reload = 0;
}
