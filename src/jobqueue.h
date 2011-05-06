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

#ifndef INC_QUEUEFS_JOBQUEUE_H
#define INC_QUEUEFS_JOBQUEUE_H

struct JobQueue;
typedef struct JobQueue JobQueue;

/*
 * cmd_template must be a NULL-terminated array where elements equal to "{}"
 * will be replaced with the file name.
 */
JobQueue* jobqueue_create(const char** cmd_template, int max_workers);

/*
 * Adds a file to be processed in the background when a worker becomes available.
 *
 * This function is thread-safe.
 */
void jobqueue_add_file(JobQueue* jq, const char* path);

/*
 * Waits for the job queue to become empty.
 */
void jobqueue_flush(JobQueue* jq);

/*
 * Destroys a job queue and kills its manager process.
 * Child processes get a SIGHUP.
 */
int jobqueue_destroy(JobQueue* jq); /* Returns status code */

#endif /* INC_QUEUEFS_JOBQUEUE_H */
