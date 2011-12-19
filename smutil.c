#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include "smrender.h"
#include "bstring.h"
#include "smlog.h"


int64_t unique_node_id(struct rdata *rd)
{
   rd->ds.min_nid = rd->ds.min_nid < 0 ? rd->ds.min_nid - 1 : -1;
   return rd->ds.min_nid;
}


int64_t unique_way_id(struct rdata *rd)
{
   rd->ds.min_wid = rd->ds.min_wid < 0 ? rd->ds.min_wid - 1 : -1;
   return rd->ds.min_wid;
}


struct onode *malloc_object(int tag_cnt, int ref_cnt)
{
   struct onode *nd;

   if ((nd = calloc(1, sizeof(struct onode) + tag_cnt * sizeof(struct otag))) == NULL)
   {
      //log_msg(LOG_ERR, "cannot calloc() for new onode: %s", strerror(errno));
      return NULL;
   }
   if ((nd->ref = calloc(ref_cnt, sizeof(int64_t))) == NULL)
   {
      //log_msg(LOG_ERR, "cannot calloc() for refs of new onode: %s", strerror(errno));
      free(nd);
      return NULL;
   }

   nd->ref_cnt = ref_cnt;
   nd->tag_cnt = tag_cnt;
   return nd;
}


int put_object(bx_node_t *tree, int64_t id, struct onode *nd)
{
   bx_node_t *bn;

   if ((bn = bx_get_node(tree, id)) == NULL)
   {
      //log_msg(LOG_ERR, "bx_get_node() failed");
      return -1;
   }
   /* too much debugging....
   if (bn->next[0] != NULL)
   {
      log_msg(LOG_DEBUG, "nt->next[0] contains valid pointer, overwriting.");
   }*/

   bn->next[0] = nd;
   return 0;
}


struct onode *get_object(bx_node_t *tree, int64_t id)
{
   bx_node_t *bn;

   if ((bn = bx_get_node(tree, id)) == NULL)
   {
      //log_msg(LOG_ERR, "bx_get_node() failed");
      return NULL;
   }
   if (bn->next[0] == NULL)
   {
      //log_msg(LOG_ERR, "nt->next[0] contains NULL pointer");
      return NULL;
   }

   return bn->next[0];
}


int bs_cmp2(const bstring_t *s1, const bstring_t *s2)
{
   if (s1->len != s2->len)
      return s1->len > s2->len ? 1 : -1;
   return memcmp(s1->buf, s2->buf, s1->len);
}


int bs_match(const bstring_t *dst, const bstring_t *pat, const struct specialTag *st)
{
   int r;
   char buf[dst->len + 1];

   if (st == NULL)
      return bs_cmp2(dst, pat) == 0;

   if ((st->type & SPECIAL_MASK) == SPECIAL_DIRECT)
   {
      r = bs_cmp2(dst, pat);
      if (st->type & SPECIAL_INVERT)
         return r != 0;
      else
         return r == 0;
   }

   if ((st->type & SPECIAL_MASK) == SPECIAL_REGEX)
   {
      // FIXME: this could be avoid if tags are 0-terminated.
      memcpy(buf, dst->buf, dst->len);
      buf[dst->len] = '\0';
 
      r = regexec(&st->re, buf, 0, NULL, 0);
      if (st->type & SPECIAL_INVERT)
         return r != 0;
      else
         return r == 0;
   }

   return 0;
}


int bs_match_attr(const struct onode *nd, const struct otag *ot)
{
   int i, kmatch, vmatch;

   for (i = 0; i < nd->tag_cnt; i++)
   {
      kmatch = vmatch = 0;

      kmatch = ot->k.len ? bs_match(&nd->otag[i].k, &ot->k, &ot->stk) : 1;
      vmatch = ot->v.len ? bs_match(&nd->otag[i].v, &ot->v, &ot->stv) : 1;

      if (kmatch && (ot->stk.type & SPECIAL_NOT))
         return -1;

      if (vmatch && (ot->stv.type & SPECIAL_NOT))
         return -1;

      if (kmatch && vmatch)
         return i;
   }

   if ((ot->stk.type & SPECIAL_NOT) || (ot->stv.type & SPECIAL_NOT))
      return INT_MAX;

   return -1;
}


/*! Match tag.
 *  @return -1 on error (no match), otherwise return number of tag which matches.
 */
int match_attr(const struct onode *nd, const char *k, const char *v)
{
   struct otag ot;

   memset(&ot, 0, sizeof(ot));

   if (k)
   {
      ot.k.len = strlen(k);
      ot.k.buf = (char*) k;
   }
   if (v)
   {
      ot.v.len = strlen(v);
      ot.v.buf = (char*) v;
   }

   return bs_match_attr(nd, &ot);
}

