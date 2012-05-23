#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "smrender.h"
#include "bxtree.h"


static size_t mem_usage_ = 0;


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
         if (atexit(bx_exit))
            log_msg(LOG_ERR, "atexit(bx_exit) failed");
         break;

      default:
         log_msg(LOG_INFO, "tree memory: %ld kByte", bx_sizeof() / 1024);
   }
   ae++;
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
         log_msg(LOG_ERR, "calloc failed in bx_add_node0() for hash 0x%016lx: %s",
               (long) h, strerror(errno)),
            exit(EXIT_FAILURE);
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

