
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "adams.h"


xy_t spilhaus_square(double lambda, double phi)
{
   double a, b, sm, sn, sp;

   sp = tan(0.5 * phi);
   a = cos(asin(sp)) * sin(0.5 * lambda);
   sm = (sp + a) < 0;
   sn = (sp - a) < 0;
   b = acos(sp);
   a = acos(a);

   return elliptic_factory(a, b, sm, sn);
}

/*
return () => d3.geoProjection(spilhausSquareRaw)
   .rotate([-66.94970198, 49.56371678, 40.17823482])
   .scale(134.838125);

latlon =
  Array(2) [
  0: -65.08076472399027
  1: -29.706594889899357
]


*/

xy_t spilhaus_square_invert(double x, double y)
{
   double phi, lam;

   phi = fmax(fmin(y / 1.8540746957596883, 1), -1) * M_PI_2;
   lam = fabs(phi) < M_PI ? fmax(fmin(x / 1.854074716833181, 1), -1) * M_PI : 0;

   return inverse(x, y, lam, phi, spilhaus_square);
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

   phi = fmax(fmin(y / 2.62181347, 1), -1) * M_PI_2;
   lam = fabs(phi) < M_PI ? fmax(fmin(x / 2.62205760 / cos(phi), 1), -1) * M_PI : 0;

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


/*!
 * https://observablehq.com/@toja/adams-world-in-a-square-i-ii
 *
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
         // (1989), it does not make sense to me.
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

      if (fabs(d.x > 1e-6 || fabs(d.y) > 1e-6))
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












