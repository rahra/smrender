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

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <syslog.h>
#include <math.h>
#include <string.h>

//#include "bxtree.h"
#include "rdata.h"
//#include "smrender.h"
#include "smrender_dev.h"


static struct rdata rd_;


struct rdata *rdata_get(void)
{
   return &rd_;
}


double mm2pxf(double x)
{
   return x * rd_.dpi / 25.4;
}


int mm2pxi(double x)
{
   return round(mm2pxf(x));
}


double px2mm(double x)
{
   return x * 25.4 / rd_.dpi;
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
   log_msg(LOG_NOTICE, "   lath = %f, lath_len = %f", rd_.lath, rd_.lath_len);
   log_msg(LOG_NOTICE, "   page size = %.1f x %.1f mm, oversampling = %d",
         PX2MM(rd_.w), PX2MM(rd_.h), rd_.ovs);
   log_msg(LOG_NOTICE, "   rendering: %dx%d px, dpi = %d",
         rd_.w, rd_.h, rd_.dpi);
   log_msg(LOG_NOTICE, "   final: %dx%d px, dpi = %d",
         rd_.fw, rd_.fh, rd_.ovs ? rd_.dpi / rd_.ovs : rd_.dpi);
   log_msg(LOG_NOTICE, "   1 px = %.3f mm, 1mm = %d px", PX2MM(1), (int) MM2PX(1));
   log_msg(LOG_NOTICE, "   scale 1:%.0f, %.1f x %.1f nm",
         rd_.scale, rd_.wc * 60 * cos(DEG2RAD(rd_.mean_lat)), rd_.hc * 60);

   log_debug("   G_GRID %.3f, G_TICKS %.3f, G_STICKS %.3f, G_MARGIN %.2f, G_TW %.2f, G_STW %.2f, G_BW %.2f",
         G_GRID, G_TICKS, G_STICKS, G_MARGIN, G_TW, G_STW, G_BW);
   log_msg(LOG_NOTICE, "***");
}


int rdata_width(void)
{
   return rd_.w;
}


int rdata_height(void)
{
   return rd_.h;
}


static void __attribute__((constructor)) init_rdata(void)
{
   //struct rdata *rd = get_rdata();
   log_debug("initializing struct rdata");
   memset(&rd_, 0, sizeof(rd_));
   rd_.dpi = 300;
   rd_.ovs = DEFAULT_OVS;
   rd_.title = "";
   //set_static_obj_tree(&rd_.obj);
}

