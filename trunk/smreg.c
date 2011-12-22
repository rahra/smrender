#include <stdint.h>
#include <string.h>

#include "smrender.h"
#include "smlog.h"
#include "MurmurHash2_64.h"
#include "smreg.h"


static bx_node_t *reg_ = NULL;


void **get_reg(const char *s)
{
   int64_t h;
   bx_node_t *bn;

   if (s == NULL)
   {
      log_msg(LOG_ERR, "get_reg() called with NULL pointer");
      return NULL;
   }

   h = MurmurHash64(s, strlen(s), 0);
   if ((bn = bx_get_node(reg_, h)) != NULL)
      return &bn->next[0];

   if ((bn = bx_add_node(&reg_, h)) == NULL)
   {
      log_msg(LOG_ERR, "bx_add_node() failed in get_reg()");
      return NULL;
   }

   bn->next[0] = NULL;
   return &bn->next[0];
}

