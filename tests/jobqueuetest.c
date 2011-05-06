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

#define QUEUEFS_DISABLE_DEBUG 1 // Comment out to get some debugging output

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "misc.c"
#include "jobqueue.c"
#include "jobqueue_process.c"

#define TESTFILE(name) "/tmp/queuefs_test_file_" name

#define CHECK(prop) if (!(prop)) { fprintf(stderr, "Failure: '%s'\n", #prop); exit(1); }

int main() {
#if !QUEUEFS_DISABLE_DEBUG
#define ECHO "echo {}"
#else
#define ECHO "true"
#endif
    const char* cmd_template[] = {"/bin/sh", "-c", "sleep 0.2 && " ECHO " && rm -f {} && touch {}", NULL};

    JobQueue* jq = jobqueue_create(cmd_template, 2);
    if (!jq) {
        fprintf(stderr, "Failed to create job queue\n");
        return 1;
    }

    struct stat st;

    jobqueue_flush(jq);

    jobqueue_add_file(jq, TESTFILE("1"));
    jobqueue_add_file(jq, TESTFILE("2"));
    jobqueue_add_file(jq, TESTFILE("3"));

    jobqueue_flush(jq);

#define CHECK_EXISTS(name) CHECK(stat(TESTFILE(name), &st) == 0)
    CHECK_EXISTS("1")
    CHECK_EXISTS("2")
    CHECK_EXISTS("3")

    jobqueue_add_file(jq, TESTFILE("4"));
    jobqueue_add_file(jq, TESTFILE("5"));
    jobqueue_add_file(jq, TESTFILE("6"));

    jobqueue_add_file(jq, TESTFILE("with spaces in name"));

    jobqueue_flush(jq);

    CHECK_EXISTS("4")
    CHECK_EXISTS("5")
    CHECK_EXISTS("6")
    CHECK_EXISTS("with spaces in name")

    for (int i = 1; i <= 6; ++i) {
        char buf[1000];
        snprintf(buf, 1000, TESTFILE("%d"), i);
        unlink(buf);
    }
    unlink(TESTFILE("with spaces in name"));

    int destroy_status = jobqueue_destroy(jq);
    if (destroy_status) {
        fprintf(stderr, "jobqueue_destroy returned %d\n", destroy_status);
        return 2;
    }

    return 0;
}
