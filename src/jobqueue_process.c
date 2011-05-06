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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#include <glib.h>


static const JobQueueSettings* settings;
static int input_fd;

static int readbuf_capacity;
static int readbuf_size;
static char* readbuf;

static int active_workers;
static GHashTable* active_work_units; // of pid to WorkUnit*


typedef struct WorkUnit {
    const char* path;
    pid_t worker_pid;
} WorkUnit;

static GQueue* work_queue;


static int read_and_enqueue_work_unit();
static int take_from_readbuf(GByteArray* buf); // returns 1 if encountered '\0'

static void wait_away_finished_workers();
static _Bool wait_away_worker(_Bool nohang);
static void start_queued_work();
static void start_worker(WorkUnit* unit);
static void make_command(const char** cmd_buf, const char *absPath);



void jobqueue_process_main(JobQueueSettings* settings_, int input_fd_) {
    settings = settings_;

    input_fd = input_fd_;
    readbuf_capacity = 4096;
    readbuf_size = 0;
    readbuf = alloca(readbuf_capacity);

    active_workers = 0;
    active_work_units = g_hash_table_new_full(&g_direct_hash,
                                              &g_direct_equal,
                                              NULL,
                                              &g_free);

    work_queue = g_queue_new();

    while (1) {
        if (!read_and_enqueue_work_unit()) {
            break;
        }
        wait_away_finished_workers();

        _Bool workers_available = active_workers < settings->max_workers;
        _Bool have_work = work_queue->length > 0;
        if (have_work && !workers_available) {
            //FIXME: this will cause the parent process to block!
            DPRINT("No more worker slots - waiting for one to finish");
            wait_away_worker(FALSE);
        }
        workers_available = active_workers < settings->max_workers;
        assert(workers_available);

        if (have_work) {
            start_queued_work();
        }
    }

    // TODO: what to do with live children?

    DPRINT("Job queue process cleaning up");
    g_queue_free(work_queue);
    g_hash_table_destroy(active_work_units);
    close(input_fd);
}

static int read_and_enqueue_work_unit() {
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

    WorkUnit* unit = g_malloc(sizeof(WorkUnit));
    DPRINTF("Received work unit: '%s'", (char*)buf->data);
    unit->path = (char*)buf->data;
    unit->worker_pid = -1;
    g_byte_array_free(buf, FALSE);

    g_queue_push_head(work_queue, unit);
    return 1;
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
            return TRUE;
        }
    }
    return FALSE;
}

static void wait_away_finished_workers() {
    int ret;
    do {
        ret = wait_away_worker(TRUE);
    } while (ret > 0);
}

static _Bool wait_away_worker(_Bool nohang) {
    int status;

    _Bool ret = FALSE;
    pid_t pid = waitpid(-1, &status, nohang ? WNOHANG : 0);
    if (pid > 0) {
        g_hash_table_remove(active_work_units, GINT_TO_POINTER(pid));
        active_workers--;
        ret = TRUE;
    }

    return ret;
}

static void start_queued_work() {
    assert(work_queue->length > 0);
    WorkUnit* unit = g_queue_pop_tail(work_queue);
    start_worker(unit);
}

static void start_worker(WorkUnit* unit) {
    DPRINTF("Starting worker for '%s'", unit->path);

    int cmd_len = 1 + strings_before_null(settings->cmd_template);
    const char** cmd = alloca(cmd_len * sizeof(const char*));
    make_command(cmd, unit->path);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(cmd[0], (char* const*)cmd);
        _exit(1);
    }

    unit->worker_pid = pid;

    g_hash_table_insert(active_work_units, GINT_TO_POINTER(pid), unit);
    active_workers++;
}

static void make_command(const char** cmd_buf, const char *absPath) {
    int i;
    for (i = 0; settings->cmd_template[i] != NULL; ++i) {
        const char *part = settings->cmd_template[i];

        if (strcmp(part, "{}") == 0) {
            cmd_buf[i] = absPath;
        } else {
            cmd_buf[i] = part;
        }
        DPRINTF("argv[%d] = \"%s\"", i, cmd_buf[i]);
    }
    cmd_buf[i] = NULL;
}

