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
   //rd->w = 2480; rd->h = 3507; rd->dpi = 300;
   // A4 paper landscape (300dpi)
   //rd->h = 2480; rd->w = 3507; rd->dpi = 300;
   // A4 paper portrait (600dpi)
   //rd->w = 4961; rd->h = 7016; rd->dpi = 600;
   // A2 paper landscape (300dpi)
   //rd->w = 7016; rd->h = 4961; rd->dpi = 300;
   // A1 paper landscape (300dpi)
   //rd->w = 9933; rd->h = 7016; rd->dpi = 300;
   // A1 paper portrait (300dpi)
   rd->h = 9933; rd->w = 7016; rd->dpi = 300;

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
   //rd->x1c = 14.72;
   //rd->y1c = 44.23;
   //rd->x2c = 15.29;
   //rd->y2c = 43.96;

   //croatia...osm
   rd->x1c = 13.9;
   rd->y1c = 45.75;
   rd->x2c = 15.4;
   rd->y2c = 43.0;

   //croatia_big...osm
   //rd->x1c = 13.5;
   //rd->y1c = 45.5;
   //rd->x2c = 15.5;
   //rd->y2c = 43.5;

   /* treasure_island
   rd->x1c = 24.33;
   rd->y1c = 37.51;
   rd->x2c = 24.98;
   rd->y2c = 37.16;
   */
 
   return rd;
}


void set_const_tag(struct otag *tag, char *k, char *v)
{
   tag->k.buf = k;
   tag->k.len = strlen(k);
   tag->v.buf = v;
   tag->v.len = strlen(v);
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
      log_msg(LOG_EMERG, "malloc_object(): cannot calloc() new onode: %s", strerror(errno));
      exit(EXIT_FAILURE);
   }
   if ((nd->ref = calloc(ref_cnt, sizeof(int64_t))) == NULL)
   {
      log_msg(LOG_EMERG, "malloc_object(): cannot calloc() refs of new onode: %s", strerror(errno));
      free(nd);
      exit(EXIT_FAILURE);
   }

   nd->ref_cnt = ref_cnt;
   nd->tag_cnt = tag_cnt;
   return nd;
}


int put_object0(bx_node_t **tree, int64_t id, void *p, int idx)
{
   bx_node_t *bn;

   if ((idx < 0) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_ERR, "index to tree node out of range: %d", idx);
      return -1;
   }

   if ((bn = bx_add_node(tree, id)) == NULL)
   {
      log_msg(LOG_ERR, "bx_add_node() failed in put_object0()");
      return -1;
   }
   /* too much debugging....
   if (bn->next[0] != NULL)
   {
      log_msg(LOG_DEBUG, "nt->next[0] contains valid pointer, overwriting.");
   }*/

   bn->next[idx] = p;
   return 0;
}


int put_object(struct onode *nd)
{
   return put_object0(&rd->obj, nd->nd.id, nd, nd->nd.type == OSM_WAY);
}


void *get_object0(bx_node_t *tree, int64_t id, int idx)
{
   bx_node_t *bn;

   if ((idx < 0) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_ERR, "get_object0(): index (%d) to tree node out of range.", idx);
      return NULL;
   }

   if ((bn = bx_get_node(tree, id)) == NULL)
   {
      //log_msg(LOG_ERR, "bx_get_node() failed");
      return NULL;
   }
   if (bn->next[idx] == NULL)
   {
      //log_msg(LOG_ERR, "nt->next[0] contains NULL pointer");
      return NULL;
   }

   return bn->next[idx];
}


struct onode *get_object(int type, int64_t id)
{
   return get_object0(rd->obj, id, type == OSM_WAY);
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

