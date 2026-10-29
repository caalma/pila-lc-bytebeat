// -------------------------------------------------------------------------
// file_watch.c
// -------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "file_watch.h"

FileWatch file_watch_init(const char *watch_dir, const char *watch_file) {
    FileWatch fw = {0};
    fw.watch_file = watch_file;
    fw.fd = inotify_init();
    if (fw.fd < 0) { perror("inotify_init"); exit(1); }
    fcntl(fw.fd, F_SETFL, O_NONBLOCK);
    fw.wd = inotify_add_watch(fw.fd, watch_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (fw.wd < 0) { perror("inotify_add_watch"); exit(1); }
    return fw;
}

int file_watch_poll(FileWatch *fw) {
    char ibuf[4096];
    ssize_t rlen = read(fw->fd, ibuf, sizeof(ibuf));
    if (rlen > 0) {
        char *ptr = ibuf, *end = ibuf + rlen;
        while (ptr < end) {
            struct inotify_event *ie = (struct inotify_event *)ptr;
            if (ie->len > 0 && strcmp(ie->name, fw->watch_file) == 0) return 1;
            ptr += sizeof(struct inotify_event) + ie->len;
        }
    } else if (rlen < 0 && errno != EAGAIN) {
        perror("inotify read");
    }
    return 0;
}

void file_watch_close(FileWatch *fw) {
    inotify_rm_watch(fw->fd, fw->wd);
    close(fw->fd);
}
