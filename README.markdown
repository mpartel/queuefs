
# queuefs #

queuefs is a FUSE filesystem that acts as a work queue. It executes a command on all files written to it.
More precisely:

  * When a file is written and closed, the command is executed on it
    in a concurrent child process.
  * The command may be successful (return status 0) or unsuccessful.
  * If the command was successful then the file is left as-is, moved to a
    success directory [TBD] or deleted [TBD].
  * [TBD] If the command was unsuccessful then the command is retried after
    a delay of that defaults to 30 seconds. After n tries, the file may
    be moved to a fail directory.

Files are stored in the directory queuefs is mounted on and are visible through the mount.

NOTE: while the basic functionality is there, most options are still missing. Also while the implementation seems sound to me, concurrency and memory bugs are certainly not out of the question.

## Installation ##

Dependencies:

  * fuse 2.8.0 or above (http://fuse.sf.net/).
  * glib 2.26.0 or above.

Compile and install as usual:

    ./configure
    make
    make install

If you want the mounts made by non-root users to be visible to other users, you may have to add the line user_allow_other to /etc/fuse.conf.


## Usage ##

See the queuefs --help or the man-page for instructions and examples.

## TODO ##

  * man page
  * --hide-all
  * --delete-on-start
  * --delete-on-finish
  * --move-on-finish successdir/
  * --retry-delay n
  * ...

## License ##

The MIT License. See the file LICENSE.
