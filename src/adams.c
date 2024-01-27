/* Copyright 2020 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file adams.c
 * This file implements the Adams Square I+II projections.
 * As of today (2020/12/18) only the function adams_square_ii() (and
 * adams_square_ii_smr()) is checked to work properly.
 * See code remarks below for further implementation details about how the code
 * was derived.
 *
 * \author Bernhard R. Fischer
 * \date 2020/12/18
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "adams.h"
#include "smath.h"


void adams_square_ii_smr(double lambda, double phi, double *x, double *y)
{
   xy_t xy;

   xy = adams_square_ii(lambda, phi);
   if (x != NULL)
      *x = xy.x;
   if (y != NULL)
      *y = xy.y;
}


xy_t adams_square_i(double lambda, double phi)
{
   double a, b, c, sm, sn, sp;

   sp = tan(0.5 * phi);
   c = cos(asin(sp)) * sin(0.5 * lambda);
   a = acos((c - sp) * M_SQRT1_2);
   b = acos((c + sp) * M_SQRT1_2);
   sm = lambda < 0;
   sn = phi < 0;

   return elliptic_factory(a, b, sm, sn);
}


xy_t adams_square_i_invert(double x, double y)
{
   double phi, lam;

   phi = fmax(fmin(y / 1.8540746957596883, 1), -1) * M_PI_2;
   lam = fabs(phi) < M_PI ? fmax(fmin(x / 1.854074716833181, 1), -1) * M_PI : 0;

   return inverse(x, y, lam, phi, adams_square_i);
}


xy_t adams_square_ii(double lambda, double phi)
{
   double a, b, sm, sn, sp;
   xy_t xy;

   sp = tan(0.5 * phi);
   a = cos(asin(sp)) * sin(0.5 * lambda);
   sm = (sp + a) < 0;
   sn = (sp - a) < 0;
   b = acos(sp);
   a = acos(a);

   xy = elliptic_factory(a, b, sm, sn);

   return (xy_t) {M_SQRT1_2 * (xy.x - xy.y), M_SQRT1_2 * (xy.x + xy.y)};
}


xy_t adams_square_ii_invert(double x, double y)
{
   double phi, lam;

   phi = fmax(fmin(y / A2_PHI_SCALE, 1), -1) * M_PI_2;
   lam = fabs(phi) < M_PI ? fmax(fmin(x / A2_LAM_SCALE / cos(phi), 1), -1) * M_PI : 0;

   return inverse(x, y, lam, phi, adams_square_ii);
}


xy_t elliptic_factory(double a, double b, double sm, double sn)
{
   double m, n;

   m = asin(sqrt(1 + fmin(0, cos(a + b))));
   if (sm)
      m = -m;

   n = asin(sqrt(fabs(1 - fmax(0, cos(a - b)))));
   if (sn)
      n = -n;

   return (xy_t) {elliptic_f(m, 0.5), elliptic_f(n, 0.5)};
}


/*! This function calculates the elliptic integral. It is derived from Torben
 * Janson's code (here
 * https://observablehq.com/@toja/adams-world-in-a-square-i-ii) and checked
 * against his implementation literature (citation see below).
 *
 * This is the original remark ab T. Janson:
 * Computes the elliptic integral of the first kind.
 * Algorithm from Bulirsch(1965), the implementation follows Snyder(1989), p. 239.
 * A faster alternative for m = 0.5 is presented in:
 * Gerald I. Evenden (2008), libproj4: A Comprehensive Library of
 * Cartographic Projection Functions (Preliminary Draft), p. 123.
 */
double elliptic_f(double phi, double m)
{
   double g, h, k, n, p, r, y, sp;

   sp = sin(phi);
   h = sp * sp;
   k = sqrt(1 - m);

   // "complete" elliptic integral
   if (h >= 1 || fabs(phi) == M_PI_2)
   {
      if (k <= TOL)
         return sp < 0 ? -INFINITY : INFINITY;

      m = 1;
      h = m;
      m += k;

      while (fabs(h - k) > C1 * m)
      {
         k = sqrt(h * k);
         m /= 2;
         h = m;
         m += k;
      }

      return sp < 0 ? -M_PI / m : M_PI / m;
   }
   // "incomplete" elliptic integral
   else
   {
      if (k <= TOL)
         return log((1 + sp) / (1 - sp)) / 2;

      y = sqrt((1 - h) / h);
      n = 0;
      m = 1;
      p = m * k;
      g = m;
      m += k;
      y -= p / y;

      if (fabs(y) <= 0)
         y = C2 * sqrt(p);

      while (fabs(g - k) > C1 * g)
      {
         k = 2 * sqrt(p);
         n += n;
         if (y < 0)
            n += 1;
         p = m * k;
         g = m;
         m += k;
         y -= p / y;

         // FIXME: although this is exactly in the original algorithm by Snyder
         // (1989), it can never be <0, only ==0.
         if (fabs(y) <= 0)
            y = C2 * sqrt(p);
      }

      if (y < 0)
         n += 1;

      r = (atan(m / y) + M_PI * n) / m;
      return sp < 0 ? -r : r;
   }
}


static double limit(double a, double b)
{
   if (a < -b)
      return -b;

   if (a > b)
      return b;

   return a;
}


/*! This function is the inverse of elliptic_f(). The code was translated from
 * the Torben Jansons's code (see above at elliptic_f()).
 *
 * FIXME: It is not yet checked if it works.
 *
 * Original remark by T. Janson:
 * Newton-Raphson inversion, based on code from PROJ written by Gerald Evenden.
 * https://github.com/OSGeo/PROJ/blob/master/src/projections/adams.cpp
 */
xy_t inverse(double x, double y, double lam, double phi, xy_t (*proj)(double, double))
{
   double lam2, phi2, dlam0, dphi0, det;
   xy_t dlam, dphi, appr, d, xy2, dtlam, dtphi;
   int i;

   dlam.x = dlam.y = 0;
   dphi.x = dphi.y = 0;

   for (i = 0; i < 15; i++)
   {
      appr = proj(lam, phi);
      d.x = appr.x - x;
      d.y = appr.y - y;

      if (fabs(d.x) < 1e-10 && fabs(d.y) < 1e-10)
         return (xy_t) {lam, phi};

      if (fabs(d.x) > 1e-6 || fabs(d.y) > 1e-6)
      {
         dlam0 = lam > 0 ? -1e-6 : 1e-6;
         lam2 = lam + dlam0;
         phi2 = phi;
         xy2 = proj(lam2, phi2);
         dtlam.x = (xy2.x - appr.x) / dlam0;
         dtlam.y = (xy2.y - appr.y) / dlam0;

         dphi0 = phi > 0 ? -1e-6 : 1e-6;
         lam2 = lam;
         phi2 = phi + dphi0;
         xy2 = proj(lam2, phi2);
         dtphi.x = (xy2.x - appr.x) / dphi0;
         dtphi.y = (xy2.y - appr.y) / dphi0;

         det = dtlam.x * dtphi.y - dtphi.x * dtlam.y;
         if (det != 0)
         {
            dlam.x =  dtphi.y / det;
            dlam.y = -dtphi.x / det;
            dphi.x = -dtlam.y / det;
            dphi.y =  dtlam.x / det;
         }
      }

      if (x != 0)
      {
         dlam0 = fmax(fmin(d.x * dlam.x + d.y * dlam.y, 0.3), -0.3);
         lam -= dlam0;
         lam = limit(lam, M_PI);
      }

      if (y != 0)
      {
         dphi0 = fmax(fmin(d.x * dphi.x + d.y * dphi.y, 0.3), -0.3);
         phi -= dphi0;
         phi = limit(phi, M_PI_2);
      }
   }

   return (xy_t) {lam, phi};
}

