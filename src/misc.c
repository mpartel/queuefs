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

#include "misc.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

const char *my_basename(const char* path)
{
    const char* p;

    if (path == NULL)
        return NULL;

    p = strrchr(path, '/');
    if (p != NULL)
        return p + 1;
    else
        return path;
}

