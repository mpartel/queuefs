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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

struct JobQueue {
    JobQueueSettings settings;
    pthread_mutex_t mutex;
    pid_t child_pid;
    int child_input_fd;
    int child_output_fd;
};

static void send_command(JobQueue* jq, const char* cmd, size_t len);


JobQueue* jobqueue_create(const char** cmd_template, int max_workers) {
    JobQueue* jq = NULL;

    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(input_pipe) == -1) {
        goto error;
    }
    if (pipe(output_pipe) == -1) {
        goto error;
    }

    jq = malloc(sizeof(JobQueue));
    if (!jq) {
        goto error;
    }
    jq->settings.cmd_template = cmd_template;
    jq->settings.max_workers = max_workers;
    pthread_mutex_init(&jq->mutex, NULL);
    jq->child_input_fd = input_pipe[1];
    jq->child_output_fd = output_pipe[0];

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid == 0) {
        DPRINT("Job queue process forked");
        close(input_pipe[1]);
        close(output_pipe[0]);
        jobqueue_process_main(&jq->settings, input_pipe[0], output_pipe[1]);
        _exit(0);
    } else if (pid == -1) {
        DPRINTF("Failed to fork jobqueue: %d", errno);
        goto error;
    }

    jq->child_pid = pid;

    return jq;

error:
    close(input_pipe[0]);
    close(input_pipe[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);
    if (jq) {
        pthread_mutex_destroy(&jq->mutex);
    }
    free(jq);
    return NULL;
}

void jobqueue_add_file(JobQueue* jq, const char* path) {
    size_t len = strlen("EXEC ") + strlen(path) + 1;
    char* cmd = alloca(len);
    strcpy(cmd, "EXEC ");
    strcpy(cmd + strlen("EXEC "), path);

    pthread_mutex_lock(&jq->mutex);
    send_command(jq, cmd, len);
    pthread_mutex_unlock(&jq->mutex);

    DPRINTF("Added to job queue: %s", path);
}

void jobqueue_flush(JobQueue* jq)
{
    pthread_mutex_lock(&jq->mutex);

    send_command(jq, "FLUSH", strlen("FLUSH") + 1);

    char buf;
    int ret = read(jq->child_output_fd, &buf, 1);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
        DPRINT("Failed to read from jobqueue.");
        abort();
    }

    pthread_mutex_unlock(&jq->mutex);
}

int jobqueue_destroy(JobQueue* jq) {
    close(jq->child_input_fd);
    close(jq->child_output_fd);
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

static void send_command(JobQueue* jq, const char* cmd, size_t len) {
    assert(pthread_mutex_trylock(&jq->mutex) == EBUSY);

    size_t amt_written = 0;
    while (amt_written < len) {
        size_t remaining = len - amt_written;
        ssize_t ret = write(jq->child_input_fd, &cmd[amt_written], remaining);
        if (ret > 0) {
            amt_written += ret;
        } else {
            if (ret == -1) {
                DPRINTF("Error writing to job queue: %d", errno);
            } else {
                DPRINTF("Job queue pipe closed: %d", errno);
            }
            abort();
        }
    }
    fsync(jq->child_input_fd);
}
