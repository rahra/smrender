#ifndef ADAMS_H
#define ADAMS_H

#define C1 1e-3
#define C2 1e-9
#define TOL 1e-5

typedef struct xy
{
   double x, y;
} xy_t;


double elliptic_f(double phi, double m);
xy_t elliptic_factory(double a, double b, double sm, double sn);
xy_t inverse(double x, double y, double lam, double phi, xy_t (*proj)(double, double));
xy_t adams_square_i(double lamda, double phi);
xy_t adams_square_i_invert(double x, double y);
xy_t adams_square_ii(double lamda, double phi);
xy_t adams_square_ii_invert(double x, double y);
xy_t spilhaus_square(double lambda, double phi);
xy_t spilhaus_square_invert(double x, double y);

#endif

