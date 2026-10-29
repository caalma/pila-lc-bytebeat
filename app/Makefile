# ─────────────────────────────────────────────────────────────────────────
# Makefile — pila-livecoding-bytebeat
# ─────────────────────────────────────────────────────────────────────────
APP        := pila-lc-bb
SRC_DIR    := src
BUILD_DIR  := build
CC         := gcc
EDITOR     := emacs27

# PortAudio via pkg-config. En Debian/Ubuntu el nombre es portaudio-2.0
# (paquete portaudio19-dev). Si pkg-config no lo encuentra con ese nombre,
# probar PKGS=portaudio.
PKGS       := portaudio-2.0 sdl2

# -D_GNU_SOURCE habilita inotify, dirname y demas extensiones POSIX/GNU
# que el proyecto necesita bajo -std=c11.
CFLAGS     := -std=c11 -D_GNU_SOURCE -Wall -Wextra -MMD -MP $(shell pkg-config --cflags $(PKGS))
LDLIBS     := $(shell pkg-config --libs $(PKGS)) -lm
LDFLAGS    :=

# Build por defecto: optimizado, con tuning para la CPU local y LTO.
# El LTO importa porque el interprete llama funciones chicas de otras
# unidades de compilacion; sin LTO no se inlinean entre archivos.
CFLAGS     += -O3 -march=native -flto
LDFLAGS    += -O3 -flto

# `make FAST=1` suma -Ofast (implica -ffast-math): mas veloz en las
# funciones trigonometricas (fs/fc/ft/fl/fe/fr) y en el modo float del
# interprete, pero relaja IEEE 754 (reordena operaciones, asume ausencia
# de NaN/Inf). Opt-in para no cambiar el comportamiento por defecto.
# -Ofast implica -ffinite-math-only, que elimina los isfinite de la red de
# seguridad contra NaN/Inf (process_sample, bits_i32/bits_i64/stack_int,
# to_int32, sample_get). El -fno-... DEBE ir despues de -Ofast.
ifeq ($(FAST),1)
CFLAGS  := $(filter-out -O3,$(CFLAGS)) -Ofast -fno-finite-math-only
endif

# `make DEBUG=1` compila con simbolos y sanitizers (ASan/UBSan).
ifdef DEBUG
CFLAGS  := $(filter-out -O3 -march=native -flto -Ofast,$(CFLAGS)) -O0 -g -fsanitize=address,undefined
LDFLAGS := $(filter-out -O3 -flto,$(LDFLAGS)) -fsanitize=address,undefined
endif

SRCS       := $(wildcard $(SRC_DIR)/*.c)
OBJS       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS       := $(OBJS:.o=.d)
CODE_FILE  ?= ./tmp/live.p

# Bajo ASan, los leaks de una sola vez de ALSA/PulseAudio/PortAudio hacen
# ruido; por defecto se apaga solo la deteccion de leaks (ASan/UBSan siguen
# activos para los bugs reales).
RUN_ENV    := ASAN_OPTIONS=detect_leaks=0

# -- Rebuild automatico al cambiar de flags (fingerprint de CFLAGS). -------
FLAGS_STAMP := $(BUILD_DIR)/.cflags-stamp
$(shell mkdir -p $(BUILD_DIR))
ifneq ($(strip $(CFLAGS)),$(strip $(shell cat $(FLAGS_STAMP) 2>/dev/null)))
$(shell echo "$(CFLAGS)" > $(FLAGS_STAMP))
endif

.PHONY: all run run-environ run-anomalies run-strict run-check debug debug-run debug-inspect clean help editar

all: $(APP) ## Compila el binario (default: release optimizado + LTO)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(FLAGS_STAMP) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(DEPS)

## -- Compila y ejecuta con tmp/live.p (o CODE_FILE=ruta) ------------------
run: $(APP)
	@mkdir -p $(dir $(CODE_FILE))
	@touch $(CODE_FILE)
	$(RUN_ENV) ./$(APP) $(CODE_FILE)


## -- Compila y ejecuta, con entorno y librerias ---------------------------
run-environ: $(APP)
	echo '' > ./tmp/live.p; \
	$(MAKE) $(APP); \
	./$(APP) \
		./codes/lib/set_8bit.p \
		./codes/lib/defs.p \
		./tmp/live.p

# -- Ejecuta en modo de depuracion iterativa -------------------------------
# Recompila y ejecuta el binario directamente (sin make anidado) para que
# el codigo de salida llegue intacto al loop: 0 = salida limpia, 42 = STRICT,
# 139 = crash. Un error de compilacion no saca del loop: avisa y espera.
run-check-anomalies: $(APP)
	@while true; do \
		if ! $(MAKE) ./$(APP); then \
			printf '\n----->> error de compilacion. Corregi y presiona CUALQUIER TECLA (Ctrl+D sale).\n'; \
			read _tecla || break; \
			continue; \
		fi; \
		$(RUN_ENV) ./$(APP) ./tmp/live.p; \
		code=$$?; \
		printf '\n----->> termino con codigo %s (0=limpia, 42=STRICT, 139=crash).\n' "$$code"; \
		printf '       Tras corregir el error, presiona CUALQUIER TECLA para reintentar (Ctrl+D sale).\n'; \
		read _tecla || break; \
	done

## -- Valida un script sin ejecutarlo (modo estricto) ----------------------
run-check: $(APP)
	@$(RUN_ENV) ./$(APP) --checkscript $(CODE_FILE) || echo "!! --- Script INVÁLIDO"

## -- Sesion viva estricta: carga inicial con diagnosticos o truncamiento = fatal
run-strict: $(APP)
	@mkdir -p $(dir $(CODE_FILE))
	@touch $(CODE_FILE)
	$(RUN_ENV) ./$(APP) --strict-load $(CODE_FILE) || echo "!! --- Script INVÁLIDO"


## -- Compila con -O0 -g y sanitizers (ASan/UBSan) -------------------------
debug:
	$(MAKE) DEBUG=1

## -- Compila en modo debug y ejecuta --------------------------------------
debug-run:
	$(MAKE) DEBUG=1 run

## -- Compila en modo debug, ejecuta, obtiene informe de volcado de pila ---
debug-inspect: $(APP)
	$(MAKE) DEBUG=1 && gdb -ex run -ex "bt full" -ex quit --args ./$(APP) $(CODE_FILE)

## -- Elimina artefactos de build ------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(APP); echo '' > ./tmp/live.p

## -- Lista los targets disponibles ----------------------------------------
help:
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

## -- Abre el editor en codes y carga las librerías especializadas ---------
editar:
	$(EDITOR) -nw -L "$$(realpath ./utils)" -l pilalcbb-mode.el -l keybind.el codes/
