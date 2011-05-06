/*
    Copyright 2011 Martin Pärtel <martin.partel@gmail.com>

    This file is part of queuefs.

    queuefs is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    queuefs is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with queuefs.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "jobqueue.h"
#include "jobqueue_process.h"
#include "debug.h"
#include "misc.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <glib.h>


typedef struct WorkUnit {
    char* path;
    pid_t worker_pid;
} WorkUnit;

static const JobQueueSettings* settings;
static int input_fd;
static int output_fd;

static int readbuf_capacity;
static int readbuf_size;
static char* readbuf;

// The following may only be accessed while SIGCHLD is blocked
// since it is also accessed in the SIGCHLD handler.
static int active_workers;
static GHashTable* active_work_units; // of pid to WorkUnit*
static GQueue* work_queue;            // of WorkUnit*

static sigset_t sigchld_set;

/*
 * Calls wait_away_finished_workers() and start_queued_work().
 */
static void handle_sigchld(int signum);
static void register_sigchld_handler();

static int process_input();
static void handle_incoming_command(const char* buf);
static int take_from_readbuf(GByteArray* buf); // returns 1 if encountered '\0'

static void wait_for_all_workers_to_finish();

static void wait_away_finished_workers();
static bool wait_away_worker(bool nohang);
static void start_queued_work();
static void start_worker(WorkUnit* unit);
static gchar** make_command(const char *file_path);

static void free_work_unit(gpointer unit);


void jobqueue_process_main(JobQueueSettings* settings_, int input_fd_, int output_fd_) {
    settings = settings_;

    input_fd = input_fd_;
    output_fd = output_fd_;
    readbuf_capacity = 4096;
    readbuf_size = 0;
    readbuf = alloca(readbuf_capacity);

    active_workers = 0;
    active_work_units = g_hash_table_new_full(&g_direct_hash,
                                              &g_direct_equal,
                                              NULL,
                                              &free_work_unit);

    work_queue = g_queue_new();

    sigemptyset(&sigchld_set);
    sigaddset(&sigchld_set, SIGCHLD);

    register_sigchld_handler();

    while (1) {
        if (!process_input()) {
            break;
        }

        sigprocmask(SIG_BLOCK, &sigchld_set, NULL);

        if (work_queue->length > 0) {
            if (active_workers < settings->max_workers) {
                start_queued_work();
            } else {
                DPRINT("No more worker slots - work is left queued");
            }
        }

        sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
    }

    // Live children will be inherited by the init process
    sigprocmask(SIG_BLOCK, &sigchld_set, NULL);

    DPRINT("Job queue process cleaning up");
    g_queue_free(work_queue);
    g_hash_table_destroy(active_work_units);
    close(input_fd);
}

static void handle_sigchld(int signum) {
    (void)signum;
    wait_away_finished_workers();
    start_queued_work();
}

static void register_sigchld_handler() {
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

static int process_input() {
    GByteArray* buf = g_byte_array_new();

    DPRINT("Reading work unit from FUSE process");
    while (1) {
        if (take_from_readbuf(buf)) {
            // If we go here, we found the null byte that separates commands.
            // Some data may have been left in readbuf.
            break;
        }
        assert(readbuf_size == 0);

        DPRINT("Buffering input from FUSE process");
        ssize_t ret = read(input_fd, readbuf, readbuf_capacity);
        DPRINTF("read() from FUSE process returned %d bytes", ret);
        if (ret == -1 || ret == 0) { // error or eof
            DPRINT("Pipe from FUSE process was closed");
            readbuf_size = 0;
            return 0;
        } else {
            readbuf_size = ret;
        }
    }

    handle_incoming_command((const char*)buf->data);
    g_byte_array_free(buf, true);

    return 1;
}

static void handle_incoming_command(const char* buf) {
    DPRINTF("Received command: '%s'", buf);

    if (g_str_has_prefix(buf, "EXEC ")) {
        WorkUnit* unit = g_malloc(sizeof(WorkUnit));
        unit->path = g_strdup(buf + strlen("EXEC "));
        unit->worker_pid = -1;
        g_queue_push_head(work_queue, unit);
    } else if (g_str_equal(buf, "FLUSH")) {
        while (work_queue->length > 0 || active_workers > 0) {
            wait_for_all_workers_to_finish();
            // start_queued_work() is called by SIGCHLD handler automatically
        }

        while (true) {
            if (write(output_fd, "1", 1) == 1) {
                break;
            }
        }
    }
}

static int take_from_readbuf(GByteArray* buf) {
    if (readbuf_size > 0) {
        size_t amount = strnlen(readbuf, readbuf_size - 1) + 1;
        g_byte_array_append(buf, (guint8*)readbuf, amount);
        int found_separator = (readbuf[amount - 1] == '\0');

        if (amount < readbuf_size) {
            memmove(readbuf, readbuf + amount, readbuf_size - amount);
        }
        readbuf_size -= amount;

        if (found_separator) {
            return true;
        }
    }
    return false;
}

static void wait_for_all_workers_to_finish() {
    sigprocmask(SIG_BLOCK, &sigchld_set, NULL);
    while (active_workers > 0) {
        wait_away_worker(false);
    }
    sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
}

static void wait_away_finished_workers() {
    int ret;
    do {
        ret = wait_away_worker(true);
    } while (ret > 0);
}

static bool wait_away_worker(bool nohang) {
    int status;

    bool ret = false;
    pid_t pid = waitpid(-1, &status, nohang ? WNOHANG : 0);
    if (pid > 0) {
        g_hash_table_remove(active_work_units, GINT_TO_POINTER(pid));
        active_workers--;
        ret = true;
    }

    return ret;
}

static void start_queued_work() {
    if (work_queue->length > 0) {
        WorkUnit* unit = g_queue_pop_tail(work_queue);
        start_worker(unit);
    }
}

static void start_worker(WorkUnit* unit) {
    DPRINTF("Starting worker for '%s'", unit->path);

    gchar** cmd = make_command(unit->path);

    pid_t pid = fork();
    if (pid == 0) {
        sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
        execvp(cmd[0], (char* const*)cmd);
        _exit(1);
    }

    g_strfreev(cmd);

    unit->worker_pid = pid;

    g_hash_table_insert(active_work_units, GINT_TO_POINTER(pid), unit);
    active_workers++;
}

static gchar** make_command(const char *file_path) {
    // TODO: change cmd_template to be a single string
    // and always invoke /bin/sh -c cmd_template_with_quoted_path
    char* quoted_path = g_shell_quote(file_path);
    const char* const* cmd_template = settings->cmd_template;
    const int num_parts = g_strv_length((gchar**)cmd_template);
    gchar** cmd = g_malloc_n(num_parts + 1, sizeof(gchar*));
    cmd[num_parts] = NULL;

    for (int i = 0; cmd_template[i] != NULL; ++i) {
        if (strstr(cmd_template[i], "{}")) {
            gchar** parts = g_strsplit(cmd_template[i], "{}", 0);
            cmd[i] = g_strjoinv(quoted_path, parts);
            g_strfreev(parts);
        } else {
            cmd[i] = g_strdup(cmd_template[i]);
        }
        DPRINTF("argv[%d] = \"%s\"", i, cmd[i]);
    }

    g_free(quoted_path);
    return cmd;
}

static void free_work_unit(gpointer unit) {
    g_free(((WorkUnit*)unit)->path);
    g_free(unit);
}
