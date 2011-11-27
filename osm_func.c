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

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"


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


int proc_osm_node(const hpx_tag_t *tag, struct osm_node *nd)
{
   int i, n;

   for (i = 0; i < tag->nattr; i++)
   {
      if (!bs_cmp(tag->attr[i].name, "lat"))
         nd->lat = bs_tod(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "lon"))
         nd->lon = bs_tod(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "id"))
         nd->id = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "version"))
         nd->ver = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "changeset"))
         nd->cs = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "uid"))
         nd->uid = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "timestamp"))
         nd->tim = parse_time(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "action"))
      {
         n = tag->attr[i].value.len < sizeof(nd->act) - 1 ? tag->attr[i].value.len : sizeof(nd->act) - 1;
         memcpy(nd->act, tag->attr[i].value.buf, n);
         nd->act[n] = '\0';
      }
   }
   nd->cl = NCL(nd->lat, nd->lon);

   return tag->type;
}


/*! Get memory for struct osm_node with tcnt tags.
 *  @param tcnt max number of tags.
 *  @return Pointer to structure or NULL in case of error.
 */
struct osm_node *malloc_node(void)
{
   struct osm_node *nd;

   if ((nd = malloc(sizeof(struct osm_node))) == NULL)
      return NULL;

   return nd;
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

