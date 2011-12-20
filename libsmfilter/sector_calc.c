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

/*! This program reads an OSM/XML file and parses, filters, and modifies it.
 *  Filter and modification rules are hardcoded.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
#include "seamark.h"
#include "smlog.h"


//#define DEG2RAD(x) ((x) * M_PI / 180.0)
#define TBUFLEN 24
#define SEAMARK_LIGHT_OBJECT "seamark:light:object"
#define SEAMARK_LIGHT_RADIAL "seamark:light_radial"
#define SEAMARK_LIGHT_SECTOR_NR "seamark:light:sector_nr"
#define SEAMARK_ARC_STYLE "seamark:arc_style"
#define SEAMARK_LIGHT_ARC_AL "seamark:light_arc_al"
#define SEAMARK_LIGHT_ARC "seamark:light_arc"

static double arc_div_ = ARC_DIV;
static double arc_max_ = ARC_MAX;
static double sec_radius_ = SEC_RADIUS;
static double dir_arc_ = DIR_ARC;

extern int parse_rhint_;

const double altr_[] = {0.005, 0.005, 0.01, 0.005};

static const char *col_[] = {"white", "red", "green", "yellow", "orange", "blue", "violet", "amber", NULL};
static const char *col_abbr_[] = {"W", "R", "G", "Y", "Or", "Bu", "Vi", "Am", NULL};
static const int col_cnt_ = 8;
static long node_id_ = -1;
static const char *atype_[] = {"undef", "solid", "suppress", "dashed", 
#ifdef RENDER_TAPERING
   "taper_up", "taper_down", "taper_1", "taper_2", "taper_3", "taper_4", "taper_5", "taper_6", "taper_7",
#endif
   NULL};


const char *color_abbr(int n)
{
   if ((n < 0) || (n >= col_cnt_))
      return NULL;
   return col_abbr_[n];
}


const char *color(int n)
{
   if ((n < 0) || (n >= col_cnt_))
      return NULL;
   return col_[n];
}


/*! Test if bstring is numeric. It tests for the following expression:
 *  /^[-]?[0-9]*[.]?[0-0].
 *  @param b Bstring to test.
 *  @return 1 if first part of bstring is numeric, otherwise 0.
 */
int bs_isnum(bstring_t b)
{
   int i;

   if (*b.buf == '-')
      if (!bs_advance(&b))
         return 0;

   for (i = 0; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b), i++);

   if (!b.len)
      return i > 0;
   if (*b.buf != '.')
      return i > 0;
   if (!bs_advance(&b))
      return i > 0;

   for (i = 0; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b), i++);
   
   return i > 0;
}


int parse_arc_type(const bstring_t *b)
{
   int i;

   for (i = 0; atype_[i] != NULL; i++)
      if (!bs_ncmp(*b, atype_[i], strlen(atype_[i])))
         return i;
   return -1;
}


/*! Find next separator which is either ':' or ';'.
 *  @return It returns 0 if separator is ':' in which case the bstring is
 *  advance at the next character behind the colon. 1 is returned if then
 *  length of bstring is 0 without finding any separator or if the separator is
 *  a semicolon. In the latter case the bstring points exactly to the
 *  semicolon.
 */
int find_sep(bstring_t *c)
{
   // find next colon
   for (; c->len && (*c->buf != ':') && (*c->buf != ';'); bs_advance(c));
   if (!c->len)
      return 1;
   if (*c->buf == ';')
      return 1;
   if (!bs_advance(c))
      return 1;

   return 0;
}


/*! get_sectors() parses the tags of an OSM nodes and extracts
 *  sector data into struct sector data structures.
 *  @param sec Pointer to first element of struct sector array.
 *  @param nmax maximum number of elements in array sec.
 *  @return Number of elements touched in sec array.
 */
//int get_sectors(const hpx_tree_t *t, struct sector *sec, int nmax)
int get_sectors(struct rdata *rd, const struct onode *nd, struct sector *sec, int nmax)
{
   int i, j, l;      //!< loop variables
   int n = 0;        //!< sector counter
   int k;            //!< sector number
   bstring_t b, c;   //!< temporary bstrings

   for (i = 0; i < nd->tag_cnt; i++)
   {
            k = 0;
            if (!bs_cmp(nd->otag[i].k, "seamark:light:orientation"))
            {
                  (sec + k)->dir = bs_tod(nd->otag[i].v);
                  if (!(sec + k)->used)
                  {
                     n++;
                     (sec + k)->used = 1;
                     (sec + k)->nr = k;
                  }
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:category"))
            {
                  if (!bs_cmp(nd->otag[i].v, "directional"))
                  {
                     (sec + k)->cat = CAT_DIR;
                     if (!(sec + k)->used)
                     {
                        n++;
                        (sec + k)->used = 1;
                        (sec + k)->nr = k;
                     }
                  }
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:colour"))
            {
               k = 0;
                  for (l = 0; col_[l]; l++)
                  {
                     if (!bs_cmp(nd->otag[i].v, col_[l]))
                     {
                        (sec + k)->col[0] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME
                     //log_msg("unknown color: %.*s", nd->otag[i].v.len, nd->otag[i].v.buf);
                     continue;
                  }
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:character"))
            {

               (sec + k)->lc.lc = nd->otag[i].v;
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:period"))
            {
                  (sec + k)->lc.period = bs_tol(nd->otag[i].v);
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:range"))
            {
                  (sec + k)->lc.range = bs_tol(nd->otag[i].v);
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:group"))
            {
                  (sec + k)->lc.group = bs_tol(nd->otag[i].v);
               continue;
            }
            else if ((nd->otag[i].k.len > 14) && !strncmp(nd->otag[i].k.buf, "seamark:light:", 14))
            {
               b = nd->otag[i].k;
               b.len -= 14;
               b.buf += 14;

               if (!bs_isnum(b))
                  continue;

               // get sector number of tag
               k = bs_tol(b);

               // check if it is in range
               if ((k <= 0) || (k >= nmax))
               {
                  //FIXME: logging not possible here
                  //log_msg(LOG_WARN, "sector number out of range: %d", k);
                  continue;
               }

               // find tag section behind sector number
               for (; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b));

               if (*b.buf == ':')
                  if (!bs_advance(&b))
                     continue;

               if (!bs_cmp(b, "sector_start"))
               {
                  (sec + k)->start = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "sector_end"))
               {
                  (sec + k)->end = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "colour"))
               {
                  c = nd->otag[i].v;
                  for (l = 0; col_[l]; l++)
                  {
                     if (!bs_ncmp(c, col_[l], strlen(col_[l])))
                     {
                        (sec + k)->col[0] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME: logging not possible here
                     //log_msg("unknown color: %.*s", c.len, c.buf);
                     continue;
                  }

                  for (; c.len && (*c.buf != ';'); bs_advance(&c));
                  if (!c.len) continue;
                  if (!bs_advance(&c)) continue;

                  for (l = 0; col_[l]; l++)
                  {

                     if (!bs_ncmp(c, col_[l], strlen(col_[l])))
                     {
                        (sec + k)->col[1] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME: logging not possible here
                     //log_msg("unknown color: %.*s", c.len, c.buf);
                     continue;
                  }
               }
               else if (!bs_cmp(b, "radius"))
               {
                  c = nd->otag[i].v;

                  if (!c.len)
                     continue;

                  for (; c.len; (sec + k)->fused++)
                  {
                     // if it is not the first radius set, advance bstring
                     // behind next ';'
                     if ((sec + k)->fused)
                     {
                        for (; c.len && (*c.buf != ';'); bs_advance(&c));
                        if (!c.len)
                           break;
                        if (!bs_advance(&c))
                           break;
                     }

                     // if radius definition does not start with a colon, the
                     // first entry is a radius
                     if (*c.buf != ':')
                        (sec + k)->sf[(sec + k)->fused].r = bs_tod(c);

                     // find next colon
                     if (find_sep(&c))
                        continue;

                     // test if it is numeric.
                     if (bs_isnum(c))
                     {
                        // get value of <segment>
                        (sec + k)->sf[(sec + k)->fused].a = bs_tod(c);
                        // find next colon
                        if (find_sep(&c))
                           continue;
                        // get value of <type>
                        if ((l = parse_arc_type(&c)) != -1)
                           (sec + k)->sf[(sec + k)->fused].type = l;
                        else
                        {
                           //FIXME
                           //log_msg("arc_type unknown: %.*s", c.len, c.buf);
                           (sec + k)->sf[(sec + k)->fused].type = ARC_SUPPRESS;
                        }
                     }
                     else 
                     {
                        // get value of <type>
                        if ((l = parse_arc_type(&c)) != -1)
                           (sec + k)->sf[(sec + k)->fused].type = l;
                        else
                        {
                           //FIXME
                           //log_msg("arc_type unknown: %.*s", c.len, c.buf);
                           (sec + k)->sf[(sec + k)->fused].type = ARC_SUPPRESS;
                        }
                         // find next colon
                        if (find_sep(&c))
                           continue;
                        // get value of <segment>
                        if (bs_isnum(c))
                           (sec + k)->sf[(sec + k)->fused].a = bs_tod(c);
                     }
                  }
               }
               else if (!bs_cmp(b, "orientation"))
               {
                  (sec + k)->dir = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "category"))
               {
                  if (bs_cmp(nd->otag[i].v, "directional"))
                     continue;
                  (sec + k)->cat = CAT_DIR;
               }
               // continue loop if none of above matches
               else
                  continue;

               if (!(sec + k)->used)
               {
                  n++;
                  (sec + k)->used = 1;
                  (sec + k)->nr = k;
               }
            }
         }
 
   //if (n) rd->cb.log_msg(LOG_DEBUG, "%d sectors found", n);

   return n;
}


void node_calc(const struct osm_node *nd, double r, double a, double *lat, double *lon)
{
   *lat = r * sin(a);
   *lon = r * cos(a) / cos(DEG2RAD(nd->lat));
}


/*! This function creates the combined light character tag
 * 'seamark:light_character'.
 */
#if 0
void pchar(const struct osm_node *nd, const struct sector *sec)
{
   char group[8] = "", period[8] = "", range[8] = "", col[8] = "", buf[256];
   struct tm *tm;
   char ts[TBUFLEN] = "0000-00-00T00:00:00Z";

   if ((tm = gmtime(&nd->tim)) != NULL)
      strftime(ts, TBUFLEN, "%Y-%m-%dT%H:%M:%SZ", tm);

   if (sec->lc.group)
      snprintf(group, sizeof(group), "(%d)", sec->lc.group);
   if (sec->lc.period)
      snprintf(period, sizeof(period), " %ds", sec->lc.period);
   if (sec->lc.range)
      snprintf(range, sizeof(range), " %dM", sec->lc.range);
   if (sec->lc.lc.len)
      snprintf(col, sizeof(col), "%s%s.", sec->lc.group ? "" : " ", color_abbr(sec->col[0]));

   if (snprintf(buf, sizeof(buf), "%.*s%s%s%s%s",
         sec->lc.lc.len, sec->lc.lc.buf, group, col, period, range))
      printf("<node id=\"%ld\" lat=\"%f\" lon=\"%f\" ver=\"1\" timestamp=\"%s\">\n<tag k=\"seamark:type\" v=\"virtual\"/>\n<tag k=\"seamark:light_character\" v=\"%s\"/>\n</node>\n",
            get_id(), nd->lat, nd->lon, ts, buf);
}
#endif


int sector_calc3(struct rdata *rd, const struct onode *nd, const struct sector *sec, bstring_t st)
{
   double lat[3], lon[3], d, s, e, w, la, lo;
   int64_t id[5], sn;
   //struct tm *tm;
   //char ts[TBUFLEN] = "0000-00-00T00:00:00Z";
   int i, j, k;
   char buf[256];
   struct onode *node;

   //if ((tm = gmtime(&nd->tim)) != NULL)
   //   strftime(ts, TBUFLEN, "%Y-%m-%dT%H:%M:%SZ", tm);

   //rd->cb.log_msg(LOG_DEBUG, "sector_calc3 called, %d fused", sec->fused);

   for (i = 0; i < sec->fused; i++)
   {
      s = M_PI - DEG2RAD(sec->sf[i].start) + M_PI_2;
      e = M_PI - DEG2RAD(sec->sf[i].end) + M_PI_2;

      // node and radial way of sector_start
      node_calc(&nd->nd, sec->sf[i].r / 60.0, s, &lat[0], &lon[0]);
      if ((node = malloc_object(0, 0)) == NULL) return -1;
      id[0] = node->nd.id = unique_node_id(rd);
      node->nd.type = OSM_NODE;
      node->nd.lat = lat[0] + nd->nd.lat;
      node->nd.lon = lon[0] + nd->nd.lon;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      put_object(rd->nodes, node->nd.id, node);

      //printf("<node id=\"%ld\" version=\"1\" timestamp=\"%s\" lat=\"%f\" lon=\"%f\"/>\n", id[0], ts, lat[0] + nd->lat, lon[0] + nd->lon);

      if (sec->sf[i].startr)
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id(rd);
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = nd->nd.id;
         node->ref[1] = id[0];
         node->otag[0].k.buf = SEAMARK_LIGHT_RADIAL;
         node->otag[0].k.len = strlen(SEAMARK_LIGHT_RADIAL);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         if ((node->otag[0].v.buf = strdup(buf)) == NULL)
         {
            // FIXME: error handling should be improved
            return -1;
         }
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = SEAMARK_LIGHT_OBJECT;
         node->otag[1].k.len = strlen(SEAMARK_LIGHT_OBJECT);
         put_object(rd->ways, node->nd.id, node);

         //printf("<way id=\"%ld\" version=\"1\" timestamp=\"%s\">\n<nd ref=\"%ld\"/>\n<nd ref=\"%ld\"/>\n<tag k=\"seamark:light_radial\" v=\"%d\"/>\n<tag k=\"seamark:light:object\" v=\"%.*s\"/>\n</way>\n", node_id_--, ts, nd->id, id[0], sec->nr, st.len, st.buf);
      }

      // if radii of two segments differ and they are not suppressed then draw a radial line
      // (id[1] still contains end node of previous segment)
      if (i && (sec->sf[i].r != sec->sf[i - 1].r) && (sec->sf[i].type != ARC_SUPPRESS) && (sec->sf[i - 1].type != ARC_SUPPRESS))
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id(rd);
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = id[1];
         node->ref[1] = id[0];
         node->otag[0].k.buf = SEAMARK_LIGHT_RADIAL;
         node->otag[0].k.len = strlen(SEAMARK_LIGHT_RADIAL);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         if ((node->otag[0].v.buf = strdup(buf)) == NULL)
         {
            // FIXME: error handling should be improved
            return -1;
         }
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = SEAMARK_LIGHT_OBJECT;
         node->otag[1].k.len = strlen(SEAMARK_LIGHT_OBJECT);
         put_object(rd->ways, node->nd.id, node);

         //printf("<way id=\"%ld\" version=\"1\" timestamp=\"%s\">\n<nd ref=\"%ld\"/>\n<nd ref=\"%ld\"/>\n<tag k=\"seamark:light_radial\" v=\"%d\"/>\n<tag k=\"seamark:light:object\" v=\"%.*s\"/>\n</way>\n", node_id_--, ts, id[1], id[0], sec->nr, st.len, st.buf);
      }
           
      // node and radial way of sector_end
      node_calc(&nd->nd, sec->sf[i].r / 60.0, e, &lat[1], &lon[1]);
      if ((node = malloc_object(0, 0)) == NULL) return -1;
      id[1] = node->nd.id = unique_node_id(rd);
      node->nd.type = OSM_NODE;
      node->nd.lat = lat[1] + nd->nd.lat;
      node->nd.lon = lon[1] + nd->nd.lon;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      put_object(rd->nodes, node->nd.id, node);

      //printf("<node id=\"%ld\" version=\"1\" timestamp=\"%s\" lat=\"%f\" lon=\"%f\"/>\n", id[1], ts, lat[1] + nd->lat, lon[1] + nd->lon);

      if (sec->sf[i].endr)
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id(rd);
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = nd->nd.id;
         node->ref[1] = id[1];
         node->otag[0].k.buf = SEAMARK_LIGHT_RADIAL;
         node->otag[0].k.len = strlen(SEAMARK_LIGHT_RADIAL);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         if ((node->otag[0].v.buf = strdup(buf)) == NULL)
         {
            // FIXME: error handling should be improved
            return -1;
         }
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = SEAMARK_LIGHT_OBJECT;
         node->otag[1].k.len = strlen(SEAMARK_LIGHT_OBJECT);
         put_object(rd->ways, node->nd.id, node);

         //printf("<way id=\"%ld\" version=\"1\" timestamp=\"%s\">\n<nd ref=\"%ld\"/>\n<nd ref=\"%ld\"/>\n<tag k=\"seamark:light_radial\" v=\"%d\"/>\n<tag k=\"seamark:light:object\" v=\"%.*s\"/>\n</way>\n", node_id_--, ts, nd->id, id[1], sec->nr, st.len, st.buf);
      }

      // do not generate arc if radius is explicitly set to 0 or type of arc is
      // set to 'suppress'
      if ((sec->sf[i].type == ARC_SUPPRESS) || (sec->sf[i].r == 0.0))
         continue;

      // calculate distance of nodes on arc
      if ((arc_max_ > 0.0) && ((sec->sf[i].r / arc_div_) > arc_max_))
         d = arc_max_;
      else
         d = sec->sf[i].r / arc_div_;
      d = 2.0 * asin((d / 60.0) / (2.0 * (sec->sf[i].r / 60.0)));

      // if end angle is greater than start, wrap around 360 degrees
      if (e > s)
         e -= 2.0 * M_PI;

      // make nodes of arc
      for (w = s - d, sn = 0, j = 0; w > e; w -= d, j++)
      {
         node_calc(&nd->nd, sec->sf[i].r / 60.0, w, &la, &lo);
         if ((node = malloc_object(0, 0)) == NULL) return -1;
         id[1] = node->nd.id = unique_node_id(rd);
         if (!sn) sn = id[1];
         node->nd.type = OSM_NODE;
         node->nd.lat = la + nd->nd.lat;
         node->nd.lon = lo + nd->nd.lon;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         put_object(rd->nodes, node->nd.id, node);

         //printf("<node id=\"%ld\" version=\"1\" timestamp=\"%s\" lat=\"%f\" lon=\"%f\"/>\n", node_id_--, ts, la + nd->lat, lo + nd->lon);
         //rd->cb.log_msg(LOG_DEBUG, "creating arc nodes, id=%ld, w=%f, e=%f, j=%d", node->nd.id, w, e, j);
      }

      // connect nodes of arc to a way
      if ((node = malloc_object(4, j + 2)) == NULL) return -1;
      id[3] = unique_way_id(rd);
      node->nd.id = id[3];
      node->nd.type = OSM_WAY;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      node->otag[0].k.buf = SEAMARK_LIGHT_SECTOR_NR;
      node->otag[0].k.len = strlen(SEAMARK_LIGHT_SECTOR_NR);
      snprintf(buf, sizeof(buf), "%d", sec->nr);
      if ((node->otag[0].v.buf = strdup(buf)) == NULL)
      {
         // FIXME: error handling should be improved
         return -1;
      }
      node->otag[0].v.len = strlen(buf);
      node->otag[1].k.buf = strdup(SEAMARK_LIGHT_OBJECT);
      node->otag[1].k.len = strlen(SEAMARK_LIGHT_OBJECT);

      node->otag[2].k.buf = strdup(SEAMARK_ARC_STYLE);
      node->otag[2].k.len = strlen(SEAMARK_ARC_STYLE);
      node->otag[2].v.buf = strdup(atype_[sec->sf[i].type]);
      node->otag[2].v.len = strlen(atype_[sec->sf[i].type]);

      rd->cb.log_msg(LOG_DEBUG, "secnr %p, SEAMARK_ARC_STYLE %p, atype_ %p", node->otag[0].v.buf, node->otag[2].k.buf, node->otag[2].v.buf);
      //printf("<way id=\"%ld\" version=\"1\" timestamp=\"%s\">\n<tag k=\"seamark:light:sector_nr\" v=\"%d\"/>\n<tag k=\"seamark:light:object\" v=\"%.*s\"/>\n<tag k=\"seamark:arc_style\" v=\"%s\"/>\n",
      //      id[3], ts, sec->nr, st.len, st.buf, atype_[sec->sf[i].type]);

      if (sec->al)
      {
         //FIXME %d after _al missing...
         node->otag[3].k.buf = SEAMARK_LIGHT_ARC_AL;
         node->otag[3].k.len = strlen(SEAMARK_LIGHT_ARC_AL);
         node->otag[3].v.buf = (char*) col_[sec->col[1]];
         node->otag[3].v.len = strlen(col_[sec->col[1]]);

         //printf("<tag k=\"seamark:light_arc_al%d\" v=\"%s\"/>\n", sec->al, col_[sec->col[1]]);
      }
      else
      {
         node->otag[3].k.buf = SEAMARK_LIGHT_ARC;
         node->otag[3].k.len = strlen(SEAMARK_LIGHT_ARC);
         node->otag[3].v.buf = (char*) col_[sec->col[0]];
         node->otag[3].v.len = strlen(col_[sec->col[0]]);

         //printf("<tag k=\"seamark:light_arc\" v=\"%s\"/>\n", col_[sec->col[0]]);
      }

      node->ref[0] = id[0];
      node->ref[node->ref_cnt - 1] = id[1];
      for (k = 0; k < j; sn--, k++)
      {
         node->ref[k + 1] = sn;
         //rd->cb.log_msg(LOG_DEBUG, "adding refs to way, id=%ld", sn);
      }
      put_object(rd->ways, node->nd.id, node);

      /*
      printf("<nd ref=\"%ld\"/>\n", id[0]);
      for (w = s - d; sn > id[3]; sn--, w -= d)
      {
         printf("<nd ref=\"%ld\"/>\n", sn);
      }
      printf("<nd ref=\"%ld\"/>\n", id[1]);
      printf("</way>\n");
      */
   }

   return 0;
}


void init_sector(struct sector *sec)
{
   int i;

   memset(sec, 0, sizeof(*sec));
   sec->start = sec->end = sec->r = sec->dir = NAN;
   sec->col[1] = -1;

   for (i = 0; i < MAX_SFRAC; i++)
      sec->sf[i].r = sec->sf[i].a = NAN;
}


/* wooly thoughts...
 * assumption:
 * sector_start = 100
 * sector_end = 200
 * radius = :10;:dashed;:solid:-10
 *    -> 100-110:solid;110-190:dashed;190-200:solid
 * radius = :-10:dashed
 *    -> 100-190:solid;190-200:dashed
 *
 *
 *
 *  @return 0 if all segments could be generated. If a negative angle was
 *  defined in another than the last segment, -1 is returned. If generation of
 *  tapering segments would exceed MAX_SFRAC (array overflow), -2 is returned.
 */
int proc_sfrac(struct sector *sec)
{
   int i, j;

   if (isnan(sec->sf[0].r))
      sec->sf[0].r = isnan(sec->r) ? sec_radius_ : sec->r;
   if (sec->sf[0].r < 0)
      sec->sf[0].r = sec_radius_;

   if (!sec->fused && isnan(sec->dir))
   {
      sec->sf[0].r = sec_radius_;
      sec->sf[0].start = sec->start;
      sec->sf[0].end = sec->end;
      sec->sf[0].col = sec->col[0];
      sec->sf[0].type = ARC_SOLID;
      if (sec->end - sec->start < 360)
         sec->sf[0].startr = sec->sf[0].endr = 1;
      sec->fused++;
      return 0;
   }

   // handle directional light
   if (!isnan(sec->dir))
   {
      if (sec->sspace < 0) sec->sf[0].start = sec->dir - dir_arc_;
      else if ((sec->sspace / 2) < dir_arc_) sec->sf[0].start = sec->dir - sec->sspace / 2;
      else sec->sf[0].start = sec->dir - dir_arc_;
      //sec->sf[0].start = sec->dir - (dir_arc_ < sec->sspace ? dir_arc_ : sec->sspace / 2);
      sec->sf[0].end = sec->dir;
      sec->sf[0].col = sec->col[0];
      sec->sf[0].type = ARC_SOLID;
      sec->sf[0].endr = 1;
      sec->sf[1].r = sec->sf[0].r;
      sec->sf[1].start = sec->dir;
      if (sec->espace < 0) sec->sf[1].end = sec->dir + dir_arc_;
      else if ((sec->espace / 2) < dir_arc_) sec->sf[1].end = sec->dir + sec->espace / 2;
      else sec->sf[1].end = sec->dir + dir_arc_;
      //sec->sf[1].end = sec->dir + (dir_arc_ < sec->espace ? dir_arc_ : sec->espace / 2);
      sec->sf[1].col = sec->col[0];
      sec->sf[1].type = ARC_SOLID;
      sec->fused = 2;

      return 0;
   }

   if (isnan(sec->sf[0].a))
   {
      sec->sf[0].a = sec->end - sec->start;
   }
   else if (sec->sf[0].a < 0)
   {
      // negative angle is allowed only in last segment
      if (sec->fused > 1)
         return -1;

      if (sec->sf[0].a < sec->start - sec->end)
         sec->sf[0].a = sec->start - sec->end;

      sec->sf[1].type = sec->sf[0].type;
      sec->sf[1].a = sec->sf[0].a;
      sec->sf[0].a = sec->sf[0].a + sec->end - sec->start;
      sec->sf[0].type = ARC_SOLID;

      sec->fused++;
   }

   if (sec->sf[0].a > sec->end - sec->start)
      sec->sf[0].a = sec->end - sec->start;

   sec->sf[0].start = sec->start;
   sec->sf[0].end = sec->start + sec->sf[0].a;
   sec->sf[0].col = sec->col[0];
   sec->sf[0].startr = 1;
   if (sec->sf[0].type == ARC_UNDEF)
      sec->sf[0].type = ARC_SOLID;

   for (i = 1; i < sec->fused; i++)
   {
      if (isnan(sec->sf[i].r))
         sec->sf[i].r = sec->sf[i - 1].r;
      if (sec->sf[i].type == ARC_UNDEF)
         sec->sf[i].type = sec->sf[i - 1].type;
      sec->sf[i].col = sec->sf[i - 1].col;

      if (isnan(sec->sf[i].a))
      {
         sec->sf[i].start = sec->sf[i - 1].end;
         sec->sf[i].end = sec->end;
         sec->sf[i].a = sec->sf[i].end - sec->sf[i].start;
      }
      else if (sec->sf[i].a < 0)
      {
         // negative angle is allowed only in last segment
         if (sec->fused > i + 1)
            return -1;

         if (sec->sf[i].a < sec->start - sec->end)
            sec->sf[i].a = sec->start - sec->end;

         sec->sf[i - 1].end = sec->end + sec->sf[i].a;
         sec->sf[i].start = sec->end + sec->sf[i].a;
         sec->sf[i].end = sec->end;
         sec->sf[i].a = -sec->sf[i].a;
      }
      else
      {
         if (sec->sf[i].a + sec->sf[i - 1].end > sec->end)
            sec->sf[i].a = sec->end - sec->sf[i - 1].end;

         sec->sf[i].start = sec->sf[i - 1].end;
         sec->sf[i].end = sec->sf[i].start + sec->sf[i].a;
      }
   }

   // creating tapering segments
   for (i = 0; i < sec->fused; i++)
   {
      if ((sec->sf[i].type != ARC_TAPER_UP) && (sec->sf[i].type != ARC_TAPER_DOWN))
         continue;
      // check array overflow
      if (sec->fused > MAX_SFRAC - TAPER_SEGS + 1)
         return -2;

      memmove(&sec->sf[i + TAPER_SEGS], &sec->sf[i + 1], sizeof(struct sector_frac) * (TAPER_SEGS - 1));
      sec->sf[i].a /= TAPER_SEGS;
      sec->sf[i].end = sec->sf[i].start + sec->sf[i].a;

      for (j = 1; j < TAPER_SEGS; j++)
      {
         memcpy(&sec->sf[i + j], &sec->sf[i], sizeof(struct sector_frac));
         sec->sf[i + j].start = sec->sf[i + j - 1].end;
         sec->sf[i + j].end = sec->sf[i + j].start + sec->sf[i + j].a;
         sec->sf[i + j].type = sec->sf[i].type == ARC_TAPER_UP ? ARC_TAPER_1 + j : ARC_TAPER_7 - j;
         if (sec->sf[i + j].startr)
            sec->sf[i + j].startr = 0;
      }
      sec->sf[i].type = sec->sf[i].type == ARC_TAPER_UP ? ARC_TAPER_1 : ARC_TAPER_7;
      sec->fused += TAPER_SEGS - 1;
   }

   i = sec->fused - 1;
   // extend last section to end of sector
   if (sec->sf[i].end < sec->end)
      sec->sf[i].end = sec->end;
   // add radial to last section
   sec->sf[i].endr = 1;

   return 0;
}

