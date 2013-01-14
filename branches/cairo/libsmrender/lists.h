/* Copyright 2012 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! This is the header file for lists.c.
 *
 *  @author Bernhard R. Fischer
 */
#ifndef LISTS_H
#define LISTS_H

  
typedef struct list list_t;

struct list
{
   list_t *next, *prev;
   void *data;
};


list_t *li_new(void);
void li_destroy(list_t*, void(*)(void*));
void li_del(list_t*, void(*)(void*));
list_t *li_add(list_t*, void*);
list_t *li_next(const list_t*);
list_t *li_first(const list_t *);
list_t *li_last(const list_t *);
list_t *li_head(list_t *);
void li_unlink(list_t *);


#endif

