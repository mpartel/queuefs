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

#define TESTFILE(name) "/tmp/queuefs_test_file_" name

#define CHECK(prop) if (!(prop)) { fprintf(stderr, "Failure: '%s'\n", #prop); exit(1); }

int main() {
#if !QUEUEFS_DISABLE_DEBUG
#define ECHO "echo {}"
#else
#define ECHO "true"
#endif
    const char* cmd_template = "sleep 0.1 && " ECHO " && rm -f {} && touch {}";

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
