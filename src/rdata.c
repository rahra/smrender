/* Copyright 2011-2021 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file rdata.c
 * This file contains functions for rendering initialization, such as unit
 * conversions and paper and coordinate initialization.
 *
 * @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <syslog.h>
#include <math.h>
#include <string.h>

#include "rdata.h"
#include "smrender.h"
#include "smrender_dev.h"
#include "adams.h"


static void test_rdata_unit(void);

static struct rdata rd_;


struct rdata *rdata_get(void)
{
   return &rd_;
}


double mm2ptf(double x)
{
   return x * 72 / 25.4;
}


double mm2pxf(double x)
{
   return x * rd_.dpi / 25.4;
}


int mm2pxi(double x)
{
   return round(mm2pxf(x));
}


void pxf2geo(double x, double y, double *lon, double *lat)
{
   *lon = x * rd_.wc / rd_.w + rd_.bb.ll.lon;
   *lat = RAD2DEG(atan(sinh(rd_.lath_len * (0.5 - y / rd_.h) + rd_.lath)));
}


/*! This function projects the polygon defined by the 4 points rd_.pw[] (pw[0]
 * -> left lower, pw[1] -> right lower, pw[2] -> right upper, pw[3] -> left
 *  upper) to the rectangular page. This does not fulfill Mercartor
 *  constraints. The function is experimental.
 */
void geo2pxf_rect(double lon, double lat, double *x, double *y)
{
   double x0, y0, sx, sy, dx, dy, mx, my;

   x0 = lon - rd_.pw[0].lon;
   y0 = lat - rd_.pw[0].lat;

   sx = x0 / (rd_.pw[1].lon - rd_.pw[0].lon);
   sy = y0 / (rd_.pw[3].lat - rd_.pw[0].lat);

   dx = (rd_.pw[3].lon - rd_.pw[0].lon);
   dy = (rd_.pw[1].lat - rd_.pw[0].lat);
   mx = (rd_.pw[2].lon - rd_.pw[3].lon) / (rd_.pw[1].lon - rd_.pw[0].lon);
   my = (rd_.pw[2].lat - rd_.pw[1].lat) / (rd_.pw[3].lat - rd_.pw[0].lat);

   x0 -= dx * sy;
   x0 /= 1 - (1 - mx) * sy;

   y0 -= dy * sx;
   y0 /= 1 - (1 - my) * sx;

   *x = x0 * rd_.w / (rd_.pw[1].lon - rd_.pw[0].lon);
   *y = rd_.h - y0 * rd_.h / (rd_.pw[3].lat - rd_.pw[0].lat);
}


/*! This function properly wraps longitude values. This is if a longitude value
 * increases above 180 degrees it "jumps" from East to West, i.e. it becomes
 * negative and increases from -180 again. This equally is done of a longitude
 * decreases below -180, i.e. it jumps from West to East.
 *
 * @param lon Longitude value.
 * @return The function returns a proper longitude value which is -180 <= ret
 * <= 180.
 */
double lonmod(double lon)
{
   lon = fmod(lon, 360);
   if (lon < -180)
      lon += 360;
   if (lon > 180)
      lon -= 360;
   return lon;
}


/*! This function translates coordinates to a different point of reference,
 * i.e. it rotates to surface to a different reference.
 * @param theta Translation of latitude in degrees.
 * @param phi Translation of longitude in degrees.
 * @param lat0 Pointer to the latitude to tranlate.
 * @param loni0 Pointer to the longitude to translate.
 */
void transcoord(double theta, double phi, double *lat0, double *lon0)
{
   double lat, lon;

   *lat0 = DEG2RAD(*lat0);
   *lon0 = DEG2RAD(*lon0);
   theta = DEG2RAD(theta);
   phi = DEG2RAD(phi);

   lat = asin(cos(theta) * sin(*lat0) - cos(*lon0) * sin(theta) * cos(*lat0));
   lon = atan2(sin(*lon0), tan(*lat0) * sin(theta) + cos(*lon0) * cos(theta)) - phi;

   *lat0 = RAD2DEG(lat);
   *lon0 = lonmod(RAD2DEG(lon));
}


void transtraversal(double lat, double lon, double *lat0, double *lon0)
{
   transcoord(0, lon, lat0, lon0);
   transcoord(lat, 0, lat0, lon0);
   transcoord(0, -lon, lat0, lon0);
}


/*! Convert geographic to Cartesian (pixel) coordinates x and y.
 * @param lon Longitude of object.
 * @param lat Latitude of object.
 * @param x Pointer to x coordinate for result.
 * @param y Pointer to y coordinate.
 */
void geo2pxf(double lon, double lat, double *x, double *y)
{
   if (rd_.proj == PROJ_ADAMS2)
   {
//#define SPILDEBUG
#ifdef SPILDEBUG
      static double lmin = HUGE_VAL, lmax = -HUGE_VAL, pmin = HUGE_VAL, pmax = -HUGE_VAL, xmin = HUGE_VAL, xmax = -HUGE_VAL, ymin = HUGE_VAL, ymax = -HUGE_VAL;
      static long cnt = 0;
#endif

      adams_square_ii_smr(DEG2RAD(lon), DEG2RAD(lat), x, y);

      *x = ((*x + A2_LAM_SCALE) * rd_.w) / (2 * A2_LAM_SCALE);
      *y = rd_.h - ((*y + A2_PHI_SCALE) * rd_.h) / (2 * A2_PHI_SCALE);

#ifdef SPILDEBUG
      lmin = fmin(lmin, lon);
      lmax = fmax(lmax, lon);
      pmin = fmin(pmin, lat);
      pmax = fmax(pmax, lat);
      xmin = fmin(xmin, *x);
      xmax = fmax(xmax, *x);
      ymin = fmin(ymin, *y);
      ymax = fmax(ymax, *y);
      cnt++;
      if (cnt % 1000)
         log_debug("lmin = %.2f, lmax = %.2f, pmin = %.1f, pmax = %.1f, xmin = %.1f, xmax = %.1f, ymin = %.1f, ymax = %.1f",
               lmin, lmax, pmin, pmax, xmin, xmax, ymin, ymax);
#endif

     return;
   }

   if (!rd_.polygon_window)
   {
      *x = (lon - rd_.bb.ll.lon) * rd_.w / rd_.wc;
      *y = rd_.h * (0.5 - (asinh(tan(DEG2RAD(lat))) - rd_.lath) / rd_.lath_len);
   }
   else
   {
      geo2pxf_rect(lon, lat, x, y);
   }
}


/*! Convert geographic to page coordinates, i.e. Cartesian coordinates
 * dependent on pixel density (dpi).
 * @param lon Longitude of object.
 * @param lat Latitude of object.
 * @param x Pointer to x coordinate for result.
 * @param y Pointer to y coordinate.
 */
void geo2pt(double lon, double lat, double *x, double *y)
{
   geo2pxf(lon, lat, x, y);
   *x = rdata_px_unit(*x, U_PT);
   *y = rdata_px_unit(*y, U_PT);
}


void geo2pxi(double lon, double lat, int *x, int *y)
{
   double xf, yf;

   geo2pxf(lat, lon, &xf, &yf);
   *x = round(xf);
   *y = round(yf);
}


void rdata_log(void)
{
   //struct rdata *rd = get_rdata();

   log_msg(LOG_NOTICE, "*** chart parameters for rendering ****");
   log_msg(LOG_NOTICE, "   %.3f %.3f -- %.3f %.3f",
         rd_.bb.ru.lat, rd_.bb.ll.lon, rd_.bb.ru.lat, rd_.bb.ru.lon);
   log_msg(LOG_NOTICE, "   %.3f %.3f -- %.3f %.3f",
         rd_.bb.ll.lat, rd_.bb.ll.lon, rd_.bb.ll.lat, rd_.bb.ru.lon);
   log_msg(LOG_NOTICE, "   wc = %.3f°, hc = %.3f°", rd_.wc, rd_.hc);
   log_msg(LOG_NOTICE, "   mean_lat = %.3f°, mean_lat_len = %.3f (%.1f nm)",
         rd_.mean_lat, rd_.mean_lat_len, rd_.mean_lat_len * 60);
   log_msg(LOG_NOTICE, "   transversal_lat = %.3f°", rd_.transversal_lat);
   log_msg(LOG_NOTICE, "   proj = %d", rd_.proj);
   log_msg(LOG_NOTICE, "   lath = %f, lath_len = %f", rd_.lath, rd_.lath_len);
   log_msg(LOG_NOTICE, "   polygon_window = %d", rd_.polygon_window);
   for (int i = 0; i < 4; i++)
      log_msg(LOG_NOTICE, "   pw[%d] = {%.3f %.3f}", i, rd_.pw[i].lat, rd_.pw[i].lon);
   log_msg(LOG_NOTICE, "   rotation = %.1f", RAD2DEG(rd_.rot));
   log_msg(LOG_NOTICE, "   page size = %.1f x %.1f mm",
         PX2MM(rd_.pgw), PX2MM(rd_.pgh));
   log_msg(LOG_NOTICE, "   rendering: %.1f x %.1f mm (%.1fx%.1f px), dpi = %d",
         PX2MM(rd_.w), PX2MM(rd_.h), rd_.w, rd_.h, rd_.dpi);
   log_msg(LOG_NOTICE, "   1 px = %.3f mm, 1 mm = %d px", PX2MM(1), (int) MM2PX(1));
   log_msg(LOG_NOTICE, "   1 px = %.3f nm, 1 nm = %.1f px", rdata_px_unit(1, U_NM), 1 / rdata_px_unit(1, U_NM));
   log_msg(LOG_NOTICE, "   scale 1:%.0f, %.1f x %.1f nm",
         rd_.scale, rd_.wc * 60 * cos(DEG2RAD(rd_.mean_lat)), rd_.hc * 60);
   log_msg(LOG_NOTICE, "   flags = 0x%04x, MAX_ITER = %d", rd_.flags, MAX_ITER);
   log_debug("   G_GRID %.3f, G_TICKS %.3f, G_STICKS %.3f, G_MARGIN %.2f, G_TW %.2f, G_STW %.2f, G_BW %.2f",
         G_GRID, G_TICKS, G_STICKS, G_MARGIN, G_TW, G_STW, G_BW);
   log_debug("   square_nm = %f, square_mm = %f", rdata_square_nm(), rdata_square_mm());
   log_msg(LOG_NOTICE, "***");

   test_rdata_unit();
}


/* Convert pixel to desired unit. */
double rdata_px_unit(double x, unit_t type)
{
   switch (type)
   {
      case U_1:
      case U_PX:
         return x;
      case U_CM:
         return x * 25.4 / rd_.dpi / 10;
      case U_MM:
         return x * 25.4 / rd_.dpi;
      case U_PT:
         return x * 72 / rd_.dpi;
      case U_IN:
         return x / rd_.dpi;
      case U_NM:
      case U_MIN:
         return x * rd_.mean_lat_len * 60 / rd_.w;
      case U_KM:
         return x * rd_.mean_lat_len * 60 / rd_.w * 1.852;
      case U_M:
         return x * rd_.mean_lat_len * 60 / rd_.w * 1852;
      case U_KBL:
         return x * rd_.mean_lat_len * 60 / rd_.w * 10;
      case U_FT:
         return x * rd_.mean_lat_len * 60 / rd_.w * 6076.12;
      case U_DEG:
         return x * rd_.mean_lat_len      / rd_.w;
      default:
         return NAN;
   }
}


double rdata_unit_px(double x, unit_t type)
{
   switch (type)
   {
      case U_1:
      case U_PX:
         return x;
      case U_CM:
         return x / 25.4 * rd_.dpi * 10;
      case U_MM:
         return x / 25.4 * rd_.dpi;
      case U_PT:
         return x / 72 * rd_.dpi;
      case U_IN:
         return x * rd_.dpi;
      case U_NM:
      case U_MIN:
         return x / rd_.mean_lat_len / 60 * rd_.w;
      case U_KM:
         return x / rd_.mean_lat_len / 60 * rd_.w / 1.852;
      case U_M:
         return x / rd_.mean_lat_len / 60 * rd_.w / 1852;
      case U_KBL:
         return x / rd_.mean_lat_len / 60 * rd_.w / 10;
      case U_FT:
         return x / rd_.mean_lat_len / 60 * rd_.w / 6076.12;
      case U_DEG:
         return x / rd_.mean_lat_len      * rd_.w;
      default:
         return NAN;
   }
}


double rdata_unit(const value_t *v, unit_t u)
{
   return rdata_px_unit(rdata_unit_px(v->val, v->u), u);
}


const char *unit_str(unit_t type)
{
   switch (type)
   {
      case U_1:
         return "1";
      case U_PX:
         return "px";
      case U_CM:
         return "cm";
      case U_MM:
         return "mm";
      case U_PT:
         return "pt";
      case U_IN:
         return "in";
      case U_NM:
         return "nm";
      case U_MIN:
         return "'";
      case U_KM:
         return "km";
      case U_M:
         return "m";
      case U_KBL:
         return "kbl";
      case U_FT:
         return "ft";
      case U_DEG:
         return "°";
      default:
         return "?";
   }
}


static void test_rdata_unit(void)
{
#define TEST_RDU_VAL 1.0
   double v;
   int i;

   for (i = 0, v = 0; !isnan(v); i++)
   {
      v = rdata_px_unit(TEST_RDU_VAL, i);
      log_msg(LOG_DEBUG, "%.1f px = %.3f %s", TEST_RDU_VAL, v, unit_str(i));
      v = rdata_unit_px(TEST_RDU_VAL, i);
      log_msg(LOG_DEBUG, "%.1f %s = %.3f px", TEST_RDU_VAL, unit_str(i), v);
   }
}


double rdata_page_width(unit_t type)
{
   return rdata_px_unit(rd_.pgw, type);
}


double rdata_page_height(unit_t type)
{
   return rdata_px_unit(rd_.pgh, type);
}


double rdata_width(unit_t type)
{
   return rdata_px_unit(rd_.w, type);
}


double rdata_height(unit_t type)
{
   return rdata_px_unit(rd_.h, type);
}


int rdata_dpi(void)
{
   return rd_.dpi;
}


double rdata_square_mm(void)
{
   return px2mm(rd_.w) * px2mm(rd_.h);
}


double rdata_square_nm(void)
{
   return rd_.mean_lat_len * rd_.hc * 3600;
}


double rdata_scale(void)
{
   return rd_.scale;
}


static void __attribute__((constructor)) init_rdata(void)
{
   //struct rdata *rd = get_rdata();
   log_debug("initializing struct rdata");
   memset(&rd_, 0, sizeof(rd_));
   rd_.dpi = 300;
   rd_.title = "";
   rd_.img_scale = 1;
   //set_static_obj_tree(&rd_.obj);
}


int is_on_page(const struct coord *c)
{
   if (c->lon < rd_.bb.ll.lon || c->lon > rd_.bb.ru.lon || c->lat < rd_.bb.ll.lat || c->lat > rd_.bb.ru.lat)
      return 0;
   return 1;
}

