#include "remote_appkeys.h"

#include "logging.h"
#include "util/bus.h"
#include "util/user_event.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#define REMOTE_APPKEYS_MAX_FDS 8

/* LG's own evdev codes (IR_KEY_* in /usr/share/X11/xkb/keycodes/lg, less 8), measured on a
 * G5 (webOS 10.3.1) on "LGE M-RCU - Builtin [0]". They are above the toolchain's KEY_MAX,
 * so they are compared as numbers, not looked up in the key bitmap. */
#define REMOTE_KEY_LGCHANNELS 1043
#define REMOTE_KEY_RAKUTENTV 1044

struct remote_appkeys_t {
    int fds[REMOTE_APPKEYS_MAX_FDS];
    int nfds;
    pthread_t thread;
    bool thread_started;
    volatile int running;
};

/* The remotes: "LGE RCU" (IR), "LGE M-RCU - Builtin [n]" (Magic Remote), "LGE W-RCU". */
static bool is_lg_remote(int fd, char *name, size_t name_size) {
    memset(name, 0, name_size);
    if (ioctl(fd, EVIOCGNAME(name_size - 1), name) < 0) {
        return false;
    }
    return strstr(name, "LGE") != NULL && strstr(name, "RCU") != NULL;
}

static int open_remotes(remote_appkeys_t *keys) {
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *ent;
    while (n < REMOTE_APPKEYS_MAX_FDS && (ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        char name[256];
        if (!is_lg_remote(fd, name, sizeof(name))) {
            close(fd);
            continue;
        }
        /* No EVIOCGRAB: the compositor and Glasshouse must keep seeing these keys. */
        keys->fds[n++] = fd;
        commons_log_info("Input", "Remote app keys: reading %s ('%s')", path, name);
    }
    closedir(dir);
    return n;
}

static void handle_event(const struct input_event *ev) {
    if (ev->type != EV_KEY || ev->value == 2) {
        /* value 2 = kernel autorepeat */
        return;
    }
    bool down = ev->value != 0;
    switch (ev->code) {
        case REMOTE_KEY_LGCHANNELS:
            if (down) {
                commons_log_info("Input", "Remote app keys: LG Channels -> disconnect");
                bus_pushevent(USER_REMOTE_DISCONNECT, NULL, NULL);
            }
            break;
        case REMOTE_KEY_RAKUTENTV:
            commons_log_info("Input", "Remote app keys: Rakuten TV -> Win %s", down ? "down" : "up");
            bus_pushevent(USER_REMOTE_WIN_KEY, (void *) (intptr_t) down, NULL);
            break;
        default:
            break;
    }
}

/* A node that went away (a remote that was unpaired, say) stays "readable" for select, and read()
 * then fails (ENODEV) without blocking: left in the set, the loop below would spin at full speed
 * for the rest of the stream. Close it and stop watching it; the other nodes keep working. */
static void drop_remote(remote_appkeys_t *keys, int index) {
    commons_log_info("Input", "Remote app keys: a remote node went away (errno %d), no longer reading it", errno);
    close(keys->fds[index]);
    keys->nfds--;
    keys->fds[index] = keys->fds[keys->nfds];
    keys->fds[keys->nfds] = -1;
}

static void *remote_appkeys_thread(void *arg) {
    remote_appkeys_t *keys = arg;
    while (keys->running) {
        fd_set set;
        FD_ZERO(&set);
        int maxfd = -1;
        for (int i = 0; i < keys->nfds; i++) {
            FD_SET(keys->fds[i], &set);
            if (keys->fds[i] > maxfd) {
                maxfd = keys->fds[i];
            }
        }
        if (maxfd < 0) {
            break; /* No remote node left. */
        }
        struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        int rc = select(maxfd + 1, &set, NULL, NULL, &tv);
        if (rc == 0 || (rc < 0 && errno == EINTR)) {
            continue;
        }
        if (rc < 0) {
            /* Would fail again at once: stop instead of spinning. */
            commons_log_error("Input", "Remote app keys: select failed (errno %d), stopping", errno);
            break;
        }
        /* Backwards: dropping a node moves the last one into its place, which was already read. */
        for (int i = keys->nfds - 1; i >= 0; i--) {
            if (!FD_ISSET(keys->fds[i], &set)) {
                continue;
            }
            struct input_event ev;
            ssize_t n;
            while ((n = read(keys->fds[i], &ev, sizeof(ev))) == (ssize_t) sizeof(ev)) {
                handle_event(&ev);
            }
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                drop_remote(keys, i);
            }
        }
    }
    return NULL;
}

remote_appkeys_t *remote_appkeys_start(void) {
    remote_appkeys_t *keys = calloc(1, sizeof(*keys));
    if (keys == NULL) {
        return NULL;
    }
    for (int i = 0; i < REMOTE_APPKEYS_MAX_FDS; i++) {
        keys->fds[i] = -1;
    }
    keys->nfds = open_remotes(keys);
    if (keys->nfds <= 0) {
        commons_log_info("Input", "Remote app keys: no LG remote node could be opened");
        free(keys);
        return NULL;
    }
    keys->running = 1;
    if (pthread_create(&keys->thread, NULL, remote_appkeys_thread, keys) != 0) {
        commons_log_error("Input", "Remote app keys: thread create failed");
        remote_appkeys_stop(keys);
        return NULL;
    }
    keys->thread_started = true;
    return keys;
}

void remote_appkeys_stop(remote_appkeys_t *keys) {
    if (keys == NULL) {
        return;
    }
    keys->running = 0;
    if (keys->thread_started) {
        pthread_join(keys->thread, NULL);
        keys->thread_started = false;
    }
    for (int i = 0; i < keys->nfds; i++) {
        if (keys->fds[i] >= 0) {
            close(keys->fds[i]);
            keys->fds[i] = -1;
        }
    }
    free(keys);
}
