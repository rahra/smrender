/* Copyright 2011-2023 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smath.h
 * This file contains definitions for some fundamental mathematics relevant
 * data.
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 */

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


void coord_diffp(const struct coord *, const struct coord *, struct pcoord *);
struct pcoord coord_diff(const struct coord *, const struct coord *);
struct coord dest_coord(const struct coord *, const struct pcoord *);
double fmod2(double , double );
int sgn(double );


#endif

