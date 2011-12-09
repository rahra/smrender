#ifndef BTREE_H
#define BTREE_H


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
#define BT_MASK(x, y) ((x >> ((sizeof(bx_hash_t) * 8  - 1) - y * BX_RES)) & BX_MSK)

#define bx_add_node(x, y) bx_add_node0(x, y, BT_ROOT)
#define bx_get_node(x, y) bx_get_node0(x, y, BT_ROOT)

typedef struct bx_node
{
   void *next[1 << BX_RES];
} bx_node_t;


bx_node_t *bx_add_node0(bx_node_t **, bx_hash_t, bx_hash_t);
bx_node_t *bx_get_node0(bx_node_t *, bx_hash_t, bx_hash_t);
size_t bx_sizeof(void);


#endif

