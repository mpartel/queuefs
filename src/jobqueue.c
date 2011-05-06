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
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

struct JobQueue {
    JobQueueSettings settings;
    pthread_mutex_t mutex;
    pid_t child_pid;
    int child_stdin;
};


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
    jq->settings.cmd_template = cmd_template;
    jq->settings.max_workers = max_workers;
    pthread_mutex_init(&jq->mutex, NULL);
    jq->child_stdin = pipefd[1];

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid == 0) {
        DPRINT("Job queue process forked");
        close(pipefd[1]);
        jobqueue_process_main(&jq->settings, pipefd[0]);
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

