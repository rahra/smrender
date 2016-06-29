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

/*! This file contains the function for generating the virtual nodes/ways which
 * make up the grid, the legend, and the chart border.
 *
 * @author Bernhard R. Fischer
 */

#include <string.h>
#include <time.h>
#include "smrender_dev.h"


#define RULER_HEIGHT MM2LAT(2.0)


int act_ruler_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
   struct coord p;
   char buf[32];
   osm_node_t *n[4], *on[2];
   osm_way_t *w;
   double lon_diff, rsec;
   int rcnt, i, j;

   rsec = -1.0;
   (void) get_param("count", &rsec, r->act);
   rcnt = rsec <= 0.0 ? 5 : rsec;

   rsec = -1.0;
   (void) get_param("section", &rsec, r->act);
   if (rsec <= 0.0)
      rsec = 1.0;

   log_msg(LOG_INFO, "ruler sectioning = %.2f km x %d", rsec, rcnt);

   // FIXME: G_ macros should be replaced by variables
   p.lon = rd->bb.ll.lon + MM2LON(G_MARGIN + G_TW + G_STW * 3);
   p.lat = rd->bb.ll.lat + MM2LAT(G_MARGIN + G_TW + G_STW * 3);

   // 1° lat = 60 sm
   // 1° lon / cos(lat) = 60 sm -> 1 / (cos(lat) * 60) = 1 sm = 1.852km -> 1 / (cos(lat) * 60 * 1.852) = 1km

   //rsec = 1.0; // ruler sectioning -> 1 km
   //rcnt = 5;   // number of ruler sections -> 5
   lon_diff = (double) rsec / (60.0 * 1.852 * cos(DEG2RAD(p.lat)));

   log_msg(LOG_INFO, "generating ruler: %d sections, %f degrees lon", rcnt, lon_diff);

   on[0] = malloc_node(1);
   osm_node_default(on[0]);
   on[1] = malloc_node(2);
   osm_node_default(on[1]);
   set_const_tag(&on[1]->obj.otag[1], "distance", "0 km");

   on[0]->lat = p.lat;
   on[0]->lon = p.lon;
   on[1]->lat = p.lat + RULER_HEIGHT;
   on[1]->lon = p.lon;
 
   put_object((osm_obj_t*) on[0]);
   put_object((osm_obj_t*) on[1]);

   for (i = 0; i < rcnt; i++)
   {
      n[0] = on[0];
      n[3] = on[1];

      on[0] = n[1] = malloc_node(1);
      osm_node_default(n[1]);
      on[1] = n[2] = malloc_node(2);
      osm_node_default(n[2]);

      if (rsec < 1.0)
         snprintf(buf, sizeof(buf), "%d m", (int) ((i + 1) * rsec * 1000.0));
      else
         snprintf(buf, sizeof(buf), "%d km", (int) ((i + 1) * rsec));
      set_const_tag(&on[1]->obj.otag[1], "distance", strdup(buf));

      n[1]->lat = n[0]->lat;
      n[1]->lon = n[0]->lon + lon_diff;
      n[2]->lat = n[3]->lat;
      n[2]->lon = n[1]->lon;

      put_object((osm_obj_t*) n[1]);
      put_object((osm_obj_t*) n[2]);

      w = malloc_way(2, 5);
      osm_way_default(w);
      set_const_tag(&w->obj.otag[1], "ruler_style", i & 1 ? "transparent" : "fill");

      for (j = 0; j < 5; j++)
         w->ref[j] = n[j % 4]->obj.id;

      put_object((osm_obj_t*) w);
   }

   return 0;
}


void geo_description(double lat, double lon, char *text, char *pos)
{
   osm_node_t *n;

   n = malloc_node(4);
   osm_node_default(n);
   n->lat = lat;
   n->lon = lon;
   set_const_tag(&n->obj.otag[1], "grid", "text");
   set_const_tag(&n->obj.otag[2], "name", text);
   set_const_tag(&n->obj.otag[3], "border", pos);
   put_object((osm_obj_t*) n);
}


void grid_date(struct rdata *rd, const struct grid *grd)
{
   osm_node_t *n;
   char buf[256];

   n = malloc_node(2);
   osm_node_default(n);
   n->lat = rd->bb.ll.lat + MM2LAT(grd->g_margin - grd->g_stw);
   n->lon = rd->bb.ll.lon + MM2LON(grd->g_margin);
   strftime(buf, sizeof(buf), "%e. %b. %Y, %R", localtime(&n->obj.tim));
   set_const_tag(&n->obj.otag[1], "chartdate", strdup(buf));
   put_object((osm_obj_t*) n);
}


void geo_square(struct rdata *rd, double b, char *v)
{
   char buf[256];
   double lat[4] = {rd->bb.ru.lat - MM2LAT(b), rd->bb.ru.lat - MM2LAT(b), rd->bb.ll.lat + MM2LAT(b), rd->bb.ll.lat + MM2LAT(b)};
   double lon[4] = {rd->bb.ll.lon + MM2LON(b), rd->bb.ru.lon - MM2LON(b), rd->bb.ru.lon - MM2LON(b), rd->bb.ll.lon + MM2LON(b)};
   osm_node_t *n;
   osm_way_t *w;
   int i;

   if (rd->polygon_window)
      for (int i = 0; i < 4; i++)
      {
         lat[i] = rd->pw[3 - i].lat;
         lon[i] = rd->pw[3 - i].lon;
      }

   w = malloc_way(2, 5);
   osm_way_default(w);
   set_const_tag(&w->obj.otag[1], "grid", v);
   put_object((osm_obj_t*) w);

   for (i = 0; i < 4; i++)
   {
      n = malloc_node(5);
      osm_node_default(n);
      w->ref[i] = n->obj.id;
      n->lat = lat[i];
      n->lon = lon[i];
      set_const_tag(&n->obj.otag[1], "grid", v);
      coord_str(lat[i], LAT_CHAR, buf, sizeof(buf));
      set_const_tag(&n->obj.otag[2], "lat", strdup(buf));
      coord_str(lon[i], LON_CHAR, buf, sizeof(buf));
      set_const_tag(&n->obj.otag[3], "lon", strdup(buf));
      snprintf(buf, sizeof(buf), "%d", i);
      set_const_tag(&n->obj.otag[4], "pointindex", strdup(buf));
      put_object((osm_obj_t*) n);
      log_debug("grid polygon lat/lon = %.8f/%.8f", n->lat, n->lon);
   }

   w->ref[4] = w->ref[0];
}


void geo_tick(double lat1, double lon1, double lat2, double lon2, char *v)
{
   osm_node_t *n;
   osm_way_t *w;

   w = malloc_way(2, 2);
   osm_way_default(w);
   set_const_tag(&w->obj.otag[1], "grid", v);
   put_object((osm_obj_t*) w);

   n = malloc_node(1);
   osm_node_default(n);
   w->ref[0] = n->obj.id;
   n->lat = lat1;
   n->lon = lon1;
   put_object((osm_obj_t*) n);
 
   n = malloc_node(1);
   osm_node_default(n);
   w->ref[1] = n->obj.id;
   n->lat = lat2;
   n->lon = lon2;
   put_object((osm_obj_t*) n);
}


/*! @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param b2 Middle line (mm)
 *  @param b3 Inner border (mm)
 *  @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 *  @param st subticks in tenths of a minute.
 */
void geo_lon_ticks(struct rdata *rd, double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lon;
   char buf[32], *s;

   bi = (lround((b + rd->bb.ll.lon) * T_RESCALE) / st) * st;
   log_msg(LOG_DEBUG, "g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lon = bi + st; lon < (rd->bb.ru.lon - b) * T_RESCALE; lon += st)
   {
      if (lon % g)
      {
         geo_tick(rd->bb.ru.lat - b3, (double) lon / T_RESCALE, rd->bb.ru.lat - ((lon % t) ? b2 : b1), (double) lon / T_RESCALE, lon % t ? "subtick" : "tick");
         geo_tick(rd->bb.ll.lat + b3, (double) lon / T_RESCALE, rd->bb.ll.lat + ((lon % t) ? b2 : b1), (double) lon / T_RESCALE, lon % t ? "subtick" : "tick");
      }
      else
      {
         geo_tick(rd->bb.ll.lat + b1, (double) lon / T_RESCALE, rd->bb.ru.lat - b1, (double) lon / T_RESCALE, "grid");

         coord_str((double) lon / T_RESCALE, LON_DEG, buf, sizeof(buf));
         s = strdup(buf);
         geo_description(rd->bb.ru.lat - b2, (double) lon / T_RESCALE, s, "top");
         geo_description(rd->bb.ll.lat + b2, (double) lon / T_RESCALE, s, "bottom");

      }
   }
}


/*! @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param b2 Middle line (mm)
 *  @param b3 Inner border (mm)
 *  @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 *  @param st subticks in tenths of a minute.
 */
void geo_lat_ticks(struct rdata *rd, double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lat;
   char buf[32], *s;

   bi = (lround((b + rd->bb.ll.lat) * T_RESCALE) / st) * st;
   log_msg(LOG_DEBUG, "g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lat = bi + st; lat < (rd->bb.ru.lat - b) * T_RESCALE; lat += st)
   {
      //log_debug("grid: lat = %d", lat);
      if (lat % g)
      {
         geo_tick((double) lat / T_RESCALE, rd->bb.ll.lon + b3, (double) lat / T_RESCALE,
               rd->bb.ll.lon + ((lat % t) ? b2 : b1), lat % t ? "subtick" : "tick");
         geo_tick((double) lat / T_RESCALE, rd->bb.ru.lon - b3, (double) lat / T_RESCALE,
               rd->bb.ru.lon - ((lat % t) ? b2 : b1), lat % t ? "subtick" : "tick");
      }
      else
      {
         geo_tick((double) lat / T_RESCALE, rd->bb.ru.lon - b1, (double) lat / T_RESCALE,
               rd->bb.ll.lon + b1, "grid");

         coord_str((double) lat / T_RESCALE, LAT_DEG, buf, sizeof(buf));
         s = strdup(buf);
         geo_description((double) lat / T_RESCALE, rd->bb.ru.lon - b2, s, "right");
         geo_description((double) lat / T_RESCALE, rd->bb.ll.lon + b2, s, "left");
//         snprintf(buf, sizeof(buf), "%02.1f'", (double) (lat % T_RESCALE) / 10);
//         s = strdup(buf);
//         geo_description((double) lat / T_RESCALE, rd->bb.ru.lon - b3, s, "en");
//         geo_description((double) lat / T_RESCALE, rd->bb.ll.lon + b3, s, "wn");
      }
   }
}


void geo_legend(struct rdata *rd, const struct grid *grd)
{
   char buf[256], *s;
   int lat;

   lat = rd->mean_lat * T_RESCALE;
   snprintf(buf, sizeof(buf), "Mean Latitude %02d %c %.1f', Scale = 1:%.0f, %.1f x %.1f mm", lat / T_RESCALE, lat < 0 ? 'S' : 'N', (double) (lat % T_RESCALE) / TM_RESCALE, rd->scale, PX2MM(rd->w) - 2 * grd->g_margin, PX2MM(rd->h) - 2 * grd->g_margin);
   s = strdup(buf);
   geo_description(rd->bb.ru.lat - MM2LAT(grd->g_margin), rd->bb.ll.lon + rd->wc / 2, s, "top");
   geo_description(rd->bb.ru.lat - MM2LAT(grd->g_margin), rd->bb.ll.lon + MM2LON(grd->g_margin), rd->title, "title");
   if (grd->copyright)
      geo_description(rd->bb.ll.lat + MM2LAT(grd->g_margin + grd->g_tw + grd->g_stw), rd->bb.ll.lon + rd->wc / 2, "Generated with " PACKAGE_STRING ", author Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>, data source: OSM.", "copyright");
   if (grd->cmdline)
      geo_description(rd->bb.ll.lat + MM2LAT(grd->g_margin - grd->g_tw), rd->bb.ll.lon + rd->wc / 2, rd->cmdline, "copyright");
}

/*! ...
 *  Karte im Maßstab 1:100 000 (Silba-Pag): grid 10', ticks 1', subticks 0.25'
 *  ...
 */
void grid(struct rdata *rd, const struct grid *grd)
{
   log_msg(LOG_INFO, "grid parameters: margin = %.2f mm, tickswidth = %.2f mm, "
         "substickswidth = %.2f mm, grid = %.2f', ticks = %.2f', subticks = %.2f'",
         grd->g_margin, grd->g_tw, grd->g_stw, grd->lon_g * 60.0,
         grd->lon_ticks * 60.0, grd->lon_sticks * 60.0);
 
   geo_square(rd, grd->g_margin, "outer_border");
   geo_square(rd, grd->g_margin + grd->g_tw, "ticks_border");
   geo_square(rd, grd->g_margin + grd->g_tw + grd->g_stw, "subticks_border");

   grid_date(rd, grd);

   geo_lon_ticks(rd, MM2LON(grd->g_margin + grd->g_tw + grd->g_stw), MM2LAT(grd->g_margin),
         MM2LAT(grd->g_margin + grd->g_tw), MM2LAT(grd->g_margin + grd->g_tw + grd->g_stw),
         MIN10(grd->lon_g), MIN10(grd->lon_ticks), MIN10(grd->lon_sticks));
   geo_lat_ticks(rd, MM2LAT(grd->g_margin + grd->g_tw + grd->g_stw), MM2LON(grd->g_margin),
         MM2LON(grd->g_margin + grd->g_tw), MM2LON(grd->g_margin + grd->g_tw + grd->g_stw),
         MIN10(grd->lat_g), MIN10(grd->lat_ticks), MIN10(grd->lat_sticks));

   geo_legend(rd, grd);
}


void init_grid(struct grid *grd)
{
   memset(grd, 0, sizeof(*grd));
   grd->g_margin = G_MARGIN;
   grd->g_tw = G_TW;
   grd->g_stw = G_STW;
   grd->copyright = 1;
   grd->cmdline = 1;
}


void auto_grid(const struct rdata *rd, struct grid *grd)
{
   log_debug("setting auto grid values");
   if (rd->scale >= 250000)
   {
      grd->lat_ticks = grd->lon_ticks = MIN2DEG(1);
      grd->lat_sticks = grd->lon_sticks = MIN2DEG(0.5);
      grd->lat_g = grd->lon_g = MIN2DEG(30);
   }
   else if (rd->scale >= 90000)
   {
      grd->lat_ticks = grd->lon_ticks = MIN2DEG(1);
      grd->lat_sticks = grd->lon_sticks = MIN2DEG(0.2);
      grd->lat_g = grd->lon_g = MIN2DEG(10);
   }
   else
   {
      grd->lat_ticks = grd->lon_ticks = MIN2DEG(1);
      grd->lat_sticks = grd->lon_sticks = MIN2DEG(0.2);
      grd->lat_g = grd->lon_g = MIN2DEG(5);
   }
}


int act_grid_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
   double ticks, sticks, g;
   struct grid grd;

   init_grid(&grd);
   auto_grid(rd, &grd);

   log_debug("parsing grid params");
   (void) get_param("margin", &grd.g_margin, r->act);
   (void) get_param("tickswidth", &grd.g_tw, r->act);
   grd.g_tw = grd.g_tw <= 0.0 ? G_TW : grd.g_tw;
   (void) get_param("subtickswidth", &grd.g_stw, r->act);
   grd.g_stw = grd.g_stw <= 0.0 ? G_STW : grd.g_stw;
   ticks = sticks = g = 0.0;
   (void) get_param("grid", &g, r->act);
   if (g > 0.0)
      grd.lat_g = grd.lon_g = MIN2DEG(g);
   (void) get_param("ticks", &ticks, r->act);
   if (ticks > 0.0)
      grd.lat_ticks = grd.lon_ticks = MIN2DEG(ticks);
   (void) get_param("subticks", &sticks, r->act);
   if (sticks > 0.0)
      grd.lat_sticks = grd.lon_sticks = MIN2DEG(sticks);

   (void) get_parami("copyright", &grd.copyright, r->act);
   (void) get_parami("cmdline", &grd.cmdline, r->act);

   grid(rd, &grd);

   return 0;
}

