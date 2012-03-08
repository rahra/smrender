#ifndef SMATH_H
#define SMATH_H


#include <math.h>


#define DEG2RAD(x) (x * M_PI / 180.0)
#define RAD2DEG(x) (x * 180.0 / M_PI)


struct coord
{
   double lat, lon;
};

struct pcoord
{
   double bearing, dist;
};


struct pcoord coord_diff(const struct coord *, const struct coord *);
struct coord dest_coord(const struct coord *, const struct pcoord *);


#endif

