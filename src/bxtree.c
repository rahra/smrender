/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "smrender.h"
#include "bxtree.h"


static size_t mem_usage_ = 0;
static long malloc_cnt_ = 0;


size_t bx_sizeof(void)
{
   return mem_usage_;
}


void bx_exit(void)
{
   static short ae = 0;

   switch (ae)
   {
      case 0:
#ifdef USE_ATEXIT
         if (atexit(bx_exit))
            log_msg(LOG_ERR, "atexit(bx_exit) failed");
#endif
         break;

      default:
         log_msg(LOG_DEBUG, "tree memory: %ld kByte, malloc_cnt_ = %ld", bx_sizeof() / 1024, malloc_cnt_);
   }
   ae++;
}


bx_node_t *bx_malloc(void)
{
   bx_node_t *node;

   if ((node = calloc(1, sizeof(bx_node_t))) == NULL)
      log_msg(LOG_ERR, "calloc() failed in bx_malloc(): %s", strerror(errno)),
         exit(EXIT_FAILURE);

#ifdef MEM_USAGE
      mem_usage_ += sizeof(bx_node_t);
      malloc_cnt_++;
#endif
   return node;
}


void bx_free(bx_node_t *node)
{
   free(node);
#ifdef MEM_USAGE
   //mem_usage_ -= sizeof(bx_node_t);
   malloc_cnt_--;
#endif
}


/*!
 * @param node double pointer to current node.
 * @param h hash value to store.
 * @param d curren depth of tree.
 * @return pointer to added node.
 */
bx_node_t *bx_add_node0(bx_node_t **node, bx_hash_t h, bx_hash_t d)
{
   // create new empty node if 'this' doesn't exist
   if (*node == NULL)
      *node = bx_malloc();

   // node found at end of depth (break recursion)
   if (d >= ((sizeof(bx_hash_t) * 8) / BX_RES))
      return *node;

   // otherwise recurse
   return bx_add_node0((bx_node_t**) &((*node)->next[BT_MASK(h, d)]), h, d + 1);
}


bx_node_t *bx_get_node0(bx_node_t *node, bx_hash_t h, bx_hash_t d)
{
   // break recursion at end of depth
   if (d >= ((sizeof(bx_hash_t) * 8) / BX_RES))
      return node;

   // test for null pointer and recurse if applicable
   return node ? bx_get_node0(node->next[BT_MASK(h, d)], h, d + 1) : node;
}


void bx_free_tree0(bx_node_t *node, bx_hash_t d)
{
   int i;

   if (node == NULL)
      return;

   if (d < ((sizeof(bx_hash_t) * 8) / BX_RES))
      for (i = 0; i < 1 << BX_RES; i++)
         bx_free_tree0(node->next[i], d + 1);

   bx_free(node);
}

