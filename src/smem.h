/* Copyright 2012-2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file smem.h
 * This file contains the definitions for the memory handling stuff.
 *
 * \author Bernhard R. Fischer, <bf@abenteuerland.at>
 * \date 2024/01/07
 */
#ifndef SMEM_H
#define SMEM_H

#define DEF_PAGESIZE (4096 - sizeof(sm_memlist_t))
#define SM_PAGES(x) ((x) / (page_size_ + 1) + 1)


typedef struct sm_memlist sm_memlist_t;
typedef struct sm_mem sm_mem_t;


struct sm_memlist
{
   size_t size;
   struct sm_memlist *next, *prev;
};

struct sm_memblock
{
   struct sm_memblock *next;
   int size;
   void *addr;
};

struct sm_mem
{
   struct sm_memblock *alloc_list;
   struct sm_memblock *free_list;
};


void *sm_alloc(int );
void *sm_calloc(int );
void sm_free(void *);
char *sm_strdup(const char *);

#endif

