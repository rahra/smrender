#include <stdio.h>
#include <stdlib.h>

#include "bxtree.h"


static size_t mem_usage_ = 0;


size_t bx_sizeof(void)
{
   return mem_usage_;
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
   if (!*node)
   {
      if ((*node = calloc(1, sizeof(bx_node_t))) == NULL)
         perror("add_node0:calloc"), exit(EXIT_FAILURE);
#ifdef MEM_USAGE
      mem_usage_ += sizeof(bx_node_t);
#endif
   }

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

