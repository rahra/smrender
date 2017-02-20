/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SEAMARK_H
#define SEAMARK_H

#include "bstring.h"
//#include "libhpxml.h"
#include "smrender.h"

#define ARC_DIV 6.0
#define ARC_MAX 0.1
#define SEC_RADIUS 0.2
#define MAX_SEC 32
#define MAX_SFRAC 36
#define TAPER_SEGS 7
#define DIR_ARC 2.0


enum {ARC_UNDEF, ARC_SOLID, ARC_SUPPRESS, ARC_DASHED, ARC_TAPER_UP, ARC_TAPER_DOWN, ARC_TAPER_1, ARC_TAPER_2, ARC_TAPER_3, ARC_TAPER_4, ARC_TAPER_5, ARC_TAPER_6, ARC_TAPER_7};

enum {CAT_STD, CAT_DIR};


/*! Struct sector_frac holds virtual subsectors which are constructed by
 * smfilter.
 */
struct sector_frac
{
   double r;            //!< radius
   double a;            //!< angle (= end - start)
   double start, end;   //!< absolute start and end angle
   int type;            //!< type (solid, dashed, taper0-7, invisible)
   int col;             //!< color
   int startr, endr;    //!< radial line at start/end
};

struct lchar
{
   bstring_t lc;
   int group;
   int period;
   int range;
};

/*! Struct sector contains all light sectors as they are imported from the
 * original OSM file. Those sectors are then further split into subsectors. The
 * subsectors are hold within struct sector_frac structures.
 */
struct sector
{
   int used;
   int col[2];
   int nr;
   //double arcp;
   double dir;
   double start, end;
   double sspace, espace;
   double mean;
   double r;
   int al;              //!< alternating arcs (sector with two colors)
   int cat;             //!< category of light (standard or directional)
   int fused;           //!< number sector_frac used
   struct sector_frac sf[MAX_SFRAC];
   struct lchar lc;
};


/*
char *smstrdup(const char *);
int get_sectors(struct rdata*, const struct onode *, struct sector *sec, int nmax);
void node_calc(const struct osm_node *nd, double r, double a, double *lat, double *lon);
int sector_calc3(struct rdata *, const struct onode *, const struct sector *, bstring_t);
void init_sector(struct sector *sec);
int proc_sfrac(struct sector *sec);
static const char *color(int);
static const char *color_abbr(int);
static void sort_sectors(struct sector *, int);
static int parse_color(bstring_t);
*/

#endif

