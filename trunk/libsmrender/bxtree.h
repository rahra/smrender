#ifndef BTREE_H
#define BTREE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>


#ifndef bx_hash_t
#define bx_hash_t uint32_t
#endif

#ifndef BX_RES
#define BX_RES ((bx_hash_t)8)
#endif

#define BX_MSK ((1 << BX_RES) - 1)
#define BT_ROOT 0
//#define BT_SHIFT(x) (x << BX_RES)
#define BT_MASK(x, y) ((x >> (sizeof(bx_hash_t) * 8 - (y + 1) * BX_RES)) & BX_MSK)

#define bx_add_node(x, y) bx_add_node0(x, y, BT_ROOT)
#define bx_get_node(x, y) bx_get_node0(x, y, BT_ROOT)
#define bx_free_tree(x) bx_free_tree0(x, BT_ROOT)


typedef struct bx_node
{
   void *next[1 << BX_RES];
} bx_node_t;


bx_node_t *bx_add_node0(bx_node_t **, bx_hash_t, bx_hash_t);
bx_node_t *bx_get_node0(bx_node_t *, bx_hash_t, bx_hash_t);
void bx_free_tree0(bx_node_t *node, bx_hash_t d);
size_t bx_sizeof(void);
//void bx_exit(void);


#endif

