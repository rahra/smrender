#include <math.h>

#include "smrender.h"


/*! Calculate bearing and distance from src to dst.
 *  @param src Source coodinates (struct coord).
 *  @param dst Destination coordinates (struct coord).
 *  @return Returns a struct pcoord. Pcoord contains the distance in degrees (on a flat Mercartor
 *  projection) and the angle, 0 degress north, clockwise.
 */
struct pcoord coord_diff(const struct coord *src, const struct coord *dst)
{
   struct pcoord pc;
   double dlat, dlon;

   dlat = dst->lat - src->lat;
   dlon = (dst->lon - src->lon) * cos(DEG2RAD((src->lat + dst->lat) / 2.0));

   pc.bearing = RAD2DEG(atan2(dlon, dlat));
   pc.dist = sqrt(dlat * dlat + dlon * dlon);

   if (pc.bearing  < 0)
      pc.bearing += 360.0;

   return pc;
}


struct coord dest_coord(const struct coord *src, const struct pcoord *pc)
{
   struct coord cd;

   cd.lat = pc->dist * cos(DEG2RAD(pc->bearing)) + src->lat;
   cd.lon = pc->dist * sin(DEG2RAD(pc->bearing)) / cos(DEG2RAD((src->lat + cd.lat) / 2.0)) + src->lon;

   return cd;
}

