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

#ifndef INC_QUEUEFS_JOBQUEUE_H
#define INC_QUEUEFS_JOBQUEUE_H

struct JobQueue;
typedef struct JobQueue JobQueue;

typedef struct JobQueueSettings {
    const char* cmd_template;
    int max_workers;
    int retry_wait_ms;
} JobQueueSettings;


/*
 * cmd_template must be a NULL-terminated array where the substring "{}"
 * will be replaced with the shell-quoted file name.
 */
JobQueue* jobqueue_create(const JobQueueSettings* settings);

/*
 * Adds a file to be processed in the background when a worker becomes available.
 *
 * This function is thread-safe.
 */
void jobqueue_add_file(JobQueue* jq, const char* path);

/*
 * Waits for the job queue to run all currently queued jobs at least once.
 * This is defined like this to account for failing jobs.
 */
void jobqueue_flush(JobQueue* jq);

/*
 * Destroys a job queue and kills its manager process.
 * Child processes get a SIGHUP.
 */
int jobqueue_destroy(JobQueue* jq); /* Returns status code */

#endif /* INC_QUEUEFS_JOBQUEUE_H */
