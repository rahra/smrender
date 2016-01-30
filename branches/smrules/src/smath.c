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

/*! This file contains contains code for some calculations on positions given in lat/lon.
 *
 *  @author Bernhard R. Fischer
 */

#include <math.h>

#include "smrender_dev.h"


/*! Calculate bearing and distance from src to dst.
 *  @param src Source coodinates (struct coord).
 *  @param dst Destination coordinates (struct coord).
 *  @return Returns a struct pcoord. Pcoord contains the orthodrome distance in
 *  degrees and the bearing in degrees, 0 degress north, clockwise.
 */
void coord_diffp(const struct coord *src, const struct coord *dst, struct pcoord *pc)
{
   double dlat, dlon;

   dlat = dst->lat - src->lat;
   dlon = (dst->lon - src->lon) * cos(DEG2RAD((src->lat + dst->lat) / 2.0));

   pc->bearing = RAD2DEG(atan2(dlon, dlat));
   pc->dist = RAD2DEG(acos(
      sin(DEG2RAD(src->lat)) * sin(DEG2RAD(dst->lat)) + 
      cos(DEG2RAD(src->lat)) * cos(DEG2RAD(dst->lat)) * cos(DEG2RAD(dst->lon - src->lon))));

   pc->bearing = fmod2(pc->bearing, 360.0);
   /*
   if (pc->bearing  < 0)
      pc->bearing += 360.0;

   return pc;*/
}


struct pcoord coord_diff(const struct coord *src, const struct coord *dst)
{
   struct pcoord pc;

   coord_diffp(src, dst, &pc);
   return pc;
}


struct coord dest_coord(const struct coord *src, const struct pcoord *pc)
{
   struct coord cd;

   cd.lat = pc->dist * cos(DEG2RAD(pc->bearing)) + src->lat;
   cd.lon = pc->dist * sin(DEG2RAD(pc->bearing)) / cos(DEG2RAD((src->lat + cd.lat) / 2.0)) + src->lon;

   return cd;
}


/*! This function works exactly like fmod(3) except that it always returns a
 * positive value.
 */
double fmod2(double a, double n)
{
   a = fmod(a, n);
   return a < 0 ? a + n : a;
}


/*! This implements the signum function. 
 * @param a Parameter to test for.
 * @return The function returns 1 if a > 0, -1 if a < 0 and 0 if a == 0.
 */
int sgn(double a)
{
   return (a > 0) - (a < 0);
}

