// -------------------------------------------------------------------------
// main.c — Punto de entrada: parsea argumentos (--help, librerías, archivo
// principal), inicializa PortAudio, carga librerías y archivo principal, y
// corre el loop principal (hot-reload, generación de audio, scope).
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#include <math.h>
#include "diag.h"
#include "limits.h"
#include "opcodes.h"
#include "state.h"
#include "parser.h"
#include "interpreter.h"
#include "audio.h"
#include "wav.h"
#include "scope.h"
#include "sample.h"
#include "file_watch.h"

static void print_usage(const char *prog) {
  printf("Uso: %s [opciones] [libreria.p ...] archivo.p\n", prog);
  printf("\n");
  printf("Opciones:\n");
  printf("  -h, --help     Muestra esta ayuda y sale\n");
  printf("  --checkscript  Validador puro: carga, informa y termina siempre.\n");
  printf("                 Exit 0 si no hay diagnosticos ni truncamiento; 1 si los hay.\n");
  printf("  --strict-load  Sesion viva estricta: con carga inicial limpia corre\n");
  printf("                 normal; con diagnosticos o truncamiento en la carga\n");
  printf("                 inicial, o truncamiento en un hot-reload, aborta (exit 1).\n");
  printf("  --bcasc        Modo bloques en cascada activo desde el arranque\n");
  printf("                 (equivale a 1 BCASC antes de la primera carga).\n");
  printf("\n");
  printf("Argumentos posicionales:\n");
  printf("  libreria.p    Archivos de libreria (opcionales, todos menos el ultimo).\n");
  printf("                Se cargan una vez al inicio, en orden. Sus definiciones\n");
  printf("                (: ; / := / =:) persisten en el diccionario y sus bloques\n");
  printf("                {a} se ejecutan. Sus bloques {b}/{b.}/{s} y el codigo sin\n");
  printf("                bloque se descartan al cargar el archivo principal.\n");
  printf("                Poner configuraciones y variables dentro de {a ... }.\n");
  printf("  archivo.p     Archivo principal (el ultimo argumento). Se carga al inicio\n");
  printf("                y se monitorea con inotify para hot-reload.\n");
  printf("                Default: %s\n", DATA_FILE_DEFAULT);
  printf("\n");
  printf("Ejemplos:\n");
  printf("  %s                           (usa %s)\n", prog, DATA_FILE_DEFAULT);
  printf("  %s main.p                    (main.p como principal)\n", prog);
  printf("  %s defs.p main.p             (defs.p como libreria)\n", prog);
  printf("  %s setup.p fx.p live.p       (dos librerias + principal)\n", prog);
}

// Resumen de estado despues de correr el {a} del archivo principal.
// La linea ">>> app=..." de load_rpn se imprime antes del {a}, asi que
// no refleja los setters que el propio bloque ejecuta.
static void print_state_summary(void) {
  printf("   Estado | sr:%d  ch:%d  buf:%d  vint:%d  mute:%d\n"
         "          | scope:%d  stype:%d  delay:%d  bcasc:%d\n",
         sample_rate, num_channels, buf_size, vint_mode,
         mute_enabled, scope_enabled, scope_type, delay_enabled, bcasc_mode);
  fflush(stdout);
}


static void signal_handler(int sig) {
  (void)sig;
  running = 0;
}

// Carga inicial sin ejecutar {a.} (no hay programa saliente todavia).
// Se usa para las librerias y para la primera carga del archivo principal.
static void initial_load(const char *path, int *running_flag) {
  load_rpn(path);
  interpreter_on_program_loaded();
  // --checkscript: validacion estatica pura; no correr {a} evita efectos
  // secundarios (recstart, setters de stream, apertura de scope, etc.).
  if (!checkscript_mode) run_program(CTX_APP, &prog_app, running_flag);
}

// Recarga con hot-reload: ejecuta {a.} del programa saliente, carga el nuevo
// y ejecuta su {a}.
static void reload_program(const char *file_data, int *running_flag) {
  run_program(CTX_APP_POST, &prog_app_post, running_flag);
  load_rpn(file_data);
  interpreter_on_program_loaded();
  run_program(CTX_APP, &prog_app, running_flag);
  print_state_summary();
}

// Canario de runtime: si algun flag o toolchain vuelve a plegar los
// isfinite, esto lo hace visible al arrancar en vez de en un NaN fantasma.
static void ieee_canary(void) {
  volatile double cero = 0.0;
  double inf = 1.0 / cero;
  if (isfinite(inf)) {
    fprintf(stderr,
            "AVISO: binario compilado sin semantica IEEE "
            "(-ffinite-math-only): las protecciones NaN/Inf NO estan activas\n");
  }
}

// Flag global para modo checkscript: se setea desde la línea de comandos.
int checkscript_mode = 0;
int strict_load_mode = 0;

// -- Función de inicio
int main(int argc, char *argv[]) {
  // -- Controles de compilación ------------------------------------------
  ieee_canary();

  // -- Parsear argumentos ------------------------------------------------
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--checkscript") == 0) {
      checkscript_mode = 1;
      continue;
    }
    if (strcmp(argv[i], "--strict-load") == 0) {
      strict_load_mode = 1;
      continue;
    }
    if (strcmp(argv[i], "--bcasc") == 0) { bcasc_mode = 1; continue; }
  }

// Archivos posicionales: todo lo que no sea flag (los flags se
  // consumen arriba con continue). El ultimo es el principal; los
  // demas, librerias en orden.
  int num_files = 0;
  const char *positional[64];
  for (int i = 1; i < argc && num_files < 64; i++) {
    if (argv[i][0] == '-' && argv[i][1] != '\0') continue;
    positional[num_files++] = argv[i];
  }
  const char *file_data;
  int num_libs;
  if (num_files == 0) {
    file_data = DATA_FILE_DEFAULT;
    num_libs  = 0;
  } else {
    file_data = positional[num_files - 1];
    num_libs  = num_files - 1;
  }

  // Directorio y nombre del archivo principal para inotify
  char path_copy[4096];
  strncpy(path_copy, file_data, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';
  const char *watch_dir  = dirname(path_copy);
  const char *watch_file = strrchr(file_data, '/');
  watch_file = watch_file ? watch_file + 1 : file_data;

  signal(SIGINT,  signal_handler);
  signal(SIGTERM, signal_handler);

  // -- Inicializacion ----------------------------------------------------
  if (!audio_init()) {
    fprintf(stderr, "No se pudo inicializar el audio. Saliendo.\n");
    return 1;
  }

  for (int i = 0; i < MAX_CHANNELS; i++) {
    scope_color[i][0] = 255;
    scope_color[i][1] = 255;
    scope_color[i][2] = 255;
    scope_stroke[i]   = 1;
  }

  // -- Cargar librerias en orden -----------------------------------------
  int diag_total = 0;
  for (int i = 0; i < num_libs; i++) {
    initial_load(positional[i], &running);
    diag_total += diag_count();
    if (!running) {
      audio_shutdown();
      return 0;
    }
  }
  FileWatch fw = file_watch_init(watch_dir, watch_file);

  // -- Cargar archivo principal (primera vez) ----------------------------
  initial_load(file_data, &running);
  diag_total += diag_count();
  if (!checkscript_mode) print_state_summary();

  int fatal_load = 0;

  // --checkscript: validador puro. Termina siempre; el exit code es la
  // senal (0 limpio, 1 con diagnosticos o truncamiento). Como en este
  // modo los {a} no se ejecutan (parseo puro), no hay recorder ni scope
  // que limpiar: el return directo es seguro.
  if (checkscript_mode) {
    int fallo = (diag_total > 0 || load_truncated);
    fprintf(stderr, "%s | checkscript -- %s: %d diagnostico(s), truncado: %s\n",
            fallo ? "    ERROR" : "    Check",
            fallo ? "FALLO" : "OK",
            diag_total, load_truncated ? "si" : "no");
    audio_shutdown();
    return fallo ? 1 : 0;
  }

  // --strict-load (variante viva-estricta): la carga inicial del archivo
  // principal es la evaluacion del script; si deja diagnosticos o
  // truncamiento, es fatal. Con carga limpia, sesion viva normal.
  if (strict_load_mode && (diag_total > 0 || load_truncated)) {
    fprintf(stderr,
            "    ERROR | strict-load -- carga inicial con %d diagnostico(s), truncado: %s: sesion abortada\n",
            diag_total, load_truncated ? "si" : "no");
    fatal_load = 1;
    running = 0;   // saltea el loop y cae en la salida limpia
  } else if (strict_load_mode) {
    fprintf(stderr, "    Check | strict-load -- carga inicial OK: sesion viva\n");
  }


  // -- Loop principal ----------------------------------------------------
  long long anomaly_accum        = 0;
  int64_t   samples_since_report = 0;
  int64_t   report_interval      = (int64_t)sample_rate * 2;

  while (running) {
    int forced = force_reload;
    force_reload = 0;
    if (file_watch_poll(&fw) || forced) {
      if (forced) fprintf(stderr, "   reload | Recarga forzada por palabra reload.\n");
      needs_reload = 1;
    }
    if (needs_reload) {
      reload_program(file_data, &running);
      // strict-load: un reload truncado dejaria el programa corrido
      // sin aviso visible: fatal. Los diagnosticos de reload solo
      // avisan, como siempre.
      if (strict_load_mode && load_truncated) {
        fprintf(stderr,
                "    ERROR | strict-load -- recarga truncada: sesion abortada\n");
        fatal_load = 1;
        break;   // salida limpia: {a.}, recorder, scope, audio
      }
    }


    audio_restart_if_needed();
    audio_render_and_write();

    if (scope_enabled && !scope_is_open()) {
      if (!scope_init()) scope_enabled = 0;
    }
    if (!scope_enabled && scope_is_open()) {
      scope_shutdown();
    }
    if (scope_is_open()) {
      if (!scope_handle_events()) {
        scope_enabled = 0;
        scope_shutdown();
      } else {
        scope_render();
      }
    }

    anomaly_accum += interpreter_get_anomaly_count();
    interpreter_reset_anomalies();
    samples_since_report += buf_size;
    if (samples_since_report >= report_interval) {
      if (anomaly_accum > 0) {
        fprintf(stderr, "Anomalías | %lld en los últimos ~2s\n", anomaly_accum);
      }
      anomaly_accum        = 0;
      samples_since_report = 0;
      report_interval      = (int64_t)sample_rate * 2;
    }
    interpreter_log_tick();
  }

  // -- Salida limpia -----------------------------------------------------
  run_program(CTX_APP_POST, &prog_app_post, &running);
  recorder_stop();
  sample_reset();
  scope_shutdown();
  audio_shutdown();
  file_watch_close(&fw);
  interpreter_log_stop();

  if (fatal_load) return 1;

  if (interpreter_strict_tripped()) {
    fprintf(stderr, "   Strict | sesión detenida por anomalía (exit 42)\n");
    return 42;
  }
  return 0;
}
