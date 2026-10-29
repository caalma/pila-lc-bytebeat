// -------------------------------------------------------------------------
// parser.h — Carga y compila un archivo .p en los cinco Programas globales.
// -------------------------------------------------------------------------
#ifndef PARSER_H
#define PARSER_H

#include "opcodes.h"   // por Inst, usado en dict_entry_code

int parser_dict_count(void);
int parser_var_slots_used(void);

// Modo checkscript: si está activo, el truncamiento de RPN_BUF_SIZE es
// fatal (la función retorna sin compilar nada); si está inactivo, solo
// avisa por stderr y continúa con el archivo truncado.
extern int checkscript_mode;

// Lee el archivo en `path`, lo tokeniza, expande macros, pliega constantes,
// rutea por bloques de contexto y llena los cinco Programas. Tambien
// actualiza `rpn_source` y limpia `needs_reload`.
void load_rpn(const char *path);

// 1 si el ultimo load_rpn tuvo que truncar el archivo por exceder
// RPN_BUF_SIZE. Lo usa main.c para el codigo de salida de --checkscript.
extern int load_truncated;

// Modo vivo-estricto: con carga inicial limpia corre como una sesion normal;
// con diagnosticos o truncamiento en la carga inicial, o truncamiento en un
// hot-reload, aborta con exit 1 despues de una salida limpia.
extern int strict_load_mode;

// Acceso del interprete al codigo compilado de una entrada del diccionario
// (OP_CALL, la cita '=). Devuelve 1 si el slot es una funcion o alias listo
// para ejecutarse; expone su codigo y sus tres mapas de saltos propios.
// Un cuerpo vacio es valido: len 0 con mapas NULL = llamada no-op.
int dict_entry_code(int slot, const Inst **code, int *len,
                    int **lm, int **ijm, int **ejm);

#endif // PARSER_H
