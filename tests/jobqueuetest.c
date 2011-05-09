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

#define QUEUEFS_DISABLE_DEBUG 1 // Comment out to get some debugging output

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "misc.c"
#include "jobqueue.c"
#include "jobqueue_process.c"

static struct stat global_stat_struct;

#define TESTFILE(name) "/tmp/queuefs_test_file_" name

#define CHECK(prop) if (!(prop)) { fprintf(stderr, "Failure: '%s'\n", #prop); exit(1); }

#define CHECK_FILE_EXISTS(name) CHECK(stat(name, &global_stat_struct) == 0)
#define CHECK_FILE_NOT_EXISTS(name) CHECK(stat(name, &global_stat_struct) != 0 && errno == ENOENT)

#if !QUEUEFS_DISABLE_DEBUG
#define ECHO "echo {}"
#else
#define ECHO "true"
#endif

static void checked_jobqueue_destroy(JobQueue* jq) {
    int destroy_status = jobqueue_destroy(jq);
    if (destroy_status) {
        fprintf(stderr, "jobqueue_destroy returned %d\n", destroy_status);
        exit(1);
    }
}

static void simple() {
    JobQueueSettings jqs;
    jqs.cmd_template = "sleep 0.1 && " ECHO " && rm -f {} && touch {}";
    jqs.max_workers = 2;
    jqs.retry_wait_ms = 1;

    JobQueue* jq = jobqueue_create(&jqs);
    CHECK(jq);

    

    jobqueue_flush(jq);

    jobqueue_add_file(jq, TESTFILE("1"));
    jobqueue_add_file(jq, TESTFILE("2"));
    jobqueue_add_file(jq, TESTFILE("3"));

    jobqueue_flush(jq);

#define CHECK_EXISTS(name) CHECK_FILE_EXISTS(TESTFILE(name))
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
#undef CHECK_EXISTS

    for (int i = 1; i <= 6; ++i) {
        char buf[1000];
        snprintf(buf, 1000, TESTFILE("%d"), i);
        unlink(buf);
    }
    unlink(TESTFILE("with spaces in name"));

    checked_jobqueue_destroy(jq);
}

static void rerunning() {
    JobQueueSettings jqs;
    jqs.cmd_template = "test -f {} && rm -f {}";
    jqs.max_workers = 2;
    jqs.retry_wait_ms = 1;

    JobQueue* jq = jobqueue_create(&jqs);
    CHECK(jq);

    const char* filename = TESTFILE("xoo");
    unlink(filename);

    jobqueue_add_file(jq, filename);
    jobqueue_flush(jq);

    FILE* f = fopen(filename, "wb");
    CHECK(f);
    fclose(f);

    jobqueue_flush(jq);
    CHECK_FILE_NOT_EXISTS(filename);

    checked_jobqueue_destroy(jq);
}

int main() {
    simple();
    rerunning();
}
