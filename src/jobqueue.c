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
#include "debug.h"
#include "misc.h"

#include <stdlib.h>
#include <stdio.h>
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

typedef struct JobQueueCommon {
    const char* const* cmd_template;
    int max_workers;
} JobQueueCommon;

struct JobQueue {
    JobQueueCommon common;
    pthread_mutex_t mutex;
    pid_t child_pid;
    int child_stdin;
};


typedef struct WorkUnit {
    const char* path;
    pid_t worker_pid;
} WorkUnit;


typedef struct JobQueueProcess {
    JobQueueCommon* common;
    int input_fd;

    int readbuf_capacity;
    int readbuf_size;
    char* readbuf;

    int active_workers;
    GHashTable* active_work_units; // of pid to WorkUnit*

    GQueue* work_queue;
} JobQueueProcess;


static void free_work_unit(gpointer unit);


static void child_main(JobQueueCommon* common, int input_fd);

static int read_and_enqueue_work_unit(JobQueueProcess* jqp);
static int take_from_readbuf(JobQueueProcess* jqp, GByteArray* buf); // returns 1 if encountered '\0'

static void wait_away_finished_workers(JobQueueProcess* jqp);
static _Bool wait_away_worker(JobQueueProcess* jqp, _Bool nohang);
static void start_queued_work(JobQueueProcess* jqp);
static void start_worker(JobQueueProcess* jqp, WorkUnit* unit);
static void make_command(const char *const *cmd_template,
                         const char** cmd_buf,
                         const char *absPath);

JobQueue* jobqueue_create(const char** cmd_template, int max_workers) {
    JobQueue* jq = NULL;

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) == -1) {
        goto error;
    }

    jq = malloc(sizeof(JobQueue));
    if (!jq) {
        goto error;
    }
    jq->common.cmd_template = cmd_template;
    jq->common.max_workers = max_workers;
    pthread_mutex_init(&jq->mutex, NULL);
    jq->child_stdin = pipefd[1];

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid == 0) {
        DPRINT("Job queue process forked");
        close(pipefd[1]);
        child_main(&jq->common, pipefd[0]);
        _exit(0);
    } else if (pid == -1) {
        DPRINTF("Failed to fork jobqueue: %d", errno);
        goto error;
    }

    jq->child_pid = pid;

    return jq;

error:
    close(pipefd[0]);
    close(pipefd[1]);
    if (jq) {
        pthread_mutex_destroy(&jq->mutex);
    }
    free(jq);
    return NULL;
}

int jobqueue_destroy(JobQueue* jq) {
    close(jq->child_stdin);
    DPRINT("Closed pipe to job queue");

    int status = 0;
    int ret = 0;
    if (waitpid(jq->child_pid, &status, 0) == jq->child_pid) {
        if (WIFSIGNALED(status)) {
            ret = -WTERMSIG(status);
            DPRINTF("Job queue process was killed by signal %d", -ret);
        } else if (WIFEXITED(status)) {
            ret = WEXITSTATUS(status);
            DPRINTF("Job queue process exit status: %d", ret);
        } else {
            DPRINTF("Unexpected status from waitpid: %d", status);
            ret = -1000;
        }
    } else {
        ret = -2000;
    }

    pthread_mutex_destroy(&jq->mutex);
    free(jq);
    return ret;
}

void jobqueue_add_file(JobQueue* jq, const char* path) {
    pthread_mutex_lock(&jq->mutex);

    size_t len = strlen(path) + 1;
    size_t amt_written = 0;
    while (amt_written < len) {
        size_t remaining = len - amt_written;
        ssize_t ret = write(jq->child_stdin, &path[amt_written], remaining);
        if (ret == -1) {
            DPRINTF("Error writing to job queue: %d", errno);
            break;
        }
        amt_written += ret;
    }
    fsync(jq->child_stdin);

    DPRINTF("Added to job queue: %s", path);

    pthread_mutex_unlock(&jq->mutex);
}



static void free_work_unit(gpointer unit) {
    g_free(unit);
}


static void child_main(JobQueueCommon* common, int input_fd) {
    JobQueueProcess jqp;
    jqp.common = common;

    jqp.input_fd = input_fd;
    jqp.readbuf_capacity = 4096;
    jqp.readbuf_size = 0;
    jqp.readbuf = alloca(jqp.readbuf_capacity);

    jqp.active_workers = 0;
    jqp.active_work_units = g_hash_table_new_full(&g_direct_hash,
                                                  &g_direct_equal,
                                                  NULL,
                                                  &free_work_unit);

    jqp.work_queue = g_queue_new();

    while (1) {
        if (!read_and_enqueue_work_unit(&jqp)) {
            break;
        }
        wait_away_finished_workers(&jqp);

        _Bool workers_available = jqp.active_workers < jqp.common->max_workers;
        _Bool have_work = jqp.work_queue->length > 0;
        if (have_work && !workers_available) {
            //FIXME: this will cause the FUSE process to block!
            DPRINT("No more worker slots - waiting for one to finish");
            wait_away_worker(&jqp, FALSE);
        }
        workers_available = jqp.active_workers < jqp.common->max_workers;
        assert(workers_available);

        if (have_work) {
            start_queued_work(&jqp);
        }
    }

    // TODO: what to do with live children?

    DPRINT("Job queue process cleaning up");
    g_queue_free(jqp.work_queue);
    g_hash_table_destroy(jqp.active_work_units);
    close(input_fd);
}

static int read_and_enqueue_work_unit(JobQueueProcess* jqp) {
    GByteArray* buf = g_byte_array_new();

    DPRINT("Reading work unit from FUSE process");
    while (1) {
        if (take_from_readbuf(jqp, buf)) {
            // If we go here, we found the null byte that separates commands.
            // Some data may have been left in readbuf.
            break;
        }
        assert(jqp->readbuf_size == 0);

        DPRINT("Buffering input from FUSE process");
        ssize_t ret = read(jqp->input_fd, jqp->readbuf, jqp->readbuf_capacity);
        DPRINTF("read() from FUSE process returned %d bytes", ret);
        if (ret == -1 || ret == 0) { // error or eof
            DPRINT("Pipe from FUSE process was closed");
            jqp->readbuf_size = 0;
            return 0;
        } else {
            jqp->readbuf_size = ret;
        }
    }

    WorkUnit* unit = g_malloc(sizeof(WorkUnit));
    DPRINTF("Received work unit: '%s'", (char*)buf->data);
    unit->path = (char*)buf->data;
    unit->worker_pid = -1;
    g_byte_array_free(buf, FALSE);

    g_queue_push_head(jqp->work_queue, unit);
    return 1;
}

static int take_from_readbuf(JobQueueProcess* jqp, GByteArray* buf) {
    if (jqp->readbuf_size > 0) {
        size_t amount = strnlen(jqp->readbuf, jqp->readbuf_size - 1) + 1;
        g_byte_array_append(buf, (guint8*)jqp->readbuf, amount);
        int found_separator = (jqp->readbuf[amount - 1] == '\0');

        if (amount < jqp->readbuf_size) {
            memmove(jqp->readbuf, jqp->readbuf + amount, jqp->readbuf_size - amount);
        }
        jqp->readbuf_size -= amount;

        if (found_separator) {
            return TRUE;
        }
    }
    return FALSE;
}

static void wait_away_finished_workers(JobQueueProcess* jqp) {
    int ret;
    do {
        ret = wait_away_worker(jqp, TRUE);
    } while (ret > 0);
}

static _Bool wait_away_worker(JobQueueProcess* jqp, _Bool nohang) {
    int status;

    _Bool ret = FALSE;
    pid_t pid = waitpid(-1, &status, nohang ? WNOHANG : 0);
    if (pid > 0) {
        g_hash_table_remove(jqp->active_work_units, GINT_TO_POINTER(pid));
        jqp->active_workers--;
        ret = TRUE;
    }

    return ret;
}

static void start_queued_work(JobQueueProcess* jqp) {
    assert(jqp->work_queue->length > 0);
    WorkUnit* unit = g_queue_pop_tail(jqp->work_queue);
    start_worker(jqp, unit);
}

static void start_worker(JobQueueProcess* jqp, WorkUnit* unit) {
    DPRINTF("Starting worker for '%s'", unit->path);

    const char* const * cmd_template = jqp->common->cmd_template;
    int cmd_len = 1 + strings_before_null(cmd_template);
    const char** cmd = alloca(cmd_len * sizeof(const char*));
    make_command(cmd_template, cmd, unit->path);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(cmd[0], (char* const*)cmd);
        _exit(1);
    }

    unit->worker_pid = pid;

    g_hash_table_insert(jqp->active_work_units, GINT_TO_POINTER(pid), unit);
    jqp->active_workers++;
}

static void make_command(const char *const *cmd_template,
                         const char** cmd_buf,
                         const char *absPath) {
    int i;
    for (i = 0; cmd_template[i] != NULL; ++i) {
        const char *part = cmd_template[i];

        if (strcmp(part, "{}") == 0) {
            cmd_buf[i] = absPath;
        } else {
            cmd_buf[i] = part;
        }
        DPRINTF("argv[%d] = \"%s\"", i, cmd_buf[i]);
    }
    cmd_buf[i] = NULL;
}

