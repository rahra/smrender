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

#define _XOPEN_SOURCE
#include <time.h>
#include <string.h>
#include <errno.h>

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
#include "smlog.h"


#define TLEN 20
#define BS_ADV(x, y) x.buf += y;\
                     x.len -= y

time_t parse_time(bstring_t b)
{
   //2006-09-29T15:02:52Z
   struct tm tm;

   if (b.len != TLEN)
      return -1;

#ifdef HAS_STRPTIME
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


int proc_osm_node(const hpx_tag_t *tag, osm_obj_t *o)
{
   int i;

   if (!bs_cmp(tag->tag, "node"))
      o->type = OSM_NODE;
   else if (!bs_cmp(tag->tag, "way"))
      o->type = OSM_WAY;
   else if (!bs_cmp(tag->tag, "relation"))
      o->type = OSM_REL;
   else 
      return -1;

   for (i = 0; i < tag->nattr; i++)
   {
      if (o->type == OSM_NODE)
      {
         if (!bs_cmp(tag->attr[i].name, "lat"))
            ((osm_node_t*) o)->lat = bs_tod(tag->attr[i].value);
         else if (!bs_cmp(tag->attr[i].name, "lon"))
            ((osm_node_t*) o)->lon = bs_tod(tag->attr[i].value);
      }

      if (!bs_cmp(tag->attr[i].name, "id"))
         o->id = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "version"))
         o->ver = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "changeset"))
         o->cs = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "uid"))
         o->uid = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "timestamp"))
         o->tim = parse_time(tag->attr[i].value);
   }

   if (!o->ver)
      o->ver = 1;
   if (!o->tim)
      o->tim = time(NULL);

   return tag->type;
}


int get_value(const char *k, hpx_tag_t *tag, bstring_t *b)
{
   int i;

   for (i = 0; i < tag->nattr; i++)
      if (!bs_cmp(tag->attr[i].name, k))
      {
         *b = tag->attr[i].value;
         return 0;
      }

   return -1;
}


void free_obj(osm_obj_t *o)
{
   free(o->otag);
   if (o->type == OSM_WAY)
      free(((osm_way_t*) o)->ref);
   //FIXME: osm_rel_t not implemented yet
   //

   free(o);
}


void *malloc_mem(size_t ele, short cnt)
{
   void *mem;

   if ((mem = malloc(ele * cnt)) == NULL)
      log_msg(LOG_ERR, "could not malloc_mem(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   return mem;
}


osm_node_t *malloc_node(short tag_cnt)
{
   osm_node_t *n;

   if ((n = calloc(1, sizeof(osm_node_t))) == NULL)
      log_msg(LOG_ERR, "could not malloc_node(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   n->obj.type = OSM_NODE;
   n->obj.otag = malloc_mem(sizeof(struct otag), tag_cnt);
   n->obj.tag_cnt = tag_cnt;
   return n;
}


osm_way_t *malloc_way(short tag_cnt, short ref_cnt)
{
   osm_way_t *w;

   if ((w = calloc(1, sizeof(osm_way_t))) == NULL)
      log_msg(LOG_ERR, "could not malloc_way(): %s", strerror(errno)),
      exit(EXIT_FAILURE);
   w->obj.type = OSM_WAY;
   w->obj.otag = malloc_mem(sizeof(struct otag), tag_cnt);
   w->obj.tag_cnt = tag_cnt;
   w->ref = malloc_mem(sizeof(int64_t), ref_cnt);
   w->ref_cnt = ref_cnt;
   return w;
}

