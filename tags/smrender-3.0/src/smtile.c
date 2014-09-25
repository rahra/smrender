/* Copyright 2012 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This files contains the code to create the tiles for an online tile server.
 *  The original code was taken from the OSM Wiki
 *  http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
 *  and was slightly adapted to fulfill the needs for Smrender.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include "smrender.h"
#include "rdata.h"
#include "smrender_dev.h"


#define ZOOM_LEVEL 10

#ifdef TEST_SMTILE
#define create_tile(x) NULL
#define delete_tile(x)
#define cut_tile(x, y)
#define clear_tile(x)
#define save_image(x, y, z)
#endif

struct tpoint
{
   int x, y;
};


static int lon2tile(double lon, int z) 
{ 
   return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z))); 
}
 

static int lat2tile(double lat, int z)
{ 
   int y = floor((1.0 - log(tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z));
#define CHECK_TILE_LAT
#ifdef CHECK_TILE_LAT
   return y < 0 ? 0 : y;
#else
   return y;
#endif
}
 

static void coord2tile(const struct coord *pc, int zoom, struct tpoint *tp)
{
   tp->x = lon2tile(pc->lon, zoom);
   tp->y = lat2tile(pc->lat, zoom);
}


static double tile2lon(int x, int z) 
{
   return x / pow(2.0, z) * 360.0 - 180;
}
 

static double tile2lat(int y, int z) 
{
   double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
   return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}


static void tile2coord(const struct tpoint *tp, int zoom, struct coord *pc)
{
   pc->lon = tile2lon(tp->x, zoom);
   pc->lat = tile2lat(tp->y, zoom);
}


static int test_and_create(const char *dir)
{
   struct stat st;

   if (!stat(dir, &st))
      return 0;

   if (errno != ENOENT)
   {
      log_msg(LOG_ERR, "stat(%s) failed: %s", dir, strerror(errno));
      return -1;
   }

   if (mkdir(dir, 0777) == -1)
   {
      log_msg(LOG_ERR, "mkdir(%s) failed: %s", dir, strerror(errno));
      return -1;
   }

   return 0;
}


#define PBUFSIZE 1024
static int check_dir_i(const char *dir, int i)
{
   char buf[PBUFSIZE];

   // safety check
   if (dir == NULL)
      dir = ".";

   if (i >= 0)
      snprintf(buf, sizeof(buf), "%s/%d", dir, i);
   else
      snprintf(buf, sizeof(buf), "%s", dir);

   if (test_and_create(buf) == -1)
      return -1;

   return 0;
}


int create_tiles(const char *tile_path, const struct rdata *rd, int zoom, int ftype)
{
   char buf[PBUFSIZE], tbuf[PBUFSIZE];
   struct tpoint tp;
   struct coord lu;
   struct bbox bb;
   void *tile;
   int x, y;

   // safety check
   if (tile_path == NULL)
      tile_path = ".";

   if (check_dir_i(tile_path, -1))
      return -1;

   if (check_dir_i(tile_path, zoom))
      return -1;

   if ((tile = create_tile()) == NULL)
      return -1;

   lu.lon = rd->bb.ll.lon;
   lu.lat = rd->bb.ru.lat;

   coord2tile(&lu, zoom, &tp);
   tile2coord(&tp, zoom, &lu);

   log_debug("lu tile: x = %d, y = %d, lon = %f, lat = %f, bblon = %f, bblat = %f",
         tp.x, tp.y, lu.lon, lu.lat, rd->bb.ll.lon, rd->bb.ru.lat);

   if (lu.lon < rd->bb.ll.lon)
      tp.x++;
   if (lu.lat > rd->bb.ru.lat)
      tp.y++;

   snprintf(buf, sizeof(buf), "%s/%d", tile_path, zoom);

   for (x = tp.x; rd->bb.ru.lon >= tile2lon(x + 1, zoom); x++)
   {
      bb.ll.lon = tile2lon(x, zoom);
      bb.ru.lon = tile2lon(x + 1, zoom);

      log_debug("tile x = %d, %f - %f", x, bb.ll.lon, bb.ru.lon);

      if (check_dir_i(buf, x))
      {
         log_msg(LOG_ERR, "check_dir_i(%s, %d) failed", buf, x);
         delete_tile(tile);
         return -1;
      }

      for (y = tp.y; rd->bb.ll.lat <= tile2lat(y + 1, zoom); y++)
      {
         bb.ru.lat = tile2lat(y, zoom);
         bb.ll.lat = tile2lat(y + 1, zoom);

         log_debug("tile y = %d, %f - %f", y, bb.ru.lat, bb.ll.lat);

         clear_tile(tile);
         cut_tile(&bb, tile);
         if (!ftype)
            snprintf(tbuf, sizeof(tbuf), "%s/%d/%d.png", buf, x, y);
         else if (ftype == 1)
            snprintf(tbuf, sizeof(tbuf), "%s/%d/%d.jpg", buf, x, y);
         else
         {
            log_msg(LOG_ERR, "unknown file type %d", ftype);
            delete_tile(tile);
            return -1;
         }

         save_image(tbuf, tile, ftype);
      }
   }

   delete_tile(tile);
   return 0;
}


#define NTILES "neighbor_tiles"


int act_neighbortile_ini(smrule_t *r)
{
   if (check_dir_i(NTILES, -1))
      return -1;
 
   if (check_dir_i(NTILES, ZOOM_LEVEL))
      return -1;

   if ((r->data = li_new()) == NULL)
      return -1;

   return 0;
}


static int node2tile(int64_t nid, int *x, int *y)
{
   osm_node_t *n;

   if ((n = get_object(OSM_NODE, nid)) == NULL)
   {
      log_msg(LOG_ERR, "failed to retrieve node %ld", (long) nid);
      return -1;
   }

   *x = lon2tile(n->lon, ZOOM_LEVEL);
   *y = lat2tile(n->lat, ZOOM_LEVEL);
   return 0;
}


static int write_tile_conf(int x, int y)
{
   double lat0, lon0, lat1, lon1;
   char buf[1024];
   FILE *f;

   snprintf(buf, sizeof(buf), "%s/%d", NTILES, ZOOM_LEVEL);
   if (check_dir_i(buf, x))
   {
      log_msg(LOG_ERR, "check_dir_i(%s, %d) failed", buf, x);
      return -1;
   }
   snprintf(buf, sizeof(buf), "%s/%d/%d/%d.conf", NTILES, ZOOM_LEVEL, x, y);

   if ((f = fopen(buf, "w")) == NULL)
   {
      log_msg(LOG_ERR, "fopen(%s) failed: %s", buf, strerror(errno));
      return -1;
   }

   lat0 = tile2lat(y, ZOOM_LEVEL);
   lon0 = tile2lon(x, ZOOM_LEVEL);
   lat1 = tile2lat(y + 1, ZOOM_LEVEL);
   lon1 = tile2lon(x + 1, ZOOM_LEVEL);


   fprintf(f, "zoom=%d\nx=%d\ny=%d\nllrulonlat=\"%.7f,%.7f,%.7f,%.7f\"\nllrulatlon=\"%.7f:%.7f:%.7f:%.7f\"\n",
         ZOOM_LEVEL, x, y, lon0, lat1, lon1, lat0, lat1, lon0, lat0, lon1);

   fclose(f);

   return 0;
}


static void tile_ptr_xy(const void *p, int *x, int *y)
{
   *x = ((long) p >> 32) & 0xffffffff;
   *y = (long) p  & 0xffffffff;
}


static inline void *mk_tile_ptr(int x, int y)
{
   return (void*) (((long) x << 32) | y);
}


static int reg_tile(list_t *first, int x, int y)
{
   list_t *elem;
   void *tp;

   tp = mk_tile_ptr(x, y);
   for (elem = li_first(first); elem != li_head(first); elem = elem->next)
      if (tp == elem->data)
         return 0;
   if (li_add(first, tp) == NULL)
   {
      log_msg(LOG_ERR, "failed to add tile pointer to list: %s", strerror(errno));
      return -1;
   }
   return 0;
}


int act_neighbortile_main(smrule_t *r, osm_way_t *w)
{
   int i, x, y;

   if (w->obj.type != OSM_WAY)
   {
      log_msg(LOG_ERR, "neighbortile can only be applied to ways");
      return -1;
   }

   for (i = 0; i < w->ref_cnt; i++)
   {
      if (node2tile(w->ref[i], &x, &y) == -1)
      {
         log_msg(LOG_ERR, "log2tile(%ld) failed", (long) w->ref[i]);
         continue;
      }
      (void) reg_tile(r->data, x, y);
   }

   /*
   if (is_closed_poly(w))
      return 0;

   if (node2tile(w->ref[0], &x, &y) == -1)
      return 1;
   write_tile_conf(x, y);

   if (node2tile(w->ref[w->ref_cnt - 1], &x, &y) == -1)
      return 1;
   write_tile_conf(x, y);
   */

   return 0;
}


int act_neighbortile_fini(smrule_t *r)
{
   list_t *elem, *first = r->data;
   int x, y;

   for (elem = li_first(first); elem != li_head(first); elem = elem->next)
   {
      tile_ptr_xy(elem->data, &x, &y);
      write_tile_conf(x, y);
   }
   li_destroy(first, NULL);
   r->data = NULL;

   return 0;
}


#ifdef TEST_SMTILE
/*! Test routine.
 *  Compile with:
 *  gcc -Wall -I../libsmrender -I.. -DHAVE_CONFIG_H -lgd -lm -lsmrender -DTEST_SMTILE smtile.c
 *  Result:
 *  lat = 47.330000, lon = 15.230000, zoom = 12
 *  x = 2221, y = 1435
 *  lat = 47.338823, lon = 15.205078, zoom = 12
 *  x = 2222, y = 1436
 *  lat = 47.279229, lon = 15.292969, zoom = 12
 */
int main(int argc, char **argv)
{
   double lat, lon;
   int x, y, zoom;

   zoom = 12;
   lat = 47.33;
   lon = 15.23;

   printf("lat = %f, lon = %f, zoom = %d\n", lat, lon, zoom);

   x = lon2tile(lon, zoom);
   y = lat2tile(lat, zoom);
   printf("x = %d, y = %d\n", x, y);

   lat = tile2lat(y, zoom);
   lon = tile2lon(x, zoom);
   printf("lat = %f, lon = %f, zoom = %d\n", lat, lon, zoom);

   x++, y++;
   printf("x = %d, y = %d\n", x, y);

   lat = tile2lat(y, zoom);
   lon = tile2lon(x, zoom);
   printf("lat = %f, lon = %f, zoom = %d\n", lat, lon, zoom);

   return 0;
}
#endif

