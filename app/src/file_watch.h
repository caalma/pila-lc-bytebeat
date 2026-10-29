// -------------------------------------------------------------------------
// file_watch.h — Hot-reload del archivo .p via inotify: detecta cuando el
// archivo que se esta livecodeando fue guardado.
// -------------------------------------------------------------------------
#ifndef FILE_WATCH_H
#define FILE_WATCH_H

typedef struct {
    int         fd;
    int         wd;
    const char *watch_file; // solo el nombre de archivo, sin directorio
} FileWatch;

// Inicializa inotify sobre `watch_dir`, observando escrituras/creaciones.
// Termina el proceso (perror + exit) si no puede inicializar inotify.
FileWatch file_watch_init(const char *watch_dir, const char *watch_file);

// Revisa eventos pendientes de forma no bloqueante. Devuelve 1 si
// `watch_file` fue modificado/creado desde la ultima llamada, 0 si no.
int file_watch_poll(FileWatch *fw);

// Libera los recursos de inotify.
void file_watch_close(FileWatch *fw);

#endif // FILE_WATCH_H
