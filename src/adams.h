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

/*! \file adams.h
 * This is the header file to adams.c.
 * See there for futher details.
 *
 * \author Bernhard R. Fischer
 * \date 2020/12/18
 */

#ifndef ADAMS_H
#define ADAMS_H

#define C1 1e-3
#define C2 1e-9
#define TOL 1e-5

#define A2_PHI_SCALE 2.62181347
#define A2_LAM_SCALE 2.62205760


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
void adams_square_ii_smr(double lambda, double phi, double *x, double *y);

#endif

