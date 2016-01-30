/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "smrender.h"
#include "osm_inplace.h"
#include "bstring.h"


#define TLEN 20
#define BS_ADV(x, y) x.buf += y;\
                     x.len -= y


static size_t mem_usage_ = 0;
static size_t mem_freed_ = 0;


size_t onode_freed(void)
{
   return mem_freed_;
}


size_t onode_mem(void)
{
   return mem_usage_;
}


time_t parse_time(bstring_t b)
{
   //2006-09-29T15:02:52Z
   struct tm tm;

   if (b.len != TLEN)
      return -1;

   memset(&tm, 0, sizeof(tm));
#ifdef HAVE_STRPTIME
   (void) strptime(b.buf, "%Y-%m-%dT%T%z", &tm);
#else
   tm.tm_year = bs_tol(b) - 1900;
   BS_ADV(b, 5);
   tm.tm_mon = bs_tol(b) - 1;
   BS_ADV(b, 3);
   tm.tm_mday = bs_tol(b);
   BS_ADV(b, 3);
   tm.tm_hour = bs_tol(b);
   BS_ADV(b, 3);
   tm.tm_min = bs_tol(b);
   BS_ADV(b, 3);
   tm.tm_sec = bs_tol(b);
   tm.tm_isdst = 0;
#endif

   return mktime(&tm);
}


void free_obj(osm_obj_t *o)
{
   free(o->otag);
   mem_freed_ += sizeof(struct otag) * o->tag_cnt;
   switch (o->type)
   {
      case OSM_NODE:
         // nothing special to free for nodes
         break;

      case OSM_WAY:
         free(((osm_way_t*) o)->ref);
         mem_freed_ += sizeof(int64_t) * ((osm_way_t*) o)->ref_cnt;
         break;

      case OSM_REL:
         free(((osm_rel_t*) o)->mem);
         mem_freed_ += sizeof(struct rmember) * ((osm_rel_t*) o)->mem_cnt;
         break;

      default:
         log_msg(LOG_ERR, "no such object type: %d", o->type);
   }
   mem_freed_ += SIZEOF_OSM_OBJ(o);
   free(o);
}


void *malloc_mem(size_t ele, int cnt)
{
   void *mem;

   if ((mem = malloc(ele * cnt)) == NULL)
      log_msg(LOG_ERR, "could not malloc_mem(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   mem_usage_ += ele *cnt;
   return mem;
}


osm_node_t *malloc_node(short tag_cnt)
{
   osm_node_t *n;

   if ((n = calloc(1, sizeof(osm_node_t))) == NULL)
      log_msg(LOG_ERR, "could not malloc_node(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   n->obj.type = OSM_NODE;
   n->obj.vis = 2;
   n->obj.otag = malloc_mem(sizeof(struct otag), tag_cnt);
   n->obj.tag_cnt = tag_cnt;
   mem_usage_ += sizeof(osm_node_t);
   return n;
}


osm_way_t *malloc_way(short tag_cnt, int ref_cnt)
{
   osm_way_t *w;

   if ((w = calloc(1, sizeof(osm_way_t))) == NULL)
      log_msg(LOG_ERR, "could not malloc_way(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   w->obj.type = OSM_WAY;
   w->obj.vis = 2;
   w->obj.otag = malloc_mem(sizeof(struct otag), tag_cnt);
   w->obj.tag_cnt = tag_cnt;
   w->ref = malloc_mem(sizeof(int64_t), ref_cnt);
   w->ref_cnt = ref_cnt;
   mem_usage_ += sizeof(osm_way_t);
   return w;
}


osm_rel_t *malloc_rel(short tag_cnt, short mem_cnt)
{
   osm_rel_t *r;

   if ((r = calloc(1, sizeof(osm_rel_t))) == NULL)
      log_msg(LOG_ERR, "could not malloc_rel(): %s", strerror(errno)),
         exit(EXIT_FAILURE);
   r->obj.type = OSM_REL;
   r->obj.vis = 2;
   r->obj.otag = malloc_mem(sizeof(struct otag), tag_cnt);
   r->obj.tag_cnt = tag_cnt;
   r->mem = malloc_mem(sizeof(struct rmember), mem_cnt);
   r->mem_cnt = mem_cnt;
   mem_usage_ += sizeof(osm_rel_t);
   return r;
}


void osm_obj_default(osm_obj_t *o)
{
   o->tim = time(NULL);
   o->ver = 1;
   o->vis = 1;
   // FIXME: this does not check if tag 0 exists and if tag_cnt > 0
   set_const_tag(&o->otag[0], "generator", "smrender");
}


void osm_way_default(osm_way_t *w)
{
   w->obj.id = unique_way_id();
   osm_obj_default((osm_obj_t*) w);
}


void osm_node_default(osm_node_t *n)
{
   n->obj.id = unique_node_id();
   osm_obj_default((osm_obj_t*) n);
}


const char *role_str(int role)
{
   switch (role)
   {
      case ROLE_EMPTY:
         return "";
      case ROLE_INNER:
         return "inner";
      case ROLE_OUTER:
         return "outer";
      case ROLE_TO:
         return "to";
      case ROLE_FROM:
         return "from";
      case ROLE_VIA:
         return "via";
      case ROLE_LINK:
         return "link";
      case ROLE_FORWARD:
         return "forward";
      case ROLE_BACKWARD:
         return "backward";
      case ROLE_STOP:
         return "stop";
      case ROLE_LABEL:
         return "label";
      case ROLE_ADMIN_CENTRE:
         return "admin_centre";
      case ROLE_NA:
      default:
         return "n/a";
   }
}


/*! This function tests the bstring_t pointed to by b for a known role-string
 * of a relation.
 * @param b Pointer to a role-string of type bstring_t.
 * @return This function returns an integer corresponding to role-string. These
 * integers are defined by an enum in osm_inplace.h name ROLE_xxx. If the
 * string is empty ROLE_EMPTY is returned. If the string is unknown or a NULL
 * pointer is passed ROLE_NA is returned.
 * */
int strrole(const bstring_t *b)
{
   if (b == NULL)
      return ROLE_NA;
   else if (!b->len)
      return ROLE_EMPTY;
   else if (!bs_cmp(*b, "inner"))
      return ROLE_INNER;
   else if (!bs_cmp(*b, "outer"))
      return ROLE_OUTER;
   else if (!bs_cmp(*b, "to"))
      return ROLE_TO;
   else if (!bs_cmp(*b, "from"))
      return ROLE_FROM;
   else if (!bs_cmp(*b, "via"))
      return ROLE_VIA;
   else if (!bs_cmp(*b, "link"))
      return ROLE_LINK;
   else if (!bs_cmp(*b, "forward"))
      return ROLE_FORWARD;
   else if (!bs_cmp(*b, "backward"))
      return ROLE_BACKWARD;
   else if (!bs_cmp(*b, "stop"))
      return ROLE_STOP;
   else if (!bs_cmp(*b, "label"))
      return ROLE_LABEL;
   else if (!bs_cmp(*b, "admin_centre"))
      return ROLE_ADMIN_CENTRE;
   else
      return ROLE_NA;
}


/*! This fucntion returns a constant string which corresponds to the type of an
 * OSM object.
 * @param type Type of object, which is one of OSM_NODE, OSM_WAY, or OSM_REL.
 * @return Returns a pointer to a string.
 */
const char *type_str(int type)
{
   switch (type)
   {
      case OSM_NODE:
         return "node";
      case OSM_WAY:
         return "way";
      case OSM_REL:
         return "relation";
      default:
         return "unknown";
   }
}

