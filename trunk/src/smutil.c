/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smrender.
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
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This file contains 'utility' code. This is getting and putting object from
 * and into the object tree, some bstring_t functions (which are not found in
 * bstring.c), and the matching functions which are applied to the objects
 * before execution the rules' actions.
 *
 *  @author Bernhard R. Fischer
 */

#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include "smrender_dev.h"
//#include "bstring.h"


static struct rdata *rd;


void set_util_rd(struct rdata *r)
{
   rd = r;
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


int put_object(osm_obj_t *o)
{
   return put_object0(&rd->obj, o->id, o, o->type - 1);
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


void *get_object(int type, int64_t id)
{
   return get_object0(rd->obj, id, type - 1);
}


/***** The following functions should be moved to different file. *****/


int bs_cmp2(const bstring_t *s1, const bstring_t *s2)
{
   if (s1->len != s2->len)
      return s1->len > s2->len ? 1 : -1;
   return memcmp(s1->buf, s2->buf, s1->len);
}


/*! Match a bstring to a pattern and tag special matching (such as regex) into consideration.
 *  @param dst String to match.
 *  @param pat Pattern which is applied to string dst.
 *  @param st Definition of special match options.
 *  @return On successful match, 1 is returned, otherwise 0.
 */
int bs_match(const bstring_t *dst, const bstring_t *pat, const struct specialTag *st)
{
   int r = 1;
   char buf[dst->len + 1];
   double val;

   if (st == NULL)
      return bs_cmp2(dst, pat) == 0;

   if ((st->type & SPECIAL_MASK) == SPECIAL_DIRECT)
   {
      r = bs_cmp2(dst, pat);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_REGEX)
   {
      // FIXME: this could be avoid if tags are 0-terminated.
      memcpy(buf, dst->buf, dst->len);
      buf[dst->len] = '\0';
 
      r = regexec(&st->re, buf, 0, NULL, 0);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_GT)
   {
      val = bs_tod(*dst);
      r = !(val > st->val);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_LT)
   {
      val = bs_tod(*dst);
      r = !(val < st->val);
   }

   if (st->type & SPECIAL_INVERT)
      return r != 0;

   return r == 0;
}


int bs_match_attr(const osm_obj_t *o, const struct otag *ot, const struct stag *st)
{
   int i, kmatch, vmatch;

   for (i = 0; i < o->tag_cnt; i++)
   {
      kmatch = vmatch = 0;

      kmatch = ot->k.len ? bs_match(&o->otag[i].k, &ot->k, &st->stk) : 1;
      vmatch = ot->v.len ? bs_match(&o->otag[i].v, &ot->v, &st->stv) : 1;

      if (kmatch && (st->stk.type & SPECIAL_NOT))
         return -1;

      if (vmatch && (st->stv.type & SPECIAL_NOT))
         return -1;

      if (kmatch && vmatch)
         return i;
   }

   // FIXME: Why INT_MAX?
   if ((st->stk.type & SPECIAL_NOT) || (st->stv.type & SPECIAL_NOT))
      return INT_MAX;

   return -1;
}


/*! Match tag.
 *  @return -1 on error (no match), otherwise return number of tag which matches.
 */
int match_attr(const osm_obj_t *o, const char *k, const char *v)
{
   struct otag ot;
   struct stag st;

   memset(&ot, 0, sizeof(ot));
   memset(&st, 0, sizeof(st));

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

   return bs_match_attr(o, &ot, &st);
}


/*! Convert coordinate c to string.
 *  @param lat_lon 0 for latitude, other values for longitude.
 */
int coord_str(double c, int lat_lon, char *buf, int len)
{
   if (!lat_lon)
      return snprintf(buf, len, "%02d %c %.1f'", (int) c, c < 0 ? 'S' : 'N', (double) ((int) round(c * T_RESCALE) % T_RESCALE) / TM_RESCALE);
   else
      return snprintf(buf, len, "%03d %c %.1f'", (int) c, c < 0 ? 'W' : 'E', (double) ((int) round(c * T_RESCALE) % T_RESCALE) / TM_RESCALE);

}


long inline col_cmp(int c1, int c2)
{
   return SQRL(RED(c1) - RED(c2)) + SQRL(GREEN(c1) - GREEN(c2)) + SQRL(BLUE(c1) - BLUE(c2));
}

