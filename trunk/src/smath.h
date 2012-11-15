#ifndef SMATH_H
#define SMATH_H


#include <math.h>


/*struct coord
{
   double lat, lon;
};*/

struct pcoord
{
   double bearing, dist;
};


struct pcoord coord_diff(const struct coord *, const struct coord *);
struct coord dest_coord(const struct coord *, const struct pcoord *);


#endif

