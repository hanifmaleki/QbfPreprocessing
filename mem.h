/*
 This file is part of qbce-prepro.

 Copyright 2018 
 Florian Lonsing, Vienna University of Technology, Austria.

 qbce-prepro is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or (at
 your option) any later version.

 qbce-prepro is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with qbce-prepro.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MEM_H_INCLUDED
#define MEM_H_INCLUDED

#include <stddef.h>

struct MemMan
{
  size_t cur_allocated;
  size_t max_allocated;
  size_t limit;
};

typedef struct MemMan MemMan;

MemMan *mm_create ();

void mm_delete (MemMan * mm);

void *mm_malloc (MemMan * mm, size_t size);

void *mm_realloc (MemMan * mm, void *ptr, size_t old_size,
                     size_t new_size);

void mm_free (MemMan * mm, void *ptr, size_t size);

size_t mm_max_allocated (MemMan * mm);

size_t mm_cur_allocated (MemMan * mm);

void mm_set_mem_limit (MemMan * mm, size_t limit);

size_t mm_get_mem_limit (MemMan * mm);

#endif
