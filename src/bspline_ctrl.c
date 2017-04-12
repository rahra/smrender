/* Copyright 2015 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file bspline_ctrl.c
 * This file contains the functions to calculate control points from a list of
 * points for drawing bezier curves.
 *
 *  @author Bernhard R. Fischer
 *  @version 2015/11/30
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include "bspline.h"

// This factor defines the "curviness". Play with it!
//#define CURVE_F 0.25
// This defines the method of using isosceles triangles. Undefine it to see the
// method of equal distances.
#define ISOSCELES_TRIANGLE


/*! This function calculates the angle of line in respect to the coordinate
 * system.
 * @param g Pointer to a line.
 * @return Returns the angle in radians.
 */
static double angle(const line_t *g)
{
   return atan2(g->B.y - g->A.y, g->B.x - g->A.x);
}


/*! This function calculates the control points. It takes two lines g and l as
 * arguments but it takes three lines into account for calculation. This is
 * line g (P0/P1), line h (P1/P2), and line l (P2/P3). The control points being
 * calculated are actually those for the middle line h, this is from P1 to P2.
 * Line g is the predecessor and line l the successor of line h.
 * @param g Pointer to first line.
 * @param l Pointer to third line (the second line is connecting g and l).
 * @param p1 Pointer to memory of first control point. This will receive the
 * coordinates.
 * @param p2 Pointer to memory of second control point.
 */ 
void control_points(const line_t *g, const line_t *l, point_t *p1, point_t *p2, double f)
{
   double lgt, a;
   line_t h;

   // calculate length of line (P1/P2)
   lgt = hypot(g->B.x - l->A.x, g->B.y - l->A.y);

#ifdef ISOSCELES_TRIANGLE
   // end point of 1st tangent
   h.B = l->A;
   // start point of tangent at same distance as end point along 'g'
   h.A.x = g->B.x - lgt * cos(angle(g));
   h.A.y = g->B.y - lgt * sin(angle(g));
#else
   h.A = g->A;
   h.B = l->A;
#endif

   // angle of tangent
   a = angle(&h);
   // 1st control point on tangent at distance 'lgt * f'
   p1->x = g->B.x + lgt * cos(a) * f;
   p1->y = g->B.y + lgt * sin(a) * f;

#ifdef ISOSCELES_TRIANGLE
   // start point of 2nd tangent
   h.A = g->B;
   // end point of tangent at same distance as start point along 'l'
   h.B.x = l->A.x + lgt * cos(angle(l));
   h.B.y = l->A.y + lgt * sin(angle(l));
#else
   h.A = g->B;
   h.B = l->B;
#endif

   // angle of tangent
   a = angle(&h);
   // 2nd control point on tangent at distance 'lgt * f'
   p2->x = l->A.x - lgt * cos(a) * f;
   p2->y = l->A.y - lgt * sin(a) * f;
}

