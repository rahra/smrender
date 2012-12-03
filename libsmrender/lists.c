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

/*! This files contains the code to handle generic double-linked lists.
 *  
 *
 *  @author Bernhard R. Fischer
 */
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "lists.h"


#ifdef TEST_LISTS
#include <stdio.h>
#define MALLOC_CNT(x) malloc_cnt_ += (x)
static int malloc_cnt_ = 0;
#else
#define MALLOC_CNT(x)
#endif


/* Create a new empty lists. This list is implemented as cyclic list.
 * @return Pointer to list_t. Must be freed again with li_destroy().
 */
list_t *li_new(void)
{
   list_t *li;

   if ((li = malloc(sizeof(list_t))) == NULL)
      return NULL;
   MALLOC_CNT(1);

   li->next = li;
   li->prev = li;
   li->data = NULL;

   return li;
}


/*! Delete element from list. This function does not unlink element form
 * concatenated list, i.e. call li_unlink() before deleting element, otherwise
 * chained pointers are corrupted.
 *  @param li Pointer to list_t element which should be deleted.
 *  @param free_data Pointer to function which is called to free the data
 *  within the element. If it is set to NULL it is not called.
 */
void li_del(list_t *li, void(*free_data)(void*))
{
   if (li->data != NULL && free_data != NULL)
      free_data(li->data);
   free(li);
   MALLOC_CNT(-1);
}


/*! Destroy list. All elements are free'd, optionally including the data
 * pointer. This function calls li_del() for each element including the root
 * element.
 * @param first pointer to list.
 * @param Optional pointer to function which is called to free the data.
 */
void li_destroy(list_t *first, void(*free_data)(void*))
{
   list_t *next, *li;

   for (li = first->next; li != first ; li = next)
   {
      next = li->next;
      li_del(li, free_data);
   }
   li_del(first, NULL);
}


/* Add element to list.
 * @param list Pointer to list_t.
 * @param p Pointer to the data of the element.
 * @return Returns pointer to new element.
 */
list_t *li_add(list_t *list, void *p)
{
   list_t *li;

   // safety check
   if (list == NULL || list->next == NULL)
   {
      errno = EFAULT;
      return NULL;
   }

   if ((li = malloc(sizeof(*li))) == NULL)
      return NULL;
   MALLOC_CNT(1);

   li->data = p;

   li->next = list->next;
   li->prev = list;
   list->next->prev = li;
   list->next = li;

   return li;
}


/*! Return next list element.
 */
list_t *li_next(const list_t *list)
{
   return list->next;
}


/*! Remove element from list. The element is not free'd.
 */
void li_unlink(list_t *list)
{
   list->prev->next = list->next;
   list->next->prev = list->prev;
}


/* Return pointer to first element.
 * @param list Pointer to root of list (as returned by li_new()).
 */
list_t *li_first(const list_t *list)
{
   return list->next;
}


/* Return pointer to last element.
 * @param list Pointer to root of list (as returned by li_new()).
 */
list_t *li_last(const list_t *list)
{
   return list->prev;
}

/* Return pointer to 'this' element.
 * @param list Pointer to root of list (as returned by li_new()).
 */
list_t *li_head(list_t *list)
{
   return list;
}


#ifdef TEST_LISTS

int main(int argc, char **argv)
{
   list_t *root, *elem;

   if ((root = li_new()) == NULL)
      perror("li_new()"), exit(EXIT_FAILURE);

   li_add(root, "Hello");
   li_add(root, "World");
   li_add(root, "!");

   for (elem = li_first(root); elem != li_head(root); elem = elem->next)
      printf("%s\n", (char*) elem->data);

   elem = li_next(root);
   li_unlink(elem);
   li_del(elem, NULL);

   for (elem = li_last(root); elem != li_head(root); elem = elem->prev)
      printf("%s\n", (char*) elem->data);

   elem = li_next(root);
   li_unlink(elem);
   li_del(elem, NULL);

   for (elem = li_last(root); elem != li_head(root); elem = elem->prev)
      printf("%s\n", (char*) elem->data);

   li_destroy(root, NULL);

   printf("malloc_cnt_ = %d\n", malloc_cnt_);

   return 0;
}

#endif

