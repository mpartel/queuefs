/***********************************************************************************/
/*  Copyright (c) 2011 Martin Pärtel <martin.partel@gmail.com>                    */
/*                                                                                 */
/*  Permission is hereby granted, free of charge, to any person obtaining a copy   */
/*  of this software and associated documentation files (the "Software"), to deal  */
/*  in the Software without restriction, including without limitation the rights   */
/*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      */
/*  copies of the Software, and to permit persons to whom the Software is          */
/*  furnished to do so, subject to the following conditions:                       */
/*                                                                                 */
/*  The above copyright notice and this permission notice shall be included in     */
/*  all copies or substantial portions of the Software.                            */
/*                                                                                 */
/*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     */
/*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       */
/*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    */
/*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         */
/*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  */
/*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN      */
/*  THE SOFTWARE.                                                                  */
/*                                                                                 */
/***********************************************************************************/

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
#include <sys/time.h>
#include <signal.h>
#include <alloca.h>

#include <glib.h>


typedef struct WorkUnit {
    char* path;
    pid_t worker_pid;

    int attempts;
    int last_exit_code;
    struct timeval next_execution_time;
} WorkUnit;

static const JobQueueSettings* settings;
static int input_fd;
static int output_fd;

static int readbuf_capacity;
static int readbuf_size;
static char* readbuf;

// The following may only be accessed while SIGCHLD is blocked
// since it is also accessed in the SIGCHLD handler.
static long long workers_started_ever;
static long long workers_waited_ever;
static int active_workers;
static GHashTable* active_work_units; // of pid to WorkUnit*
static GTree* work_queue;             // of WorkUnit*

static sigset_t sigchld_set;

/*
 * Calls wait_away_finished_workers() and start_queued_work().
 */
static void handle_sigchld(int signum);
static void register_sigchld_handler();

static int process_input();
static void handle_incoming_command(const char* buf);
static int take_from_readbuf(GByteArray* buf); // returns 1 if encountered '\0'

static void wait_away_finished_workers();
static bool wait_away_worker(bool nohang);
static void start_queued_work(bool nodelay);
static void start_worker(WorkUnit* unit);
static gchar* make_command(const char* file_path);

static void free_work_unit(gpointer unit);
static gint compare_work_unit(gconstpointer a, gconstpointer b, gpointer data);
static gboolean traverse_get_first_key(gpointer key, gpointer value, gpointer dest);
static bool wait_for_sigchld(long ms_to_wait);


void jobqueue_process_main(JobQueueSettings* settings_, int input_fd_, int output_fd_) {
    settings = settings_;

    input_fd = input_fd_;
    output_fd = output_fd_;
    readbuf_capacity = 4096;
    readbuf_size = 0;
    readbuf = alloca(readbuf_capacity);

    workers_started_ever = 0;
    workers_waited_ever = 0;
    active_workers = 0;
    active_work_units = g_hash_table_new_full(&g_direct_hash,
                                              &g_direct_equal,
                                              NULL,
                                              &free_work_unit);

    work_queue = g_tree_new_full(&compare_work_unit,
                                 NULL,
                                 NULL,
                                 &free_work_unit);

    sigemptyset(&sigchld_set);
    sigaddset(&sigchld_set, SIGCHLD);

    register_sigchld_handler();

    while (1) {
        if (!process_input()) {
            break;
        }

        sigprocmask(SIG_BLOCK, &sigchld_set, NULL);

        if (g_tree_nnodes(work_queue) > 0) {
            if (active_workers < settings->max_workers) {
                start_queued_work(true);
            } else {
                DPRINT("No more worker slots - work is left queued");
            }
        }

        sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
    }

    // Live children will be inherited by the init process
    sigprocmask(SIG_BLOCK, &sigchld_set, NULL);

    DPRINT("Job queue process cleaning up");
    g_tree_destroy(work_queue);
    g_hash_table_destroy(active_work_units);
    close(input_fd);
}

static void handle_sigchld(int signum) {
    (void)signum;
    wait_away_finished_workers();
    start_queued_work(true);
}

static void register_sigchld_handler() {
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

static int process_input() {
    GByteArray* buf = g_byte_array_new();

    DPRINT("Reading work unit from parent process");
    while (1) {
        if (take_from_readbuf(buf)) {
            // If we go here, we found the null byte that separates commands.
            // Some data may have been left in readbuf.
            break;
        }
        assert(readbuf_size == 0);

        DPRINT("Buffering input from parent process");
        ssize_t ret = read(input_fd, readbuf, readbuf_capacity);
        DPRINTF("read() from parent process returned %d bytes", ret);
        if (ret == -1 || ret == 0) { // error or eof
            DPRINT("Pipe from parent process was closed");
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

    sigset_t oldmask;
    sigprocmask(SIG_BLOCK, &sigchld_set, &oldmask);
    
    if (g_str_has_prefix(buf, "EXEC ")) {
        WorkUnit* unit = g_malloc(sizeof(WorkUnit));
        unit->path = g_strdup(buf + strlen("EXEC "));
        unit->worker_pid = -1;
        gettimeofday(&unit->next_execution_time, NULL);
        unit->attempts = 0;
        unit->last_exit_code = -1;
        g_tree_insert(work_queue, unit, unit);
    } else if (g_str_equal(buf, "FLUSH")) {
        DPRINT("Handling FLUSH command");
        
        long long terminations_expected = workers_started_ever + g_tree_nnodes(work_queue);
        while (workers_waited_ever < terminations_expected) {
            if (active_workers == 0) {
                start_queued_work(false);
            }
            DPRINTF("QUEUED: %d   ACTIVE: %d   EVER:  %lld / %lld",
                    g_tree_nnodes(work_queue), active_workers,
                    workers_waited_ever, workers_started_ever);
            DPRINT("Waiting for SIGCHLD");
            sigsuspend(&oldmask);
            // start_queued_work() is called by SIGCHLD handler automatically
        }

        while (true) {
            if (write(output_fd, "1", 1) == 1) {
                break;
            }
        }
    }
    
    sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
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
        gpointer key = GINT_TO_POINTER(pid);
        WorkUnit* unit = g_hash_table_lookup(active_work_units, key);
        g_hash_table_steal(active_work_units, key);
        active_workers--;
        workers_waited_ever++;

        int code = wait_status_to_code(status);
        if (code == 0) {
            DPRINTF("Work unit finished successfully: %s", unit->path);
            // Could move or delete the file or something
            free_work_unit(unit);
        } else {
            DPRINTF("Work unit failed: %s (%d)", unit->path, code);
            unit->attempts++;
            unit->last_exit_code = code;
            gettimeofday(&unit->next_execution_time, NULL);
            timeval_add_ms(&unit->next_execution_time, settings->retry_wait_ms);
            g_tree_insert(work_queue, unit, unit);
        }

        ret = true;
    }

    return ret;
}

static void start_queued_work(bool wait) {
    WorkUnit* unit = NULL;
    g_tree_foreach(work_queue, &traverse_get_first_key, &unit);
    if (unit) {
        bool can_execute = true;
        if (wait) {
            long ms_to_wait = ms_to_timeval(&unit->next_execution_time);
            if (ms_to_wait > 0) {
                can_execute = !wait_for_sigchld(ms_to_wait);
            }
        }
        g_tree_steal(work_queue, unit);
        start_worker(unit);
    }
}

static void start_worker(WorkUnit* unit) {
    DPRINTF("Starting worker for '%s'", unit->path);

    const char* shell = "/bin/sh";
    gchar* cmd = make_command(unit->path);
    DPRINTF("Command: %s", cmd);

    pid_t pid = fork();
    if (pid == 0) {
        sigprocmask(SIG_UNBLOCK, &sigchld_set, NULL);
        execlp(shell, shell, "-c", cmd, NULL);
        _exit(1);
    }

    g_free(cmd);

    unit->worker_pid = pid;

    g_hash_table_insert(active_work_units, GINT_TO_POINTER(pid), unit);
    active_workers++;
    workers_started_ever++;
}

static gchar* make_command(const char* file_path) {
    char* quoted_path = g_shell_quote(file_path);
    gchar** parts = g_strsplit(settings->cmd_template, "{}", 0);
    gchar* cmd = g_strjoinv(quoted_path, parts);
    g_strfreev(parts);
    g_free(quoted_path);
    return cmd;
}

static void free_work_unit(gpointer unit) {
    g_free(((WorkUnit*)unit)->path);
    g_free(unit);
}

static gint compare_work_unit(gconstpointer a, gconstpointer b, gpointer data) {
    WorkUnit* wu1 = (WorkUnit*)a;
    WorkUnit* wu2 = (WorkUnit*)b;
    struct timeval* tv1 = &wu1->next_execution_time;
    struct timeval* tv2 = &wu2->next_execution_time;
    if (tv1->tv_sec == tv2->tv_sec) {
        if (tv1->tv_usec == tv2->tv_usec) {
            return wu1->worker_pid - wu2->worker_pid;
        } else {
            return tv1->tv_usec - tv2->tv_usec;
        }
    } else {
        return tv1->tv_sec - tv2->tv_sec;
    }
}

static gboolean traverse_get_first_key(gpointer key, gpointer value, gpointer dest) {
    *(gpointer*)dest = key;
    return TRUE;
}

static bool wait_for_sigchld(long ms_to_wait) {
    //FIXIME: this is no good - we can't accept new jobs while waiting
    struct timespec ts;
    ts.tv_sec = ms_to_wait / 1000;
    ts.tv_nsec = (ms_to_wait % 1000) * 1000000;
    DPRINTF("Waiting for %ld ms or SIGCHLD", ms_to_wait);
    if (sigtimedwait(&sigchld_set, NULL, &ts) == -1) {
        return true;
    } else {
        return false;
    }
}
