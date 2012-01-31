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
#include "smrender.h"

#define MIN10(x) round((x) * 600)


void geo_description(double lat, double lon, char *text, char *pos)
{
   struct onode *n;

   n = malloc_object(4, 0);
   n->nd.type = OSM_NODE;
   n->nd.id = unique_node_id();
   n->nd.tim = time(NULL);
   n->nd.ver = 1;
   n->nd.lat = lat;
   n->nd.lon = lon;
   set_const_tag(&n->otag[0], "generator", "smrender");
   set_const_tag(&n->otag[1], "grid", "text");
   set_const_tag(&n->otag[2], "name", text);
   set_const_tag(&n->otag[3], "border", pos);
   put_object(n);
}


void geo_square(struct rdata *rd, double b, char *v)
{
   char buf[256];
   double lat[4] = {rd->y1c - MM2LAT(b), rd->y1c - MM2LAT(b), rd->y2c + MM2LAT(b), rd->y2c + MM2LAT(b)};
   double lon[4] = {rd->x1c + MM2LON(b), rd->x2c - MM2LON(b), rd->x2c - MM2LON(b), rd->x1c + MM2LON(b)};
   struct onode *n, *w;
   int i;

   w = malloc_object(2, 5);
   w->nd.type = OSM_WAY;
   w->nd.id = unique_way_id();
   w->nd.tim = time(NULL);
   w->nd.ver = 1;
   set_const_tag(&w->otag[0], "generator", "smrender");
   set_const_tag(&w->otag[1], "grid", v);
   put_object(w);

   for (i = 0; i < 4; i++)
   {
      n = malloc_object(5, 0);
      n->nd.type = OSM_NODE;
      w->ref[i] = n->nd.id = unique_node_id();
      n->nd.tim = time(NULL);
      n->nd.ver = 1;
      n->nd.lat = lat[i];
      n->nd.lon = lon[i];
      set_const_tag(&n->otag[0], "generator", "smrender");
      set_const_tag(&n->otag[1], "grid", v);
      snprintf(buf, sizeof(buf), "%02.0f %c %.1f'", lat[i], lat[i] < 0 ? 'S' : 'N', (double) ((int) round(lat[i] * 600) % 600) / 10);
      set_const_tag(&n->otag[2], "lat", strdup(buf));
      snprintf(buf, sizeof(buf), "%03.0f %c %.1f'", lon[i], lon[i] < 0 ? 'W' : 'E', (double) ((int) round(lon[i] * 600) % 600) / 10);
      set_const_tag(&n->otag[3], "lon", strdup(buf));
      snprintf(buf, sizeof(buf), "%d", i);
      set_const_tag(&n->otag[4], "pointindex", strdup(buf));
      put_object(n);
      log_debug("grid polygon lat/lon = %.8f/%.8f", n->nd.lat, n->nd.lon);
   }

   w->ref[4] = w->ref[0];
}


void geo_tick(double lat1, double lon1, double lat2, double lon2, char *v)
{
   struct onode *n, *w;

   w = malloc_object(2, 2);
   w->nd.type = OSM_WAY;
   w->nd.id = unique_way_id();
   w->nd.tim = time(NULL);
   w->nd.ver = 1;
   set_const_tag(&w->otag[0], "generator", "smrender");
   //set_const_tag(&w->otag[1], "grid", lon % t ? "subtick" : "tick");
   set_const_tag(&w->otag[1], "grid", v);
   put_object(w);

   n = malloc_object(1, 0);
   n->nd.type = OSM_NODE;
   w->ref[0] = n->nd.id = unique_node_id();
   n->nd.tim = time(NULL);
   n->nd.ver = 1;
   //n->nd.lat = rd->y1c - b3;
   n->nd.lat = lat1;
   //n->nd.lon = (double) lon / 600.0;
   n->nd.lon = lon1;
   set_const_tag(&n->otag[0], "generator", "smrender");
   put_object(n);
 
   n = malloc_object(1, 0);
   n->nd.type = OSM_NODE;
   w->ref[1] = n->nd.id = unique_node_id();
   n->nd.tim = time(NULL);
   n->nd.ver = 1;
   //n->nd.lat = rd->y1c - ((lon % t) ? b2 : b1);
   n->nd.lat = lat2;
   //n->nd.lon = (double) lon / 600.0;
   n->nd.lon = lon2;
   set_const_tag(&n->otag[0], "generator", "smrender");
   put_object(n);
}


/*! @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param b2 Middle line (mm)
 *  @param b3 Inner border (mm)
 *  @param t Ticks in tenths of a minute (i.e. 600 = 1').
 *  @param st subticks in tenths of a minute.
 */
void geo_lon_ticks(struct rdata *rd, double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lon;
   char buf[32], *s;

   bi = (lround((b + rd->x1c) * 600.0) / st) * st;
   log_msg(LOG_DEBUG, "g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lon = bi + st; lon < (rd->x2c - b) * 600; lon += st)
   {
      if (lon % g)
      {
         geo_tick(rd->y1c - b3, (double) lon / 600.0, rd->y1c - ((lon % t) ? b2 : b1), (double) lon / 600.0, lon % t ? "subtick" : "tick");
         geo_tick(rd->y2c + b3, (double) lon / 600.0, rd->y2c + ((lon % t) ? b2 : b1), (double) lon / 600.0, lon % t ? "subtick" : "tick");
      }
      else
      {
         geo_tick(rd->y2c + b1, (double) lon / 600.0, rd->y1c - b1, (double) lon / 600.0, "grid");

         snprintf(buf, sizeof(buf), "%03d° %02d'", lon / 600, (lon % 600) / 10);
         s = strdup(buf);
         geo_description(rd->y1c - b2, (double) lon / 600.0, s, "top");
         geo_description(rd->y2c + b2, (double) lon / 600.0, s, "bottom");

      }
   }
}


/*! @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param b2 Middle line (mm)
 *  @param b3 Inner border (mm)
 *  @param t Ticks in tenths of a minute (i.e. 600 = 1').
 *  @param st subticks in tenths of a minute.
 */
void geo_lat_ticks(struct rdata *rd, double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lat;
   char buf[32], *s;

   bi = (lround((b + rd->y2c) * 600) / st) * st;
   log_msg(LOG_DEBUG, "g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lat = bi + st; lat < (rd->y1c - b) * 600; lat += st)
   {
      //log_debug("grid: lat = %d", lat);
      if (lat % g)
      {
         geo_tick((double) lat / 600.0, rd->x1c + b3, (double) lat / 600.0,
               rd->x1c + ((lat % t) ? b2 : b1), lat % t ? "subtick" : "tick");
         geo_tick((double) lat / 600.0, rd->x2c - b3, (double) lat / 600.0,
               rd->x2c - ((lat % t) ? b2 : b1), lat % t ? "subtick" : "tick");
      }
      else
      {
         geo_tick((double) lat / 600.0, rd->x2c - b1, (double) lat / 600.0,
               rd->x1c + b1, "grid");

         snprintf(buf, sizeof(buf), "%02d° %02d'", lat / 600, (lat % 600) / 10);
         s = strdup(buf);
         geo_description((double) lat / 600.0, rd->x2c - b2, s, "right");
         geo_description((double) lat / 600.0, rd->x1c + b2, s, "left");
//         snprintf(buf, sizeof(buf), "%02.1f'", (double) (lat % 600) / 10);
//         s = strdup(buf);
//         geo_description((double) lat / 600.0, rd->x2c - b3, s, "en");
//         geo_description((double) lat / 600.0, rd->x1c + b3, s, "wn");
      }
   }
}


void geo_legend(struct rdata *rd)
{
   char buf[256], *s;
   int lat;

   lat = rd->mean_lat * 600.0;
   snprintf(buf, sizeof(buf), "Mean Latitude %02d %c %.1f', Scale = 1:%.0f, %.1f x %.1f mm", lat / 600, lat < 0 ? 'S' : 'N', (double) (lat % 600) / 10, rd->scale, PX2MM(rd->w) - 2 * G_MARGIN, PX2MM(rd->h) - 2 * G_MARGIN);
   s = strdup(buf);
   geo_description(rd->y1c - MM2LAT(G_MARGIN), rd->x1c + rd->wc / 2, s, "top");
   geo_description(rd->y2c + MM2LAT(G_MARGIN + G_TW + G_STW), rd->x1c + rd->wc / 2, "Generated with /smrender/, author Bernhard R. Fischer, 2048R/5C5FFD47 &lt;bf@abenteuerland.at&gt;, data source: OSM.", "copyright");
}

/*! ...
 *  Karte im Maßstab 1:100 000 (Silba-Pag): grid 10', ticks 1', subticks 0.25'
 *  ...
 */
void grid2(struct rdata *rd)
{
   geo_square(rd, G_MARGIN, "outer_border");
   geo_square(rd, G_MARGIN + G_TW, "ticks_border");
   geo_square(rd, G_MARGIN + G_TW + G_STW, "subticks_border");

   geo_lon_ticks(rd, MM2LON(G_MARGIN + G_TW + G_STW), MM2LAT(G_MARGIN),
         MM2LAT(G_MARGIN + G_TW), MM2LAT(G_MARGIN + G_TW + G_STW),
         MIN10(rd->grd.lon_g), MIN10(rd->grd.lon_ticks), MIN10(rd->grd.lon_sticks));
   geo_lat_ticks(rd, MM2LAT(G_MARGIN + G_TW + G_STW), MM2LON(G_MARGIN),
         MM2LON(G_MARGIN + G_TW), MM2LON(G_MARGIN + G_TW + G_STW),
         MIN10(rd->grd.lat_g), MIN10(rd->grd.lat_ticks), MIN10(rd->grd.lat_sticks));

   geo_legend(rd);
}

