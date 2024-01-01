/* Copyright 2011-2012 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! \file bxtree.c
 * This file contains to code for the (binary) tree. All objects are stored
 * within this tree. Rules are stored as well in a separate tree.
 *
 * @author Bernhard R. Fischer, <bf@abenteuerland.at>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "smrender.h"
#include "bxtree.h"

#ifdef WITH_THREADS
#include <pthread.h>
static pthread_rwlock_t rwlock_ = PTHREAD_RWLOCK_INITIALIZER;
#endif

static size_t mem_alloc_ = 0, mem_free_ = 0;
static long malloc_cnt_ = 0;


/*! Return the number of bytes currently allocated by the tree.
 * @return Number of bytes allocated.
 */
size_t bx_sizeof(void)
{
   return mem_alloc_ - mem_free_;
}


/*! This function outputs the current amount of memory at program exit.
 */
void __attribute__((destructor)) bx_exit(void)
{
   log_msg(LOG_DEBUG, "tree memory: %ld kByte, malloc_cnt_ = %ld, mem_alloc_ = %ld, mem_free_ = %ld",
         bx_sizeof() / 1024, malloc_cnt_, mem_alloc_, mem_free_);
}


/*! Allocate and initialize memory for a tree node.
 * @return This function always returns a valid pointer to a new node. If no
 * memory is available the functions calls exit(3).
 */
bx_node_t *bx_malloc(void)
{
   bx_node_t *node;

   if ((node = calloc(1, sizeof(bx_node_t))) == NULL)
      log_msg(LOG_ERR, "calloc() failed in bx_malloc(): %s", strerror(errno)),
         exit(EXIT_FAILURE);

#define MEM_USAGE
#ifdef MEM_USAGE
      mem_alloc_ += sizeof(bx_node_t);
      malloc_cnt_++;
#endif
   return node;
}


/*! Free the memory of the given node.
 * @param node Pointer of node to free.
 */
void bx_free(bx_node_t *node)
{
   free(node);
#ifdef MEM_USAGE
   mem_free_ -= sizeof(bx_node_t);
   malloc_cnt_--;
#endif
}


/*! This funmction recursively adds a new node to the tree at the given id h.
 * This function is not thread-safe, use bx_add_node0() instead.
 * @param node double pointer to current node.
 * @param h hash value to store.
 * @param d curren depth of tree.
 * @return pointer to added node.
 */
bx_node_t *bx_add_node1(bx_node_t **node, bx_hash_t h, bx_hash_t d)
{
   // create new empty node if 'this' doesn't exist
   if (*node == NULL)
      *node = bx_malloc();

   // node found at end of depth (break recursion)
   if (d >= (((int) sizeof(bx_hash_t) * 8) / BX_RES))
      return *node;

   // otherwise recurse
   return bx_add_node1((bx_node_t**) &((*node)->next[BT_MASK(h, d)]), h, d + 1);
}


/*! This function adds a new node to the tree taking core on thread locking.
 * Internally it calls bx_add_node1().
 * @param node Double pointer to current node.
 * @param h Hash value to store.
 * @param d Curren depth of tree starting with 0 at the root.
 * @return Pointer to added node.
 */
bx_node_t *bx_add_node0(bx_node_t **node, bx_hash_t h, bx_hash_t d)
{
#ifdef WITH_THREADS
   bx_node_t *nd;

   pthread_rwlock_wrlock(&rwlock_);
   nd = bx_add_node1(node, h, d);
   pthread_rwlock_unlock(&rwlock_);
   return nd;
#else
   return bx_add_node1(node, h, d);
#endif
}


/*! This function recursively retrieves the tree node with the id h. This
 * function does not take thread-locking into account.
 * @param node Pointer to tree node to start search for.
 * @param h Hash value (id) of node to get.
 * @param d Current search depth starting with 0 at the root.
 * @return Returns a pointer to the desired node.
 */
bx_node_t *bx_get_node1(bx_node_t *node, bx_hash_t h, bx_hash_t d)
{
   // break recursion at end of depth
   if (d >= (((int) sizeof(bx_hash_t) * 8) / BX_RES))
      return node;

   // test for null pointer and recurse if applicable
   return node ? bx_get_node1(node->next[BT_MASK(h, d)], h, d + 1) : node;
}


/*! This function recursively retrieves the tree node with the id h. This
 * function does takes care on thread-locking. Internally it calls
 * bx_get_node1().
 * @param node Pointer to tree node to start search for.
 * @param h Hash value (id) of node to get.
 * @param d Current search depth starting with 0 at the root.
 * @return Returns a pointer to the desired node.
 */
bx_node_t *bx_get_node0(bx_node_t *node, bx_hash_t h, bx_hash_t d)
{
#ifdef WITH_THREADS
   bx_node_t *nd;

   pthread_rwlock_rdlock(&rwlock_);
   nd = bx_get_node1(node, h, d);
   pthread_rwlock_unlock(&rwlock_);
   return nd;
#else
   return bx_get_node1(node, h, d);
#endif
}


/*! This function recursively frees a whole tree.
 * @param node Pointer to tree.
 * @param d Current depth, starting with 0 at the root.
 */
void bx_free_tree0(bx_node_t *node, bx_hash_t d)
{
   int i;

   if (node == NULL)
      return;

   if (d < (((int) sizeof(bx_hash_t) * 8) / BX_RES))
      for (i = 0; i < 1 << BX_RES; i++)
         bx_free_tree0(node->next[i], d + 1);

   bx_free(node);
}

