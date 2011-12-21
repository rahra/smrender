#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include "smrender.h"
#include "bstring.h"
#include "smlog.h"


static struct rdata rd_, *rd = &rd_;


struct rdata *init_rdata(void)
{
   memset(rd, 0, sizeof(*rd));

   // A3 paper portrait (300dpi)
   //rd->w = 3507; rd->h = 4961; rd->dpi = 300;
   // A4 paper portrait (300dpi)
   rd->w = 2480; rd->h = 3507; rd->dpi = 300;
   // A4 paper landscape (300dpi)
   rd->h = 2480; rd->w = 3507; rd->dpi = 300;
   // A4 paper portrait (600dpi)
   //rd->w = 4961; rd->h = 7016; rd->dpi = 600;

   rd->grd.lat_ticks = rd->grd.lon_ticks = G_TICKS;
   rd->grd.lat_sticks = rd->grd.lon_sticks = G_STICKS;
   rd->grd.lat_g = rd->grd.lon_g = G_GRID;

   // init callback function pointers
   //rd->cb.log_msg = log_msg;
   //rd->cb.get_object = get_object;
   //rd->cb.put_object = put_object;
   //rd->cb.malloc_object = malloc_object;
   //rd->cb.match_attr = match_attr;

   // this should be given by CLI arguments
   /* porec.osm
   rd->x1c = 13.53;
   rd->y1c = 45.28;
   rd->x2c = 13.63;
   rd->y2c = 45.183; */
   //dugi.osm
   rd->x1c = 14.72;
   rd->y1c = 44.23;
   rd->x2c = 15.29;
   rd->y2c = 43.96;

   /* treasure_island
   rd->x1c = 24.33;
   rd->y1c = 37.51;
   rd->x2c = 24.98;
   rd->y2c = 37.16;
   */
 
   return rd;
}


//int64_t unique_node_id(struct rdata *rd)
int64_t unique_node_id(void)
{
   rd->ds.min_nid = rd->ds.min_nid < 0 ? rd->ds.min_nid - 1 : -1;
   return rd->ds.min_nid;
}


//int64_t unique_way_id(struct rdata *rd)
int64_t unique_way_id(void)
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


int put_object0(bx_node_t **tree, int64_t id, struct onode *nd)
{
   bx_node_t *bn;

   if ((bn = bx_add_node(tree, id)) == NULL)
   {
      log_msg(LOG_ERR, "bx_add_node() failed");
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


int put_object(struct onode *nd)
{
   bx_node_t **tree;

   switch (nd->nd.type)
   {
      case OSM_NODE:
         tree = &rd->nodes;
         break;

      case OSM_WAY:
         tree = &rd->ways;
         break;

      default:
         log_msg(LOG_ERR, "unknown node type %d", nd->nd.type);
         return -1;
   }

   return put_object0(tree, nd->nd.id, nd);
}


struct onode *get_object0(bx_node_t *tree, int64_t id)
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


struct onode *get_object(int type, int64_t id)
{
   bx_node_t *tree;

   switch (type)
   {
      case OSM_NODE:
         tree = rd->nodes;
         break;

      case OSM_WAY:
         tree = rd->ways;
         break;

      default:
         log_msg(LOG_ERR, "unknown node type %d", type);
         return NULL;
   }

   return get_object0(tree, id);
}


/***** The following functions should be moved to different file. *****/


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

