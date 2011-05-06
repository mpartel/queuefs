
#include <stdio.h>
#include <unistd.h>
#include "misc.c"
#include "jobqueue.c"
#include "jobqueue_process.c"

int main() {
    const char* cmd_template[] = {"/bin/sh", "-c", "echo {} && sleep 1", NULL};
    JobQueue* jq = jobqueue_create(cmd_template, 2);
    if (!jq) {
        fprintf(stderr, "Failed to create job queue\n");
        return 1;
    }
    jobqueue_add_file(jq, "hello");
    jobqueue_add_file(jq, "world");
    jobqueue_add_file(jq, "hello2");
    jobqueue_add_file(jq, "world2");
    jobqueue_add_file(jq, "hello3");
    jobqueue_add_file(jq, "world3");
    sleep(1);
    int destroy_status = jobqueue_destroy(jq);
    if (destroy_status) {
        fprintf(stderr, "jobqueue_destroy returned %d\n", destroy_status);
        return 2;
    }
    return 0;
}
