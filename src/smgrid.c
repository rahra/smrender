/* Copyright 2011-2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smgrid.c
 * This file contains the function for generating the virtual nodes/ways which
 * make up the grid, the legend, and the chart border.
 *
 * @author Bernhard R. Fischer
 * @date 2023/12/18
 */

#include <string.h>
#include <time.h>
#include <errno.h>
#include "smrender_dev.h"
#include "smem.h"
#include "smcore.h"


#define RULER_HEIGHT MM2LAT(2.0)


int ruler(struct rdata *rd, ruler_t *rl)
{
   struct coord p;
   char buf[32];
   osm_node_t *n[4], *on[2];
   osm_way_t *w;
   double lon_diff;
   int i, j;

   // FIXME: G_ macros should be replaced by variables
   p.lon = rd->bb.ll.lon + MM2LON(G_MARGIN + G_TW + G_STW * 3);
   p.lat = rd->bb.ll.lat + MM2LAT(G_MARGIN + G_TW + G_STW * 3);

//   if (rd->proj == PROJ_TRANSVERSAL)
//      transtraversal(-rd->transversal_lat, rd->mean_lon, &p.lat, &p.lon);

   // 1° lat = 60 sm
   // 1° lon / cos(lat) = 60 sm -> 1 / (cos(lat) * 60) = 1 sm = 1.852km -> 1 / (cos(lat) * 60 * 1.852) = 1km

   //rsec = 1.0; // ruler sectioning -> 1 km
   //rcnt = 5;   // number of ruler sections -> 5
   lon_diff = rl->rsec / (60.0 * 1.852 * cos(DEG2RAD(p.lat)));

   log_msg(LOG_INFO, "generating ruler: %d sections, %f degrees lon", rl->rcnt, lon_diff);

   on[0] = malloc_node(1);
   osm_node_default(on[0]);
   on[1] = malloc_node(2);
   osm_node_default(on[1]);
   set_const_tag(&on[1]->obj.otag[1], "distance", rl->unit ? "0 nm" : "0 km");

   on[0]->lat = p.lat;
   on[0]->lon = p.lon;
   on[1]->lat = p.lat + RULER_HEIGHT;
   on[1]->lon = p.lon;
 
   put_object((osm_obj_t*) on[0]);
   put_object((osm_obj_t*) on[1]);

   for (i = 0; i < rl->rcnt; i++)
   {
      n[0] = on[0];
      n[3] = on[1];

      on[0] = n[1] = malloc_node(1);
      osm_node_default(n[1]);
      on[1] = n[2] = malloc_node(2);
      osm_node_default(n[2]);

      if (rl->rsec < 1.0)
         snprintf(buf, sizeof(buf), "%d m", (int) ((i + 1) * rl->rsec * 1000.0));
      else
      {
         if (!rl->unit)
            snprintf(buf, sizeof(buf), "%d km", (int) ((i + 1) * rl->rsec));
         else
            snprintf(buf, sizeof(buf), "%d nm", (int) ((i + 1) * rl->rsec / 1.852));
      }
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


int ruler_ini(smrule_t *r)
{
   ruler_t *rl;

   if ((rl = malloc(sizeof(*rl))) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
      return -1;
   }
   memset(rl, 0, sizeof(*rl));

   rl->rsec = 1;
   get_param("section", &rl->rsec, r->act);
   if (rl->rsec <= 0)
   {
      log_msg(LOG_WARN, "resetting negative section value");
      rl->rsec = 1;
   }

   rl->rcnt = 5;
   get_parami("count", &rl->rcnt, r->act);
   if (rl->rcnt < 1)
      rl->rcnt = 5;

   rl->unit = get_param_bool("nautical", r->act);
   if (rl->unit)
      rl->rsec *= 1.852;

   log_msg(LOG_INFO, "ruler sectioning = %.2f km x %d, unit = %d", rl->rsec, rl->rcnt, rl->unit);

   r->data = rl;
   return 0;
}


int act_ruler_ini(smrule_t *r)
{
   if (ruler_ini(r) == -1)
      return -1;

   return ruler(get_rdata(), r->data);
}


int act_ruler_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


int act_ruler2_ini(smrule_t *r)
{
   sm_set_flag(r, ACTION_EXEC_ONCE);
   return ruler_ini(r);
}


int act_ruler2_main(smrule_t *r, osm_obj_t *UNUSED(o))
{
   ruler_t *rl = r->data;

   return ruler(get_rdata(), rl);
}


int act_ruler2_fini(smrule_t *r)
{
   return act_ruler_fini(r);
}


/*! This function calculates the value yn at xn in accordance to a line running
 * from coordinates x0/y0 to x1/y1.
 */
double intermediate(double x0, double y0, double x1, double y1, double xn)
{
   // DIV0 safety check
   if (x1 - x0 == 0.0)
      return y0;

   return y0 + (xn - x0) / (x1 - x0) * (y1 - y0);
}


/*! Calculate the degrees of longitude of the page at a specific latitude. This
 * is always the same for Mercator, but different for Transversal Mercator.
 */
static double lonlen_at_lat(const struct coord *pw, double lat)
{
   return intermediate(pw[0].lat, pw[1].lon - pw[0].lon, pw[3].lat, pw[2].lon - pw[3].lon, lat);
}


static double latlen_at_lon(const struct coord *pw, double lon)
{
   return intermediate(pw[3].lon, pw[3].lat - pw[0].lat, pw[2].lon, pw[2].lat - pw[1].lat, lon);
}


void geo_description(double lat, double lon, char *text, const char *pos)
{
   osm_node_t *n;

   n = malloc_node(4);
   osm_node_default(n);
   n->lat = lat;
   n->lon = lon;
   set_const_tag(&n->obj.otag[1], "grid", "text");
   set_const_tag(&n->obj.otag[2], "name", text);
   set_const_tag(&n->obj.otag[3], "border", strdup(pos));
   put_object((osm_obj_t*) n);
}


void grid_date(const bbox_t *bb, const struct grid *grd)
{
   osm_node_t *n;
   char buf[256];

   n = malloc_node(2);
   osm_node_default(n);
   n->lat = bb->ll.lat + MM2LAT(grd->g_margin - grd->g_stw);
   n->lon = bb->ll.lon + MM2LON(grd->g_margin);
   strftime(buf, sizeof(buf), "%e. %b. %Y, %R", localtime(&n->obj.tim));
   set_const_tag(&n->obj.otag[1], "chartdate", strdup(buf));
   put_object((osm_obj_t*) n);
}


// return 1 if x is First(0) or Last(3) (of 4)
#define FL(x) ((x) == 0 || (x) == 3)
// return 1 if x is First(0) or Second(1) (of 4)
#define F2(x) ((x) == 0 || (x) == 1)
void geo_square(const struct coord *pw0, double b, char *v, int cnt)
{
   char buf[256];
   struct coord pw[4];
   osm_node_t *n;
   osm_way_t *w;
   int i, j;
   double dlat, dlon;

   for (int i = 0; i < 4; i++)
   {
      pw[i] = pw0[3 - i];
      pw[i].lat += MM2LAT0(b, pw0[FL(i) ? 3 : 2].lat - pw0[FL(i) ? 0 : 1].lat) * (F2(i) ? -1 : 1);
      pw[i].lon += MM2LON0(b, pw0[F2(i) ? 2 : 1].lon - pw0[F2(i) ? 3 : 0].lon) * (FL(i) ? 1 : -1);
   }

   w = malloc_way(2, 4 * cnt + 1);
   osm_way_default(w);
   set_const_tag(&w->obj.otag[1], "grid", v);

   for (i = 0; i < 4; i++)
   {
      n = malloc_node(5);
      osm_node_default(n);
      w->ref[i * cnt] = n->obj.id; // FIXME: insert_refs() must be used instead!
      n->lat = pw[i].lat;
      n->lon = pw[i].lon;
      set_const_tag(&n->obj.otag[1], "grid", v);
      coord_str(pw[i].lat, LAT_CHAR, buf, sizeof(buf));
      set_const_tag(&n->obj.otag[2], "lat", strdup(buf));
      coord_str(pw[i].lon, LON_CHAR, buf, sizeof(buf));
      set_const_tag(&n->obj.otag[3], "lon", strdup(buf));
      snprintf(buf, sizeof(buf), "%d", i);
      set_const_tag(&n->obj.otag[4], "pointindex", strdup(buf));
      put_object((osm_obj_t*) n);
      log_debug("border polygon lat/lon = %.8f/%.8f, \"%s\"", n->lat, n->lon, v);

      j = (i + 1) % 4;
      dlat = (pw[j].lat - pw[i].lat) / (cnt - 1);
      dlon = (pw[j].lon - pw[i].lon) / (cnt - 1);
      for (j = 1; j < cnt; j++)
      {
         n = malloc_node(1);
         osm_node_default(n);
         w->ref[i * cnt + j] = n->obj.id; // FIXME: insert_refs() must be used instead!
         n->lat = pw[i].lat + dlat * j;
         n->lon = pw[i].lon + dlon * j;
         put_object((osm_obj_t*) n);
      }
   }

   w->ref[4 * cnt] = w->ref[0];
   put_object((osm_obj_t*) w);
}


void geo_tick0(double lat1, double lon1, double lat2, double lon2, char *v, int cnt)
{
   double dlat, dlon;
   osm_node_t *n;
   osm_way_t *w;

   // safety check
   if (cnt < 2)
      cnt = 2;

   w = malloc_way(2, 0);
   osm_way_default(w);
   set_const_tag(&w->obj.otag[1], "grid", v);
   put_object((osm_obj_t*) w);

   dlat = (lat2 - lat1) / (cnt - 1);
   dlon = (lon2 - lon1) / (cnt - 1);
   for (int i = 0; i < cnt; i++)
   {
      n = malloc_node(1);
      osm_node_default(n);
      n->lat = lat1 + dlat * i;
      n->lon = lon1 + dlon * i;
      put_object((osm_obj_t*) n);
      insert_refs(w, &n, 1, w->ref_cnt);
   }
}


void geo_tick(double lat1, double lon1, double lat2, double lon2, char *v)
{
   geo_tick0(lat1, lon1, lat2, lon2, v, 2);
}


/*! Generate longitude ticks within top and bottom border.
 *  @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param b2 Middle line (mm)
 *  @param b3 Inner border (mm)
 *  @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 *  @param st subticks in tenths of a minute.
 */
void geo_lon_ticks0(const struct coord *pw, int c0, int c1, const char *desc,  double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lon;
   char buf[32], *s;
   double latf, lonf, latm;

   bi = (lround((MM2LON0(b, pw[c1].lon - pw[c0].lon) + pw[c0].lon) * T_RESCALE) / st) * st;
   log_debug("g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lon = bi + st; lon < (pw[c1].lon - MM2LON0(b, pw[c1].lon - pw[c0].lon)) * T_RESCALE; lon += st)
   {
      lonf = (double) lon / T_RESCALE;
      latf = intermediate(pw[c0].lon, pw[c0].lat, pw[c1].lon, pw[c1].lat, lonf);
      latm = latlen_at_lon(pw, lonf);
      log_debug("latf = %.3f, lonf = %.3f, latm = %.3f", latf, lonf, latm);

      geo_tick(latf + MM2LAT0(b3, latm), lonf, latf + MM2LAT0((lon % t) ? b2 : b1, latm), lonf, lon % t ? "subtick" : "tick");

      if (!(lon % g))
      {
         coord_str(lonf, (double) g / T_RESCALE < 1 ? LON_DEG : LON_DEG_ONLY, buf, sizeof(buf));
         s = strdup(buf);
         geo_description(latf + MM2LAT0(b2, latm), lonf, s, desc);
      }
   }
}


void geo_lon_ticks(const struct coord *pw, double b, double b1, double b2, double b3, int g, int t, int st)
{
   geo_lon_ticks0(pw, 0, 1, "bottom", b, b1, b2, b3, g, t, st);
   geo_lon_ticks0(pw, 3, 2, "top", b, -b1, -b2, -b3, g, t, st);
}


/*! Generate latitude ticks within left and right border.
 * @param pw Array of corner points.
 * @param c0 Index to ower corner.
 * @param c1 Index to upper corner.
 * @param desc Caption to be added in the OSM tags.
 * @param b Longitude border, i.e. distance in mm from top/bottom of the page border.
 * @param b1 Outer border, i.e. distance in mm from the left/right of the page border.
 * @param b2 Middle line (mm).
 * @param b3 Inner border (mm).
 * @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 * @param st subticks in tenths of a minute.
 */
void geo_lat_ticks0(const struct coord *pw, int c0, int c1, const char *desc, double b, double b1, double b2, double b3, int g, int t, int st)
{
   int bi, lat;
   char buf[32], *s;
   double latf, lonf, lonm;

   bi = (lround((MM2LAT0(b, pw[c1].lat - pw[c0].lat) + pw[c0].lat) * T_RESCALE) / st) * st;
   log_debug("g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lat = bi + st; lat < (pw[c1].lat - MM2LAT0(b, pw[c1].lat - pw[c0].lat)) * T_RESCALE; lat += st)
   {
      latf = (double) lat / T_RESCALE;
      lonf = intermediate(pw[c0].lat, pw[c0].lon, pw[c1].lat, pw[c1].lon, latf);
      lonm = lonlen_at_lat(pw, latf);
      log_debug("latf = %.3f, lonf = %.3f, lonm = %.3f", latf, lonf, lonm);

      geo_tick(latf, lonf + MM2LON0(b3, lonm), latf, lonf + MM2LON0((lat % t) ? b2 : b1, lonm), lat % t ? "subtick" : "tick");

      if (!(lat % g))
      {
         coord_str(latf, (double) g / T_RESCALE < 1 ? LAT_DEG : LAT_DEG_ONLY, buf, sizeof(buf));
         s = strdup(buf);
         geo_description(latf, lonf + MM2LON0(b2, lonm), s, desc);
      }
   }
}


void geo_lat_ticks(const struct coord *pw, double b, double b1, double b2, double b3, int g, int t, int st)
{
   geo_lat_ticks0(pw, 0, 3, "left", b, b1, b2 ,b3, g, t, st);
   geo_lat_ticks0(pw, 1, 2, "right", b, -b1, -b2 ,-b3, g, t, st);
}


/*! Generate longitude grid lines.
 *  @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 *  @param st subticks in tenths of a minute.
 *  @param cnt Number of points of each gridline. It must be cnt >= 2.
 */
void geo_lon_grid(const struct coord *pw, double b, double b1, int g, int t, int st, int cnt)
{
   int bi, lon;
   double lonf, latf0, latf1, latm;

   bi = (lround((MM2LON0(b, pw[2].lon - pw[3].lon) + pw[3].lon) * T_RESCALE) / st) * st;
   log_debug("g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lon = bi + st; lon < (pw[2].lon - MM2LON0(b, pw[2].lon - pw[3].lon)) * T_RESCALE; lon += st)
   {
      if (!(lon % g))
      {
         lonf = (double) lon / T_RESCALE;
         if (lonf < pw[0].lon + MM2LON0(b, pw[1].lon - pw[0].lon))
         {
            log_debug("outside left");
            latf0 = intermediate(pw[0].lon + MM2LON0(b, pw[1].lon - pw[0].lon), pw[0].lat, pw[3].lon, pw[3].lat, lonf);
         }
         else if (lonf > pw[1].lon - MM2LON0(b, pw[1].lon - pw[0].lon))
         {
            log_debug("outside right");
            latf0 = intermediate(pw[1].lon - MM2LON0(b, pw[1].lon - pw[0].lon), pw[1].lat, pw[2].lon, pw[2].lat, lonf);
         }
         else
         {
            latf0 = intermediate(pw[0].lon, pw[0].lat, pw[1].lon, pw[1].lat, lonf);
         }
         latf1 = intermediate(pw[3].lon, pw[3].lat, pw[2].lon, pw[2].lat, lonf);
         latm = latlen_at_lon(pw, lonf);
         log_debug("lonf = %.2f, latf0 = %.2f, latf1 = %.2f", lonf, latf0, latf1);
         geo_tick0(latf0 + MM2LAT0(b1, latm), lonf, latf1 - MM2LAT0(b1, latm), lonf, "grid", cnt);
      }
   }
}


/*! Generate latitude grid lines.
 *  @param b Longitude border.
 *  @param b1 Outer border (mm)
 *  @param t Ticks in tenths of a minute (i.e. T_RESCALE = 1').
 *  @param st subticks in tenths of a minute.
 *  @param cnt Number of points of each gridline. It must be cnt >= 2.
 */
void geo_lat_grid(const struct coord *pw, double b, double b1, int g, int t, int st, int cnt)
{
   int bi, lat;
   double latf, lonf0, lonf1, lonm;

   bi = (lround((MM2LAT0(b, pw[3].lat - pw[0].lat) + pw[0].lat) * T_RESCALE) / st) * st;
   log_debug("g = %d, t = %d, st = %d, bi = %d", g, t, st, bi);

   for (lat = bi + st; lat < (pw[2].lat - MM2LAT0(b, pw[2].lat - pw[1].lat)) * T_RESCALE; lat += st)
   {
      if (!(lat % g))
      {
         latf = (double) lat / T_RESCALE;
         lonf0 = intermediate(pw[0].lat, pw[0].lon, pw[3].lat, pw[3].lon, latf);
         lonf1 = intermediate(pw[1].lat, pw[1].lon, pw[2].lat, pw[2].lon, latf);
         lonm = lonlen_at_lat(pw, latf);
         log_debug("latf = %.2f, lonf0 = %.2f, lonf1 = %.2f", latf, lonf0, lonf1);
         geo_tick0(latf, lonf1 - MM2LON0(b1, lonm), latf, lonf0 + MM2LON0(b1, lonm), "grid", cnt);
      }
   }
}


void geo_legend(const bbox_t *bb, struct rdata *rd, const struct grid *grd)
{
   char buf[256], *s;
   int lat;

   lat = rd->mean_lat * T_RESCALE;
   snprintf(buf, sizeof(buf), "Mean Latitude %02d %c %.1f', Scale = 1:%.0f, %.1f x %.1f mm", lat / T_RESCALE, lat < 0 ? 'S' : 'N', (double) (lat % T_RESCALE) / TM_RESCALE, rd->scale, PX2MM(rd->w) - 2 * grd->g_margin, PX2MM(rd->h) - 2 * grd->g_margin);
   s = strdup(buf);
   geo_description(bb->ru.lat - MM2LAT(grd->g_margin), bb->ll.lon + rd->wc / 2, s, "top");
   geo_description(bb->ru.lat - MM2LAT(grd->g_margin), bb->ll.lon + MM2LON(grd->g_margin), rd->title, "title");
   if (grd->copyright)
      geo_description(bb->ll.lat + MM2LAT(grd->g_margin + grd->g_tw + grd->g_stw), bb->ll.lon + rd->wc / 2, "Generated with " PACKAGE_STRING ", author Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>, data source: OSM.", "copyright");
   if (grd->cmdline)
      geo_description(bb->ll.lat + MM2LAT(grd->g_margin - grd->g_tw), bb->ll.lon + rd->wc / 2, rd->cmdline, "copyright");
}

/*! ...
 *  Karte im Maßstab 1:100 000 (Silba-Pag): grid 10', ticks 1', subticks 0.25'
 *  ...
 */
void grid(struct rdata *rd, const struct grid *grd)
{
   struct coord c[4], *pw = c;
   bbox_t bb = rd->bb;

   if (rd->proj == PROJ_TRANSVERSAL)
   {
      log_debug("transforming bounding box of grid");
      c[1].lat = bb.ll.lat;
      c[1].lon = bb.ru.lon;
      c[3].lat = bb.ru.lat;
      c[3].lon = bb.ll.lon;
      transtraversal(-rd->transversal_lat, rd->mean_lon, &bb.ll.lat, &bb.ll.lon);
      transtraversal(-rd->transversal_lat, rd->mean_lon, &bb.ru.lat, &bb.ru.lon);
      transtraversal(-rd->transversal_lat, rd->mean_lon, &c[1].lat, &c[1].lon);
      transtraversal(-rd->transversal_lat, rd->mean_lon, &c[3].lat, &c[3].lon);
      if (!grd->polygon_window)
         bb.ru.lon = c[1].lon;
   }

   if (rd->polygon_window)
   {
      pw = rd->pw;
   }
   else
   {
      c[0] = bb.ll;
      c[2] = bb.ru;

      if (!(rd->proj == PROJ_TRANSVERSAL && grd->polygon_window))
      {
         c[1].lat = c[0].lat;
         c[1].lon = c[2].lon;
         c[3].lat = c[2].lat;
         c[3].lon = c[0].lon;
      }
   }

   log_msg(LOG_INFO, "grid parameters: margin = %.2f mm, tickswidth = %.2f mm, "
         "substickswidth = %.2f mm, grid = %.2f', ticks = %.2f', subticks = %.2f'",
         grd->g_margin, grd->g_tw, grd->g_stw, grd->lon_g * 60.0,
         grd->lon_ticks * 60.0, grd->lon_sticks * 60.0);
   log_msg(LOG_INFO, "grid top    %.3f %.3f -- %.3f %.3f",
         bb.ru.lat, bb.ll.lon, bb.ru.lat, bb.ru.lon);
   log_msg(LOG_INFO, "grid bottom %.3f %.3f -- %.3f %.3f",
         bb.ll.lat, bb.ll.lon, bb.ll.lat, bb.ru.lon);
 
   geo_square(pw, grd->g_margin, "outer_border", grd->gpcnt);
   geo_square(pw, grd->g_margin + grd->g_tw, "ticks_border", grd->gpcnt);
   geo_square(pw, grd->g_margin + grd->g_tw + grd->g_stw, "subticks_border", grd->gpcnt);

   grid_date(&bb, grd);

   geo_lon_ticks(pw, grd->g_margin + grd->g_tw + grd->g_stw, grd->g_margin,
         grd->g_margin + grd->g_tw, grd->g_margin + grd->g_tw + grd->g_stw,
         MIN10(grd->lon_g), MIN10(grd->lon_ticks), MIN10(grd->lon_sticks));
   geo_lat_ticks(pw, grd->g_margin + grd->g_tw + grd->g_stw, grd->g_margin,
         grd->g_margin + grd->g_tw, grd->g_margin + grd->g_tw + grd->g_stw,
         MIN10(grd->lat_g), MIN10(grd->lat_ticks), MIN10(grd->lat_sticks));

   geo_lon_grid(pw, grd->g_margin + grd->g_tw + grd->g_stw, grd->g_margin,
         MIN10(grd->lon_g), MIN10(grd->lon_ticks), MIN10(grd->lon_sticks), grd->gpcnt);
   geo_lat_grid(pw, grd->g_margin + grd->g_tw + grd->g_stw, grd->g_margin,
         MIN10(grd->lat_g), MIN10(grd->lat_ticks), MIN10(grd->lat_sticks), grd->gpcnt);

   geo_legend(&bb, rd, grd);
}


void init_grid(struct grid *grd)
{
   memset(grd, 0, sizeof(*grd));
   grd->g_margin = G_MARGIN;
   grd->g_tw = G_TW;
   grd->g_stw = G_STW;
   grd->copyright = 1;
   grd->cmdline = 1;
   grd->gpcnt = 2;
}


typedef struct grid_autodef
{
   double scale, grid, ticks, subticks;
} grid_autodef_t;


/*! Automatically set grid parameters.
 */
void auto_grid(const struct rdata *rd, struct grid *grd)
{
   const grid_autodef_t gd[] =
   {
      {2500000, 300, 60, 10},
      { 250000,  30,  1,  0.5},
      {  90000,  10,  1,  0.2},
      {      0,   5,  1,  0.2},
      {     -1,   0,  0,  0}
   };

   log_debug("setting auto grid values");
   for (int i = 0; gd[i].scale >= 0; i++)
      if (rd->scale >= gd[i].scale)
      {
         log_debug("grid_autodef.scale = %.1f", gd[i].scale);
         grd->lat_g = grd->lon_g = MIN2DEG(gd[i].grid);
         grd->lat_ticks = grd->lon_ticks = MIN2DEG(gd[i].ticks);
         grd->lat_sticks = grd->lon_sticks = MIN2DEG(gd[i].subticks);
         break;
      }
}


/*! Initialize grid structure according to the config parameters in the grid
 * rule.
 */
int grid_ini(smrule_t *r, struct grid *grd)
{
   struct rdata *rd = get_rdata();
   double ticks, sticks, g;

   init_grid(grd);
   auto_grid(rd, grd);

   log_debug("parsing grid params");
   (void) get_param("margin", &grd->g_margin, r->act);
   (void) get_param("tickswidth", &grd->g_tw, r->act);
   grd->g_tw = grd->g_tw <= 0.0 ? G_TW : grd->g_tw;
   (void) get_param("subtickswidth", &grd->g_stw, r->act);
   grd->g_stw = grd->g_stw <= 0.0 ? G_STW : grd->g_stw;
   ticks = sticks = g = 0.0;
   (void) get_param("grid", &g, r->act);
   if (g > 0.0)
      grd->lat_g = grd->lon_g = MIN2DEG(g);
   (void) get_param("ticks", &ticks, r->act);
   if (ticks > 0.0)
      grd->lat_ticks = grd->lon_ticks = MIN2DEG(ticks);
   (void) get_param("subticks", &sticks, r->act);
   if (sticks > 0.0)
      grd->lat_sticks = grd->lon_sticks = MIN2DEG(sticks);

   grd->copyright = get_param_bool2("copyright", r->act, grd->copyright);
   grd->cmdline = get_param_bool2("cmdline", r->act, grd->cmdline);

   get_parami("gridpoints", &grd->gpcnt, r->act);
   if (grd->gpcnt < 2)
      grd->gpcnt = 2;

   grd->polygon_window = get_param_bool("polygon_window", r->act);

   log_debug("struct grid = {lat(%.1f:%.1f:%.1f), lon(%.1f:%.1f:%.1f), %.1f, %.1f, %.1f, %d, %d, %d}",
         grd->lat_g, grd->lat_ticks, grd->lat_sticks, grd->lon_g, grd->lon_ticks, grd->lon_sticks,
         grd->g_margin, grd->g_tw, grd->g_stw,
         grd->copyright, grd->cmdline, grd->gpcnt);

   return 0;
}


/*! Initialize grid structure.
 */
int act_grid2_ini(smrule_t *r)
{
   struct grid *grd;

   if ((grd = malloc(sizeof(*grd))) == NULL)
   {
      log_errno(LOG_ERR, "failed to get mem for grid data");
      return -1;
   }

   grid_ini(r, grd);
   r->data = grd;

   sm_set_flag(r, ACTION_EXEC_ONCE);
   return 0;
}


/*! Generate grid. The grid is always generated just once, independently how
 * often this function is called. The difference between grid() and grid2() is
 * only the time when the grid is generated during runtime.
 */
int act_grid2_main(smrule_t *r, osm_obj_t *UNUSED(o))
{
   grid(get_rdata(), r->data);
   return 1;
}


int act_grid2_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


/*! Initialize grid structure and generate grid immediately.
 */
int act_grid_ini(smrule_t *r)
{
   int e;

   if (!(e = act_grid2_ini(r)))
      grid(get_rdata(), r->data);

   return e;
}


int act_grid_main(smrule_t *UNUSED(r), osm_obj_t *UNUSED(o))
{
   return 0;
}


int act_grid_fini(smrule_t *r)
{
   return act_grid2_fini(r);
}


/*! Initialize grid structure according to the config parameters in the grid
 * rule.
 */
int act_global_grid_ini(smrule_t *r)
{
#define _GGRID(x, y) ((x) > 0.0 ? MIN2DEG(x) : (y))
   struct grid *grd;
   struct rdata *rd = get_rdata();
   double g;

   grd = sm_alloc(sizeof(*grd));
   init_grid(grd);
   auto_grid(rd, grd);

   (void) get_param("lat_grid", &g, r->act);
   grd->lat_g = _GGRID(g, grd->lat_g);
   (void) get_param("lon_grid", &g, r->act);
   grd->lon_g = _GGRID(g, grd->lon_g);

   if (get_param("grid", &g, r->act) != NULL)
   {
      if (get_param("lat_grid", NULL, r->act) == NULL && get_param("lon_grid", NULL, r->act) == NULL)
         grd->lat_g = grd->lon_g = _GGRID(g, grd->lat_g);
      else
         log_msg(LOG_WARN, "'grid' cannot be set together with 'lat_grid' or 'lon_grid', ignoring 'grid'");
   }

   get_parami("gridpoints", &grd->gpcnt, r->act);
   if (grd->gpcnt < 2)
      grd->gpcnt = 2;

   log_debug("lat_grid = %.1f, lon_grid = %.1f, gridpoints = %d", grd->lat_g, grd->lon_g, grd->gpcnt);
   r->data = grd;
   sm_set_flag(r, ACTION_EXEC_ONCE);
   return 0;
}


static double parallel_set_coords(double *lat, double *lon, double a0, double b)
{
   if (lat != NULL)
      *lat = a0;
   if (lon != NULL)
      *lon = lonmod(b);

   return a0;
}


static double meridian_set_coords(double *lat, double *lon, double a0, double b)
{
   if (lat != NULL)
      *lat = latmod(b);
   if (lon != NULL)
      *lon = b <= 90 || b > 270 ? a0 : lonmod(a0 + 180);

   return lonmod(a0);
}


static char sgnc(double a0, char p, char z, char n)
{
   if (a0 == 0.0)
      return z;
   if (a0 < 0.0)
      return n;
   //if (a0 > 0.0)
      return p;
}


static char dirc(double a0, const char *circt)
{
   if (!strncmp(circt, "parallel", 8))
      return sgnc(a0, 'N', ' ', 'S');
   if (!strncmp(circt, "meridian", 8))
      return sgnc(a0, 'E', ' ', 'W');
   return ' ';
}


/*! This function generates a generic geographic circle on the surface of the
 * Earth.
 */
osm_way_t *circle(double a0, double g, int cnt, char *circt, double (*cfunc)(double *, double*, double, double))
{
   osm_node_t *n;
   osm_way_t *w;
   char buf[32];

   cnt = cnt < 1 ? 1 : cnt;

   a0 = cfunc(NULL, NULL, a0, 0);
   w = malloc_way(5, 0);
   osm_way_default(w);
   set_const_tag(&w->obj.otag[1], "global_grid", "yes");
   set_const_tag(&w->obj.otag[2], "circle", circt);
   snprintf(buf, sizeof(buf), "%d", (int) a0);
   set_const_tag(&w->obj.otag[3], "deg", strdup(buf));
   snprintf(buf, sizeof(buf), "%d %c", abs((int) a0), dirc(a0, circt));
   set_const_tag(&w->obj.otag[4], "deg:naut", strdup(buf));

   for (double a = 0; a < 360; a += g)
   {
      double b = a;
      for (int i = 0; i < cnt; i++, b += g / cnt)
      {
         if (!i)
         {
            n = malloc_node(3);
            osm_node_default(n);
            int p = strncmp(circt, "parallel", 8);
            snprintf(buf, sizeof(buf), "%d", (int) a0);
            set_const_tag(&n->obj.otag[1], p ? "lon" : "lat", strdup(buf));
            snprintf(buf, sizeof(buf), "%d", (int) a);
            set_const_tag(&n->obj.otag[2], p ? "lat" : "lon", strdup(buf));
         }
         else
         {
            n = malloc_node(1);
            osm_node_default(n);
         }

         (void) cfunc(&n->lat, &n->lon, a0, b);

         put_object((osm_obj_t*) n);
         insert_refs(w, &n, 1, w->ref_cnt);
      }
   }

   n = get_object(OSM_NODE, w->ref[0]);
   insert_refs(w, &n, 1, w->ref_cnt);
   put_object((osm_obj_t*) w);

   return w;
}


/*! This function creates a parallel at lat0 degrees. Every g degrees a
 * longitudinal grid node is inserted and between each of these nodes
 * additional cnt many nodes are inserted.
 * @param lat0 Latitude in degrees.
 * @param g Distance of longitude grid in degrees.
 * @param cnt Number of points between each longitude node.
 * @param circt Value of tag "circle=<value>".
 * @return Returns a pointer to the way object. All newly created objects are
 * already inserted into the OSM database.
 */
osm_way_t *parallel0(double lat0, double g, int cnt, char *circt)
{
   return circle(lat0, g, cnt, circt, parallel_set_coords);
}


osm_way_t *parallel(double lat0, double g, int cnt)
{
   return parallel0(lat0, g, cnt, "parallel");
}


/*! This function creates a full 360 degree meridian at longitude lon0 with a
 * latitude grid of g and cnt points between each g. It creates a way with
 * nodes which will be added to the OSM data.
 * @param lon0 Longitude of the Meridian. Since it generates a 360 degrees
 * great circle, also lon0 + 180 will be covered.
 * @param g Distance of latitude grid.
 * @param cnt Number of points between each g point. This value should be at
 * least 1. If it smaller than that, 1 will be used.
 * @param circt Value of tag "circle=<value>".
 * @return Returns a pointer to the newly generated OSM way.
 */
osm_way_t *meridian0(double lon0, double g, int cnt, char *circt)
{
   return circle(lon0, g, cnt, circt, meridian_set_coords);
}


osm_way_t *meridian(double lon0, double g, int cnt)
{
   return meridian0(lon0, g, cnt, "meridian");
}


/*! This function generates ways and nodes of a global grid. The function
 * executes only once per rule, independently of how often it is called.
 */
int act_global_grid_main(smrule_t *r, osm_obj_t *UNUSED(o))
{
   struct grid *grd = r->data;

   log_debug("generating global longitude grid");
   for (double a = -180; a < 180; a += grd->lon_g)
      meridian(a, grd->lat_g, grd->gpcnt);
   log_debug("generating global latitude grid");
   for (double a = -90; a <= 90; a += grd->lat_g)
      parallel(a, grd->lon_g, grd->gpcnt);

   parallel0(66.563555, grd->lon_g, grd->gpcnt, "parallel:Arctic circle");
   parallel0(-66.563555, grd->lon_g, grd->gpcnt, "parallel:Antarctic circle");
   parallel0(23.436444, grd->lon_g, grd->gpcnt, "parallel:Tropic of Cancer");
   parallel0(-23.436444, grd->lon_g, grd->gpcnt, "parallel:Tropic of Capricorn");

   return 1;
}


int act_global_grid_fini(smrule_t *r)
{
   sm_free(r->data);
   r->data = NULL;
   return 0;
}

