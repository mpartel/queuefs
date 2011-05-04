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

#ifndef INC_QUEUEFS_MISC_H
#define INC_QUEUEFS_MISC_H

/* Returns a pointer to the first character after the
   final slash of path, or path itself if it contains no slashes.
   If the path ends with a slash, then the result is an empty
   string.
   Returns NULL if path is NULL. */
const char *my_basename(const char *path);

#endif
