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

#include <config.h>

/* For pread/pwrite and utimensat */
#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <alloca.h>

#include <fuse.h>
#include <fuse_opt.h>

#include "debug.h"
#include "jobqueue.h"
#include "misc.h"

/* SETTINGS */
static struct Settings {
    const char* progname;
    const char* mntsrc;
    const char* mntdest;

    size_t mntsrc_pathlen;

    char* cmd_template;
    int max_workers;

    int mntsrc_fd;

    JobQueue* jobqueue;
} settings;

/* PROTOTYPES */

/* Processes the virtual path to a real path. Don't free() the result. */
static const char *process_path(const char *path);

/* FUSE callbacks */
static void *queuefs_init();
static void queuefs_destroy(void *private_data);
static int queuefs_getattr(const char *path, struct stat *stbuf);
static int queuefs_fgetattr(const char *path,
                            struct stat *stbuf,
                            struct fuse_file_info *fi);
static int queuefs_readlink(const char *path, char *buf, size_t size);
static int queuefs_opendir(const char *path, struct fuse_file_info *fi);
static inline DIR *get_dirp(struct fuse_file_info *fi);
static int queuefs_readdir(const char *path,
                           void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi);
static int queuefs_releasedir(const char *path, struct fuse_file_info *fi);
static int queuefs_mkdir(const char *path, mode_t mode);
static int queuefs_unlink(const char *path);
static int queuefs_rmdir(const char *path);
static int queuefs_symlink(const char *from, const char *to);
static int queuefs_rename(const char *from, const char *to);
static int queuefs_link(const char *from, const char *to);
static int queuefs_chmod(const char *path, mode_t mode);
static int queuefs_chown(const char *path, uid_t uid, gid_t gid);
static int queuefs_truncate(const char *path, off_t size);
static int queuefs_ftruncate(const char *path,
                             off_t size,
                             struct fuse_file_info *fi);
static int queuefs_utimens(const char *path, const struct timespec tv[2]);
static int queuefs_create(const char *path,
                          mode_t mode,
                          struct fuse_file_info *fi);
static int queuefs_open(const char *path, struct fuse_file_info *fi);
static int queuefs_read(const char *path,
                        char *buf,
                        size_t size,
                        off_t offset,
                        struct fuse_file_info *fi);
static int queuefs_write(const char *path,
                         const char *buf,
                         size_t size,
                         off_t offset,
                         struct fuse_file_info *fi);
static int queuefs_statfs(const char *path, struct statvfs *stbuf);
static int queuefs_release(const char *path, struct fuse_file_info *fi);
static int queuefs_fsync(const char *path,
                         int isdatasync,
                         struct fuse_file_info *fi);

static void handle_sigusr(int signum, siginfo_t* info, void* unused);

static void print_usage(const char *progname);
static void atexit_func();
static int process_option(void *data,
                          const char *arg,
                          int key,
                          struct fuse_args *outargs);

static const char *process_path(const char *path) {
    if (path == NULL) /* possible? */
        return NULL;

    while (*path == '/')
        ++path;

    if (*path == '\0')
        return ".";
    else
        return path;
}

static void *queuefs_init() {
    assert(settings.mntsrc_fd > 0);

    DPRINTF("queuefs daemon pid is %d", (int)getpid());

    if (fchdir(settings.mntsrc_fd) != 0) {
        fprintf(stderr, "Could not change working directory to '%s': %s\n",
                settings.mntsrc, strerror(errno));
        fuse_exit(fuse_get_context()->fuse);
    }

    settings.jobqueue = jobqueue_create(settings.cmd_template, settings.max_workers);
    if (!settings.jobqueue) {
        fprintf(stderr, "Failed to create job queue.\n");
        fuse_exit(fuse_get_context()->fuse);
    }

    struct sigaction sa;
    sa.sa_sigaction = &handle_sigusr;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    return NULL;
}

static void queuefs_destroy(void *private_data) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    jobqueue_destroy(settings.jobqueue);
}

static int queuefs_getattr(const char *path, struct stat *stbuf) {
    path = process_path(path);

    if (lstat(path, stbuf) == -1)
        return -errno;
    return 0;
}

static int queuefs_fgetattr(const char *path,
                            struct stat *stbuf,
                            struct fuse_file_info *fi) {
    path = process_path(path);

    if (fstat(fi->fh, stbuf) == -1)
        return -errno;
    return 0;
}

static int queuefs_readlink(const char *path, char *buf, size_t size) {
    path = process_path(path);

    int res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}

static int queuefs_opendir(const char *path, struct fuse_file_info *fi) {
    path = process_path(path);

    DIR *dp = opendir(path);
    if (dp == NULL)
        return -errno;

    fi->fh = (unsigned long) dp;
    return 0;
}

static inline DIR *get_dirp(struct fuse_file_info *fi) {
    return (DIR *) (uintptr_t) fi->fh;
}

static int queuefs_readdir(const char *path,
                           void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi) {
    DIR *dp = get_dirp(fi);
    struct dirent *de;

    (void) path;
    seekdir(dp, offset);
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, telldir(dp)))
            break;
    }

    return 0;
}

static int queuefs_releasedir(const char *path, struct fuse_file_info *fi) {
    DIR *dp = get_dirp(fi);
    (void) path;
    closedir(dp);
    return 0;
}

static int queuefs_mkdir(const char *path, mode_t mode) {
    path = process_path(path);

    int res = mkdir(path, mode & 0777);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_unlink(const char *path) {
    path = process_path(path);

    int res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_rmdir(const char *path) {
    path = process_path(path);

    int res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_symlink(const char *from, const char *to) {
    to = process_path(to);

    int res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_rename(const char *from, const char *to) {
    from = process_path(from);
    to = process_path(to);

    int res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_link(const char *from, const char *to) {
    from = process_path(from);
    to = process_path(to);

    int res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_chmod(const char *path, mode_t mode) {
    path = process_path(path);

    if (chmod(path, mode) == -1)
        return -errno;

    return 0;
}

static int queuefs_chown(const char *path, uid_t uid, gid_t gid) {
    path = process_path(path);
    int res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_truncate(const char *path, off_t size) {
    path = process_path(path);

    int res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_ftruncate(const char *path,
                             off_t size,
                             struct fuse_file_info *fi) {
    (void) path;

    int res = ftruncate(fi->fh, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_utimens(const char *path, const struct timespec tv[2]) {
    path = process_path(path);

    int res = utimensat(settings.mntsrc_fd, path, tv, 0);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_create(const char *path,
                          mode_t mode,
                          struct fuse_file_info *fi) {
    path = process_path(path);

    int fd = open(path, fi->flags, mode & 0777);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int queuefs_open(const char *path, struct fuse_file_info *fi) {
    path = process_path(path);

    int fd = open(path, fi->flags);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int queuefs_read(const char *path,
                        char *buf,
                        size_t size,
                        off_t offset,
                        struct fuse_file_info *fi) {
    (void) path;
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int queuefs_write(const char *path,
                         const char *buf,
                         size_t size,
                         off_t offset,
                         struct fuse_file_info *fi) {
    (void) path;
    int res = pwrite(fi->fh, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int queuefs_statfs(const char *path, struct statvfs *stbuf) {
    path = process_path(path);

    int res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int queuefs_release(const char *path, struct fuse_file_info *fi) {
    assert(path != NULL);
    close(fi->fh);

    size_t mntsrc_pathlen = settings.mntsrc_pathlen;
    size_t pathlen = strlen(path);
    char* abs_path = alloca(mntsrc_pathlen + pathlen + 1);
    strcpy(abs_path, settings.mntsrc);
    strcpy(abs_path + mntsrc_pathlen, path);
    jobqueue_add_file(settings.jobqueue, abs_path);

    return 0;
}

static int queuefs_fsync(const char *path,
                         int isdatasync,
                         struct fuse_file_info *fi) {
    (void) path;
    int res;

#ifndef HAVE_FDATASYNC
    (void) isdatasync;
#else
    if (isdatasync)
    res = fdatasync(fi->fh);
    else
#endif
    res = fsync(fi->fh);
    if (res == -1)
        return -errno;

    return 0;
}

static void handle_sigusr(int signum, siginfo_t* info, void* unused)
{
    (void)unused;

    jobqueue_flush(settings.jobqueue);
    if (signum == SIGUSR2) {
        kill(info->si_pid, SIGUSR2);
    }
}

static struct fuse_operations queuefs_oper = {
    .init = queuefs_init,
    .destroy = queuefs_destroy,
    .getattr = queuefs_getattr,
    .fgetattr = queuefs_fgetattr,
    /* no access() since we always use -o default_permissions */
    .readlink = queuefs_readlink,
    .opendir = queuefs_opendir,
    .readdir = queuefs_readdir,
    .releasedir = queuefs_releasedir,
    .mkdir = queuefs_mkdir,
    .symlink = queuefs_symlink,
    .unlink = queuefs_unlink,
    .rmdir = queuefs_rmdir,
    .rename = queuefs_rename,
    .link = queuefs_link,
    .chmod = queuefs_chmod,
    .chown = queuefs_chown,
    .truncate = queuefs_truncate,
    .ftruncate = queuefs_ftruncate,
    .utimens = queuefs_utimens,
    .create = queuefs_create,
    .open = queuefs_open,
    .read = queuefs_read,
    .write = queuefs_write,
    .statfs = queuefs_statfs,
    .release = queuefs_release,
    .fsync = queuefs_fsync,
    .flag_nullpath_ok = 0  // We use the path in release()
};

static void print_usage(const char *progname) {
    if (progname == NULL)
        progname = "queuefs";

    printf("\n"
        "Usage: %s [options] dir mountpoint command\n"
        "\n"
        "The command is executed by /bin/sh with each occurrence of {}\n"
        "replaced by the absolute path to the file that was written.\n"
        "\n"
        "Information:\n"
        "  -h      --help            Print this and exit.\n"
        "  -V      --version         Print version number and exit.\n"
        "\n"
        "Options:\n"
        "  (none yet)\n"
        "\n"
        "FUSE options:\n"
        "  -o opt[,opt,...]          Mount options.\n"
        "  -r      -o ro             Mount strictly read-only.\n"
        "  -d      -o debug          Enable debug output (implies -f).\n"
        "  -f                        Foreground operation.\n"
        "  -s                        Disable multithreaded operation.\n"
        "\n", progname);
}

static void atexit_func() {
}

enum OptionKey {
    OPTKEY_NONOPTION = -2,
    OPTKEY_UNKNOWN = -1,
    OPTKEY_HELP,
    OPTKEY_VERSION
};

static int process_option(void *data,
                          const char *arg,
                          int key,
                          struct fuse_args *outargs) {
    switch ((enum OptionKey) key) {
    case OPTKEY_HELP:
        print_usage(my_basename(settings.progname));
        exit(0);

    case OPTKEY_VERSION:
        printf("%s\n", PACKAGE_STRING);
        exit(0);

    case OPTKEY_NONOPTION:
        if (!settings.mntsrc) {
            settings.mntsrc = arg;
            settings.mntsrc_pathlen = strlen(arg);
        } else if (!settings.mntdest) {
            settings.mntdest = arg;
        } else if (!settings.cmd_template) {
            settings.cmd_template = strdup(arg);
        } else {
            char* old = settings.cmd_template;
            size_t old_len = strlen(old);
            size_t arg_len = strlen(arg);
            char* new = malloc(old_len + 1 + arg_len + 1);
            strcpy(new, old);
            new[old_len] = ' ';
            strcpy(new + old_len + 1, arg);
            free(old);
            settings.cmd_template = new;
        }
        return 0;

    default:
        return 1;
    }
}

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* Fuse's option parser will store things here. */
    static struct OptionData {
        int no_allow_other;
    } od = { 0 };

#define OPT2(one, two, key) \
            FUSE_OPT_KEY(one, key), \
            FUSE_OPT_KEY(two, key)
#define OPT_OFFSET2(one, two, offset, key) \
            {one, offsetof(struct OptionData, offset), key}, \
            {two, offsetof(struct OptionData, offset), key}
#define OPT_OFFSET3(one, two, three, offset, key) \
            {one, offsetof(struct OptionData, offset), key}, \
            {two, offsetof(struct OptionData, offset), key}, \
            {three, offsetof(struct OptionData, offset), key}
    static const struct fuse_opt options[] = {
        OPT2("-h", "--help", OPTKEY_HELP),
        OPT2("-V", "--version", OPTKEY_VERSION),
        FUSE_OPT_END
    };

    /* Initialize settings */
    settings.progname = argv[0];
    settings.mntsrc = NULL;
    settings.mntdest = NULL;
    settings.mntsrc_pathlen = -1;
    settings.cmd_template = NULL;
    settings.max_workers = 100;
    settings.jobqueue = NULL;
    atexit(&atexit_func);

    /* Parse options */
    if (fuse_opt_parse(&args, &od, options, &process_option) == -1)
        return 1;

    /* Check that required arguments were given */
    if (!settings.mntsrc || !settings.mntdest || !settings.cmd_template) {
        print_usage(my_basename(argv[0]));
        return 1;
    }

    /* Add default fuse options */
    if (!od.no_allow_other) {
        fuse_opt_add_arg(&args, "-oallow_other");
    }

    /* We want the kernel to do our access checks for us based on what getattr gives it. */
    fuse_opt_add_arg(&args, "-odefault_permissions");

    /* By default we don't mind if there are old jobs in queue. */
    fuse_opt_add_arg(&args, "-ononempty");

    fuse_opt_add_arg(&args, settings.mntdest);

    /* Open mount source for chrooting in queuefs_init */
    settings.mntsrc_fd = open(settings.mntsrc, O_RDONLY);
    if (settings.mntsrc_fd == -1) {
        fprintf(stderr, "Could not open source directory\n");
        return 1;
    }

    /* Ignore mounter's umask */
    umask(0);

    int fuse_main_return = fuse_main(args.argc, args.argv, &queuefs_oper, NULL);

    fuse_opt_free_args(&args);
    close(settings.mntsrc_fd);
    return fuse_main_return;
}
