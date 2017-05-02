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

/*! \file bspline.h
 * This file contains the prototypes and typedefs for the bspline_ctrl.c for
 * the calculation of control points for drawing bezier curves.
 *
 *  @author Bernhard R. Fischer
 *  @version 2015/11/30
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef BSPLINE_H
#define BSPLINE_H


typedef struct point
{
   double x, y;
} point_t;

typedef struct line
{
   point_t A, B;
} line_t;


void control_points(const line_t *, const line_t *, point_t *, point_t *, double f);

#endif

