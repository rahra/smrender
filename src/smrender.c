/* Copyright 2011-2018 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smrender.c
 * This file contains the main() function and main initialization functions.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/time.h>      // gettimeofday()
#include <fcntl.h>         // stat()
#include <syslog.h>
#include <signal.h>
#include <locale.h>

#include "smrender_dev.h"
#include "smcore.h"
#include "smloadosm.h"
#include "rdata.h"
#include "lists.h"


#define COORD_LAT 0
#define COORD_LON 1

#define ISNORTH(x) (strchr("Nn", (x)) != NULL)
#define ISSOUTH(x) (strchr("Ss", (x)) != NULL)
#define ISEAST(x) (strchr("EeOo", (x)) != NULL)
#define ISWEST(x) (strchr("Ww", (x)) != NULL)
#define ISLAT(x) (ISNORTH(x) || ISSOUTH(x))
#define ISLON(x) (ISEAST(x) || ISWEST(x))


struct tile_info
{
   char *path;    // path to tiles
   int zlo, zhi;  // lowest and highest zoom level
   int ftype;     // 0 ... png, 1 ... jpg, 2 ... pdf
};

volatile sig_atomic_t int_ = 0;
static volatile sig_atomic_t pipe_ = 0;
int render_all_nodes_ = 0;


/*! This function parse a coordinate string of format "[-]dd.ddd[NESW]" or
 * "[-]dd[NESW](dd.ddd)?" into a correctly signed double value. The function
 * returns either COORD_LAT (0) if the string contains a latitude coordinate,
 * or COORD_LON (1) if the string contains a longitude coordinate, or -1
 * otherwise.
 * @param s Pointer to string.
 * @param a Pointer to double variable which will receive the converted value.
 * @return 0 for latitude, 1 for longitude, or -1 otherwise. In any case a will
 * be set to 0.0.
 */
int parse_coord(const char *s, double *a)
{
   double e, f, n = 1.0;
   int r;

   for (; isspace((int) *s); s++);
   if (*s == '-')
   {
      s++;
      n = -1.0;
   }
   for (*a = 0.0; isdigit((int) *s); s++)
   {
      *a *= 10.0;
      *a += *s - '0';
   }

   for (; isspace((int) *s); s++);
   if (*s == '\0')
   {
      *a *= n;
      return -1;
   }

   if (ISLAT(*s))
   {
      r = COORD_LAT;
      if (ISSOUTH(*s)) n *= -1.0;
   }
   else if (ISLON(*s))
   {
      r = COORD_LON;
      if (ISWEST(*s)) n *= -1.0;
   }
   else if (*s == '.')
   {
      s++;
      for (e = 1.0, f = 0.0; isdigit((int) *s); e *= 10.0, s++)
      {
         f *= 10.0;
         f += *s - '0';
      }
      *a += f / e;
      *a *= n;

      for (; isspace((int) *s); s++);
      if (*s == '\0') return -1;

      if (ISLAT(*s))
      {
         if (ISSOUTH(*s)) *a *= -1.0;
         return COORD_LAT;
      }
      else if (ISLON(*s))
      {
         if (ISWEST(*s)) *a *= -1.0;
         return COORD_LON;
      }
      else
         return -1;
   }
   else
   {
      *a *= n;
      return -1;
   }

   s++;
   for (; isspace((int) *s); s++);
   f = atof(s);
   *a += f / 60.0;
   *a *= n;

   return r;
}


void int_handler(int sig)
{
   switch (sig)
   {
      case SIGINT:
         int_++;
         break;
      case SIGPIPE:
         pipe_++;
         break;
   }
}


void install_sigint(void)
{
   struct sigaction sa;

   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = int_handler;

   if (sigaction(SIGINT, &sa, NULL) == -1)
      log_msg(LOG_WARNING, "SIGINT handler cannot be installed: %s", strerror(errno));
   else
      log_msg(LOG_INFO, "SIGINT installed (pid = %ld)", (long) getpid());

   if (sigaction(SIGPIPE, &sa, NULL) == -1)
      log_msg(LOG_WARNING, "SIGPIPE handler cannot be installed: %s", strerror(errno));
   else
      log_msg(LOG_INFO, "SIGPIPE installed (pid = %ld)", (long) getpid());
}


int norm_rule_node(osm_obj_t *o, void * UNUSED(p))
{
#define RULE_LON_DIFF 1.0/600.0
#define RULE_LAT_DIFF RULE_LON_DIFF
   static double lon;

   if ((((osm_node_t*) o)->lon == 0.0) && (((osm_node_t*) o)->lon == 0.0))
   {
      //log_debug("norm %f", lon);
      lon += RULE_LON_DIFF;
      ((osm_node_t*) o)->lon = lon;
   }
   return 0;
}


int norm_rule_way(osm_obj_t *o, void *p)
{
   struct rdata *rd = get_rdata();
   static double lat;
   osm_node_t *n;

   if (((osm_way_t*) o)->ref_cnt > 0)
      return 0;

   lat += RULE_LAT_DIFF;

   n = malloc_node(0);
   n->obj.id = --((struct dstats*) p)->min_id[OSM_NODE];
   n->obj.ver = 1;
   n->lat = lat;
   n->lon = 0;
   put_object0(&rd->rules, n->obj.id, n, IDX_NODE);
   n = malloc_node(0);
   n->obj.id = --((struct dstats*) p)->min_id[OSM_NODE];
   n->obj.ver = 1;
   n->lat = lat;
   n->lon = RULE_LON_DIFF;
   put_object0(&rd->rules, n->obj.id, n, IDX_NODE);

   if ((((osm_way_t*) o)->ref = malloc(sizeof(int64_t) * 2)) == NULL)
      log_msg(LOG_ERR, "malloc failed: %s", strerror(errno)), exit(EXIT_FAILURE);

   ((osm_way_t*) o)->ref[0] = n->obj.id + 1;
   ((osm_way_t*) o)->ref[1] = n->obj.id;
   ((osm_way_t*) o)->ref_cnt = 2;

   return 0;
}


int strip_ways(osm_way_t *w, void * UNUSED(p))
{
   struct onode *n;
   int i;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         memmove(&w->ref[i], &w->ref[i + 1], (w->ref_cnt - i - 1) * sizeof(int64_t));
         w->ref_cnt--;
         i--;
      }
   }

   if (w->ref_cnt == 0)
   {
      log_debug("way %ld has no nodes", w->obj.id);
   }

   return 0;
}


void print_url(struct bbox bb)
{
   const char *url[] = {
      "http://www.overpass-api.de/api/xapi_meta?*[bbox=%.3f,%.3f,%.3f,%.3f]\n",
      "http://api.openstreetmap.org/api/0.6/map?bbox=%.3f,%.3f,%.3f,%.3f\n",
      "http://overpass.osm.rambler.ru/cgi/xapi_meta?*[bbox=%.3f,%.3f,%.3f,%.3f]\n",
      "http://api.openstreetmap.fr/xapi?*[bbox=%.3f,%.3f,%.3f,%.3f]\n",
      NULL};
   double d;
   int i;

   d = (bb.ru.lon - bb.ll.lon) * BB_SCALE;
   bb.ll.lon -= d;
   bb.ru.lon += d;
   d = (bb.ru.lat - bb.ll.lat) * BB_SCALE;
   bb.ll.lat -= d;
   bb.ru.lat += d;

   for (i = 0; url[i] != NULL; i++)
      printf(url[i], bb.ll.lon, bb.ll.lat, bb.ru.lon, bb.ru.lat);
}


/*! This function initializes the projection parameters. This is the final
 * geographic bounding box, the hyperpolic North-South stretching, and the
 * chart scale.
 * @param rd Pointer to the rdata structure. The following members of the
 * structure have to be set correctly before calling init_bbox_mll():
 * mean_lat, mean_lat_len, mean_lon, w, h, dpi
 */
void init_bbox_mll(struct rdata *rd)
{
   double lat, lon;

   // calculate scale which depends on the mean latitude
   rd->scale = (rd->mean_lat_len * 60.0 * 1852 * 100 / 2.54) / ((double) rd->w / (double) rd->dpi);
   // calculate meridians on left and right border of the chart
   rd->wc = rd->mean_lat_len / cos(rd->mean_lat * M_PI / 180);
   rd->bb.ll.lon = rd->mean_lon - rd->wc / 2;
   rd->bb.ru.lon = rd->mean_lon + rd->wc / 2;

   // estimate latitudes on upper on lower border of the chart
   rd->hc = rd->mean_lat_len * rd->h / rd->w;
   rd->bb.ru.lat = rd->mean_lat + rd->hc / 2.0;
   rd->bb.ll.lat = rd->mean_lat - rd->hc / 2.0;

   // iteratively approximate latitudes
   for (int i = 0; i < 3; i++)
   {
      // calculate hyperbolic distoration factors
      rd->lath = asinh(tan(DEG2RAD(rd->mean_lat)));
      rd->lath_len = asinh(tan(DEG2RAD(rd->bb.ru.lat))) - asinh(tan(DEG2RAD(rd->bb.ll.lat)));

      // recalculate northern and southern latitude
      pxf2geo(0.0, 0.0, &lon, &lat);
      rd->bb.ru.lat = lat;
      pxf2geo(0.0, rd->h, &lon, &lat);
      rd->bb.ll.lat = lat;
      rd->hc = rd->bb.ru.lat - rd->bb.ll.lat;
   }
}


int free_rules(smrule_t *r, void * UNUSED(p))
{
   free_obj(r->oo);
   free_fparam(r->act->fp);
   // action must not be freed because it is part of the rule (see alloc_rule())
   free(r);
   return 0;
}


int free_objects(osm_obj_t *o, void * UNUSED(p))
{
   free_obj(o);
   return 0;
}


/*! Calculate the large page bounding if a page should be rotated.
 * @param rd Pointer to rdata structure.
 * @param angle Pointer to string containing decimal rotation.
 */
static void page_rotate(struct rdata *rd, const char *angle)
{
   double a, r;
   char *endptr;

  if (angle == NULL)
      return;

   errno = 0;
   rd->rot = strtod(angle, &endptr);
   if (errno || endptr == angle)
   {
      rd->rot = 0;
      return;
   }

   rd->rot = fmod(DEG2RAD(rd->rot), 2 * M_PI);
   if (rd->rot == 0)
      return;

   a = atan(rd->h / rd->w);
   r = hypot(rd->h, rd->w);
   rd->h = r * sin(a + fabs(rd->rot));
   rd->w = r * cos(a - fabs(rd->rot));
}


/*! This function initializes the pixel width (w) and height (h) of the rdata
 * structure. rd->dpi must be pre-initialized!
 * @param rd Pointer to the rdata structure.
 * @param paper Pointer to a string containing page dimension information, i.e.
 * "A4", "A3",..., or "<width>x<height>" in millimeters.
 */
void init_rd_paper(struct rdata *rd, const char *paper)
{
   char buf[strlen(paper) + 1], *s, *angle;
   double a4_w, a4_h;

   a4_w = MM2PX(210);
   a4_h = MM2PX(297);

   strcpy(buf, paper);
   strtok(buf, ":");
   angle = strtok(NULL, ":");

   if (strchr(buf, 'x'))
   {
      if ((s = strtok(buf, "x")) == NULL)
         log_msg(LOG_ERR, "strtok returned NULL"),
            exit(EXIT_FAILURE);
      rd->w = MM2PX(atof(s));
      if ((s = strtok(NULL, "x")) == NULL)
         log_msg(LOG_ERR, "format error in page size: '%s'", buf),
            exit(EXIT_FAILURE);
      rd->h = MM2PX(atof(s));
      
      if ((rd->w < 0) || (rd->h < 0))
         log_msg(LOG_ERR, "page width and height must be a decimal value greater than 0"),
            exit(EXIT_FAILURE);

      if (!rd->w && !rd->h)
         log_msg(LOG_ERR, "width and height cannot both be 0"),
            exit(EXIT_FAILURE);
   } 
   else if (!strcasecmp(buf, "A4"))
   {
      rd->w = a4_w;
      rd->h = a4_h;
   }
   else if (!strcasecmp(buf, "A3"))
   {
      rd->w = a4_h;
      rd->h = a4_w * 2;
   }
   else if (!strcasecmp(buf, "A2"))
   {
      rd->w = a4_w * 2;
      rd->h = a4_h * 2;
   }
   else if (!strcasecmp(buf, "A1"))
   {
      rd->w = a4_h * 2;
      rd->h = a4_w * 4;
   }
   else if (!strcasecmp(buf, "A0"))
   {
      rd->w = a4_w * 4;
      rd->h = a4_h * 4;
   }
   else
   {
      log_msg(LOG_WARN, "unknown page size %s, defaulting to A4", buf);
      rd->w = a4_w;
      rd->h = a4_h;
   }

   if (rd->flags & RD_LANDSCAPE)
   {
      a4_w = rd->w;
      rd->w = rd->h;
      rd->h = a4_w;
   }

   rd->pgw = rd->w;
   rd->pgh = rd->h;
   page_rotate(rd, angle);
}


static void print_version(void)
{
   printf("Seamark renderer V" PACKAGE_VERSION ", (c) 2011-2018, Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>.\n"
          "See http://www.abenteuerland.at/smrender/ for more information.\n");
#ifdef HAVE_CAIRO
   printf("Using libcairo %s.\n", cairo_version_string());
#else
   printf("Compiled without libcairo support.\n");
#endif
}


void usage(const char *s)
{
   print_version();
   printf(
         "usage: %s [OPTIONS] <window>\n"
         "   <window> := <center> | <bbox>\n"
         "   <bbox>   := <left lower>:<right upper>\n"
         "   <left lower> := <coords>\n"
         "   <right upper> := <coords>\n"
         "   <center> := <coords>:<size>\n"
         "   <coords> := <lat>:<lon>\n"
         "   <size>   := <scale> | <length>'d' | <length>'m'\n"
         "               <scale> Scale of chart.\n"
         "               <length> Length of mean latitude in either degrees ('d') or\n"
         "                        nautical miles ('m')\n"
         "   -a .................. Render all nodes, otherwise only nodes which are\n"
         "                         on the page are rendered.\n"
         "   -b <color> .......... Choose background color ('white' is default).\n"
         "   -D .................. Increase verbosity (can be specified multiple times).\n"
         "   -d <density> ........ Set image density (300 is default).\n"
         "   -f .................. Use loading filter.\n"
         "   -g <grd>[:<t>[:<s>]]  Distance of grid/ticks/subticks in minutes.\n"
         "   -G .................. Do not generate grid nodes/ways.\n"
         "   -i <osm input> ...... OSM input data (default is stdin).\n"
         "   -k <filename> ....... Generate KAP file.\n"
         "   -K <filename> ....... Generate KAP header file.\n"
         "   -L <logfile> ........ Save log output to <logfile>.\n"
         "   -l .................. Select landscape output. Only useful with option -P.\n"
         "   -M .................. Input file is memory mapped (default).\n"
         "   -m .................. Input file is read into heap memory.\n"
         "   -N <offset> ......... Add numerical <offset> to all IDs in output data.\n"
         "   -n .................. Output IDs as positive values only.\n"
         "   -r <rules file> ..... Rules file ('rules.osm' is default).\n"
         "                         Set <rules file> to 'none' to run without rules.\n"
         "   -R <file> ........... Output all rules to <file>.\n"
         "   -s <img scale> ...... Set global image scale (default = 1).\n"
         "   -t <title> .......... Set descriptional chart title.\n"
         "   -T <tile_info> ...... Create tiles.\n"
         "      <tile_info> := <zoom_lo> [ '-' <zoom_hi> ] ':' <tile_path> [ ':' <file_type> ]\n"
         "      <file_type> := 'png' | 'jpg'\n"
         "   -o <image file> ..... Name of output file. The extensions determines the output format.\n"
         "                         Currently supported formats: .PDF, .PNG, .SVG.\n"
         "   -O <pdf file> ....... Filename of output PDF file (DEPRECATED).\n"
         "   -P <page format> .... Select output page format.\n"
         "   -u .................. Output URLs suitable for OSM data download and\n"
         "                         exit.\n"
         "   -V .................. Show chart parameters and exit.\n"
         "   -v .................. Print version and exit.\n"
         "   -w <osm file> ....... Output OSM data to file.\n",
         s
         );
}


int cmp_int(const int *a, const int *b)
{
   return *a - *b;
}


int cnt_cmd_args(const char **argv)
{
   int i;

   for (i = 0; *argv; argv++)
   {
      i += strlen(*argv) + 1;
      if (strchr(*argv, ' ') != NULL)
         i += 2;
   }

   return i;
}


char *mk_cmd_line(const char **argv)
{
   int i;
   char *cmdl;

   if ((cmdl = malloc(cnt_cmd_args(argv))) == NULL)
      log_msg(LOG_ERR, "malloc failed: %s", strerror(errno)),
         exit(EXIT_FAILURE);

   for (i = 0; *argv; argv++)
   {
      if (i)
      {
         cmdl[i] = ' ';
         i++;
      }
      if (strchr(*argv, ' '))
      {
         i += sprintf(&cmdl[i], "\"%s\"", *argv);
      }
      else
      {
         strcpy(&cmdl[i], *argv);
         i += strlen(*argv);
      }
   }
   cmdl[i] = '\0';

   return cmdl;
}


int parse_tile_info(char *tstr, struct tile_info *ti)
{
   char *s, *p;

   if (tstr == NULL)
      return -1;

   memset(ti, 0, sizeof(*ti));

   s = strtok(tstr, ":");
   ti->zlo = atoi(s);
   if ((p = strchr(tstr, '-')) != NULL)
      ti->zhi = atoi(p + 1);
   else
      ti->zhi = ti->zlo;

   if (ti->zlo < 0)
      ti->zlo = 0;

   if (ti->zhi < ti->zlo)
   {
      log_msg(LOG_ERR, "error in tile_info string '%s'", tstr);
      return -1;
   }

   if ((ti->path = strtok(NULL, ":")) == NULL)
   {
      ti->path = ".";
      return 0;
   }

   if ((s = strtok(NULL, ":")) == NULL)
      return 0;

   if (!strcasecmp("jpg", s))
      ti->ftype = 1;

   return 0;
}


/*! This function initializes all parameters for rendering in dependence of the
 * command line arguments. This is the page dimension, projection parameters
 * and chart scale.
 * @param rd Pointer to the rdata structure.
 * @param win Pointer to the string which specifies the geograhic rendering
 * window.
 * @param paper Pointer to the string which specifies the page dimension.
 */
void init_rendering_window(struct rdata *rd, char *win, const char *paper)
{
   char *s;
   int n;

   if (win == NULL)
   {
      log_msg(LOG_WARN, "window parameter missing, setting defaults 0:0:100000 and activating option -a");
      rd->scale = 100000;
      render_all_nodes_ = 1;
   }
   else
   {
      int i = strcnt(win, ':');
      double param;

      if (i != 2 && i != 3 && i != 7)
         log_msg(LOG_ERR, "format error in window"), exit(EXIT_FAILURE);

      s = strtok(win, ":");
      n = parse_coord(s, &param);
      if (n == COORD_LON)
         rd->mean_lon = param;
      else
         rd->mean_lat = param;

      s = strtok(NULL, ":");
      n = parse_coord(s, &param);
      if (n == COORD_LAT)
         rd->mean_lat = param;
      else
         rd->mean_lon = param;

      s = strtok(NULL, ":");
      // window contains length of mean latitude
      if (i == 2)
      {
         if ((param = atof(s)) <= 0)
            log_msg(LOG_ERR, "illegal size argument, must be > 0"), exit(EXIT_FAILURE);
 
         if (isdigit((unsigned) s[strlen(s) - 1]) || (s[strlen(s) - 1] == '.'))
            rd->scale = param;
         else if (s[strlen(s) - 1] == 'm')
            rd->mean_lat_len = param / 60;
         else if (s[strlen(s) - 1] == 'd')
            rd->wc = param;
         else
            log_msg(LOG_ERR, "illegal size parameter"), exit(EXIT_FAILURE);
      }
      // window is bounding box
      else if (i == 3)
      {
         rd->bb.ll.lon = rd->mean_lon;
         rd->bb.ll.lat = rd->mean_lat;
   
         n = parse_coord(s, &param);
         if (n == COORD_LON)
            rd->bb.ru.lon = param;
         else
            rd->bb.ru.lat = param;

         s = strtok(NULL, ":");
         n = parse_coord(s, &param);
         if (n == COORD_LAT)
            rd->bb.ru.lat = param;
         else
            rd->bb.ru.lon = param;

         rd->mean_lon = (rd->bb.ru.lon + rd->bb.ll.lon) / 2.0;
         rd->mean_lat = (rd->bb.ru.lat + rd->bb.ll.lat) / 2.0;
      }
      else if (i == 7)
      {
         rd->polygon_window = 1;
         rd->bb.ll.lon = rd->bb.ru.lon = rd->pw[0].lon = rd->mean_lon;
         rd->bb.ll.lat = rd->bb.ru.lat = rd->pw[0].lat = rd->mean_lat;

         for (i = 1; i < 4; i++)
         {
            n = parse_coord(s, &param);
            if (n == COORD_LON)
               rd->pw[i].lon = param;
            else
               rd->pw[i].lat = param;

            s = strtok(NULL, ":");
            n = parse_coord(s, &param);
            if (n == COORD_LAT)
               rd->pw[i].lat = param;
            else
               rd->pw[i].lon = param;

            rd->bb.ll.lon = fmin(rd->bb.ll.lon, rd->pw[i].lon);
            rd->bb.ll.lat = fmin(rd->bb.ll.lat, rd->pw[i].lat);
            rd->bb.ru.lon = fmax(rd->bb.ru.lon, rd->pw[i].lon);
            rd->bb.ru.lat = fmax(rd->bb.ru.lat, rd->pw[i].lat);

            s = strtok(NULL, ":");
         }
         rd->mean_lon = (rd->bb.ru.lon + rd->bb.ll.lon) / 2.0;
         rd->mean_lat = (rd->bb.ru.lat + rd->bb.ll.lat) / 2.0;
      }
      else
      {
         log_msg(LOG_EMERG, "fatal window error: this should never happend");
         exit(EXIT_FAILURE);
      }
   }

   init_rd_paper(rd, paper);
   if (rd->scale > 0)
   {
      if (!rd->w || !rd->h)
         log_msg(LOG_ERR, "zero height or width only possible with bounding box window"),
            exit(EXIT_FAILURE);
      rd->mean_lat_len = rd->scale * ((double) rd->w / (double) rd->dpi) * 2.54 / (60.0 * 1852 * 100);
   }
   else if (rd->wc > 0)
   {
      if (!rd->w || !rd->h)
         log_msg(LOG_ERR, "zero height or width only possible with bounding box window"),
            exit(EXIT_FAILURE);
      rd->mean_lat_len  = rd->wc * cos(rd->mean_lat * M_PI / 180);
   }
   else if (rd->mean_lat_len == 0)
   {
      rd->mean_lat_len = (rd->bb.ru.lon - rd->bb.ll.lon) * cos(DEG2RAD(rd->mean_lat));

      // autofit page
      if (!rd->w)
         rd->w = round((double) rd->h * rd->mean_lat_len / (rd->bb.ru.lat - rd->bb.ll.lat));
      else if (!rd->h)
         rd->h = round((double) rd->w * (rd->bb.ru.lat - rd->bb.ll.lat) / rd->mean_lat_len);

      if (rd->mean_lat_len * rd->h / rd->w < rd->bb.ru.lat - rd->bb.ll.lat)
      {
         rd->mean_lat_len = (rd->bb.ru.lat - rd->bb.ll.lat) * rd->w / rd->h;
         //log_msg(LOG_INFO, "bbox widened from %.2f to %.2f nm", (rd->bb.ru.lon - rd->bb.ll.lon) * cos(DEG2RAD(rd->mean_lat)) * 60, rd->mean_lat_len * 60);
      }
   }

   init_bbox_mll(rd);
}


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0, n, i, norules = 0;
   struct stat st;
   FILE *f;
   char *cf = "rules.osm", *img_file = NULL, *osm_ifile = NULL, *osm_ofile =
      NULL, *osm_rfile = NULL, *kap_file = NULL, *kap_hfile = NULL, *pdf_file = NULL, *svg_file = NULL;
   struct rdata *rd;
   struct timeval tv_start, tv_end;
   int w_mmap = 1, load_filter = 0, init_exit = 0, gen_grid = AUTO_GRID, prt_url = 0;
   char *paper = "A3", *bg = NULL;
   struct filter fi;
   struct dstats rstats;
   struct grid grd;
   char *s;
   struct tile_info ti;
   int level = 5;    // default log level: 5 = LOG_NOTICE
   char *logfile = "stderr";

   (void) gettimeofday(&tv_start, NULL);
   init_log(logfile, level);
   rd = get_rdata();
   init_grid(&grd);
   rd->cmdline = mk_cmd_line((const char**) argv);
   memset(&ti, 0, sizeof(ti));
   if (sizeof(long) < 8)
      log_msg(LOG_WARN, "system seems to have %d bits only. This may lead to errors.", (int) sizeof(long) * 8);

   if (setlocale(LC_CTYPE, "") == NULL)
      log_msg(LOG_WARN, "setlocale() failed");

   while ((n = getopt(argc, argv, "ab:Dd:fg:Ghi:k:K:lL:MmN:no:O:P:r:R:s:t:T:uVvw:")) != -1)
      switch (n)
      {
         case 'a':
            render_all_nodes_ = 1;
            break;

         case 'b':
            bg = optarg;
            break;

         case 'D':
            if (level < 7)
            {
               level++;
               init_log(logfile, level);
            }
            break;

         case 'd':
            if ((rd->dpi = atoi(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal dpi argument %s", optarg),
                  exit(EXIT_FAILURE);
            break;

         case 'g':
            gen_grid = USER_GRID;
            if ((s = strtok(optarg, ":")) == NULL)
               log_msg(LOG_ERR, "ill grid parameter"), exit(EXIT_FAILURE);

            grd.lat_g = grd.lon_g = atof(s) / 60;

            if ((s = strtok(NULL, ":")) == NULL)
            {
               grd.lat_ticks = grd.lon_ticks = grd.lat_g / 10;
               break;
            }
            grd.lat_ticks = grd.lon_ticks = atof(s) / 60;

            if ((s = strtok(NULL, ":")) == NULL)
            {
               if (!((int) round(grd.lat_ticks * 600) % 4))
                  grd.lat_sticks = grd.lon_sticks = grd.lat_ticks / 4;
               else
                  grd.lat_sticks = grd.lon_sticks = grd.lat_ticks / 5;
               break;
            }

            grd.lat_sticks = grd.lon_sticks = atof(s) / 60;
            break;

         case 'G':
            gen_grid = 0;
            break;

         case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);

         case 'f':
            load_filter = 1;
            break;

         case 'i':
            osm_ifile = optarg;
            break;

         case 'k':
            kap_file = optarg;
            break;

         case 'K':
            kap_hfile = optarg;
            break;

         case 'L':
            logfile = optarg;
            init_log(logfile, level);
            break;

         case 'l':
            rd->flags |= RD_LANDSCAPE;
            break;

         case 'M':
            w_mmap = 1;
            break;

         case 'm':
            w_mmap = 0;
            break;

         case 'N':
            errno = 0;
            rd->id_off = strtoll(optarg, NULL, 0);
            if (errno)
               log_msg(LOG_ERR, "could not convert '%s': %s", optarg, strerror(errno)),
                  exit(EXIT_FAILURE);
            log_debug("id_off = %"PRId64, rd->id_off);
            break;

         case 'n':
            rd->flags |= RD_UIDS;
            break;

         case 'o':
            log_debug("parsing '-o %s'", optarg);
            if (strlen(optarg) >= 4)
            {
               if (!strcasecmp(optarg + strlen(optarg) - 4, ".png"))
               {
                  img_file = optarg;
               }
               else if (!strcasecmp(optarg + strlen(optarg) - 4, ".pdf"))
               {
                  pdf_file = optarg;
               }
               else if (!strcasecmp(optarg + strlen(optarg) - 4, ".svg"))
               {
                  svg_file = optarg;
               }
               else
               {
                  log_msg(LOG_NOTICE, "output file type for %s defaults to PNG", optarg);
                  img_file = optarg;
               }
            }
            else
               img_file = optarg;
            break;

         case 'O':
            pdf_file = optarg;
            break;

         case 'P':
            paper = optarg;
            break;

         case 'r':
            cf = optarg;
            if (!strcmp(cf, "none"))
               cf = NULL, norules++;
            break;

         case 'R':
            osm_rfile = optarg;
            break;

         case 's':
            errno = 0;
            rd->img_scale = strtod(optarg, NULL);
            if (errno)
            {
               log_msg(LOG_ERR, "illegal image scaling: %s", strerror(errno));
               rd->img_scale = 1;
            }
            break;

         case 't':
            rd->title = optarg;
            break;

         case 'T':
            if (parse_tile_info(optarg, &ti))
            {
               log_msg(LOG_ERR, "failed to parse tile info '%s'", optarg);
               exit(EXIT_FAILURE);
            }
            break;

         case 'u':
            prt_url = 1;
            break;

         case 'V':
            init_exit = 1;
            break;

         case 'v':
            print_version();
            exit(EXIT_SUCCESS);

         case 'w':
            osm_ofile = optarg;
            break;
      }

   log_debug("args: %s", rd->cmdline);

   init_rendering_window(rd, argv[optind], paper);

   if (prt_url)
   {
      print_url(rd->bb);
      exit(EXIT_SUCCESS);
   }

   rdata_log();

   if (init_exit)
      exit(EXIT_SUCCESS);

   // install exit handlers
   osm_read_exit();

   // preparing image
#ifdef HAVE_CAIRO
   cairo_smr_init_main_image(bg);
#endif

   if ((cfctl = open_osm_source(cf, 0)) == NULL)
      exit(EXIT_FAILURE);

   log_msg(LOG_NOTICE, "reading rules (file size %ld kb)", (long) cfctl->len / 1024);
   (void) read_osm_file(cfctl, &rd->rules, NULL, &rstats);
   (void) close(cfctl->fd);

   if (!rstats.cnt[OSM_NODE] && !rstats.cnt[OSM_WAY] && !rstats.cnt[OSM_REL])
   {
      log_msg(LOG_NOTICE, "no rules found");
      norules++;
   }

   qsort(rstats.ver, rstats.ver_cnt, sizeof(int), (int(*)(const void*, const void*)) cmp_int);
   for (n = 0; n < rstats.ver_cnt; n++)
      log_msg(LOG_DEBUG, " rstats.ver[%d] = %d", n, rstats.ver[n]);

   if (osm_rfile != NULL)
   {
      traverse(rd->rules, 0, IDX_NODE, norm_rule_node, NULL);
      traverse(rd->rules, 0, IDX_WAY, norm_rule_way, &rstats);
      // FIXME: saving relation rules missing
      save_osm(osm_rfile, rd->rules, NULL, NULL);
   }

   if (!norules)
   {
      log_msg(LOG_INFO, "preparing node rules");
      if (traverse(rd->rules, 0, IDX_NODE, (tree_func_t) init_rules, rd->rules) < 0)
         log_msg(LOG_ERR, "rule parser failed"),
            exit(EXIT_FAILURE);
      log_msg(LOG_INFO, "preparing way rules");
      if (traverse(rd->rules, 0, IDX_WAY, (tree_func_t) init_rules, rd->rules) < 0)
         log_msg(LOG_ERR, "rule parser failed"),
            exit(EXIT_FAILURE);
      log_msg(LOG_INFO, "preparing relation rules");
      if (traverse(rd->rules, 0, IDX_REL, (tree_func_t) init_rules, rd->rules) < 0)
         log_msg(LOG_ERR, "rule parser failed"),
            exit(EXIT_FAILURE);
   }

   if ((osm_ifile != NULL) && ((fd = open(osm_ifile, O_RDONLY)) == -1))
      log_msg(LOG_ERR, "cannot open file %s: %s", osm_ifile, strerror(errno)),
         exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if (w_mmap)
   {
      log_msg(LOG_INFO, "input file will be memory mapped with mmap()");
      st.st_size = -st.st_size;
   }
   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_NOTICE, "reading osm data (file size %ld kb, memory at %p)",
         (long) labs(st.st_size) / 1024, ctl->buf.buf);

   if (load_filter)
   {
      memset(&fi, 0, sizeof(fi));
      fi.c1.lat = rd->bb.ru.lat + rd->hc * 0.05;
      fi.c1.lon = rd->bb.ll.lon - rd->wc * 0.05;
      fi.c2.lat = rd->bb.ll.lat - rd->hc * 0.05;
      fi.c2.lon = rd->bb.ru.lon + rd->wc * 0.05;
      fi.use_bbox = 1;
      log_msg(LOG_INFO, "using input bounding box %.3f/%.3f - %.3f/%.3f",
            fi.c1.lat, fi.c1.lon, fi.c2.lat, fi.c2.lon);
      (void) read_osm_file(ctl, get_objtree(), &fi, &rd->ds);
   }
   else
   {
      (void) read_osm_file(ctl, get_objtree(), NULL, &rd->ds);
   }

   if (!rd->ds.cnt[OSM_NODE])
   {
      log_msg(LOG_ERR, "no data to render");
      exit(EXIT_NODATA);
   }

   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);

   log_msg(LOG_INFO, "stripping filtered way nodes");
   traverse(*get_objtree(), 0, IDX_WAY, (tree_func_t) strip_ways, NULL);

   // reverse pointers are only created if requested by some action
   if (rd->need_index)
   {
      log_msg(LOG_INFO, "creating reverse pointers from nodes to ways");
      traverse(*get_objtree(), 0, IDX_WAY, (tree_func_t) rev_index_way_nodes, &rd->index);
      log_msg(LOG_INFO, "creating reverse pointers from relation members to relations");
      traverse(*get_objtree(), 0, IDX_REL, (tree_func_t) rev_index_rel_nodes, &rd->index);
   }

   switch (gen_grid)
   {
      case AUTO_GRID:
         auto_grid(rd, &grd);
         // intentionally, there's no break

      case USER_GRID:
         grid(rd, &grd);
         break;

      default:
         log_debug("no command line grid");
   }

   install_sigint();
   init_cat_poly(rd);

   for (n = 0; (n < rstats.ver_cnt) && !int_ && (rstats.ver[n] < SUBROUTINE_VERSION); n++)
   {
      log_msg(LOG_NOTICE, "rendering pass %d (ver = %d)", n, rstats.ver[n]);
      execute_rules(rd->rules, rstats.ver[n]);
   }

   int_ = 0;

   if (argv[optind] == NULL)
      save_osm(osm_ofile, *get_objtree(), NULL, rd->cmdline);
   else
      save_osm(osm_ofile, *get_objtree(), &rd->bb, rd->cmdline);
   (void) close(ctl->fd);
   hpx_free(ctl);
   hpx_free(cfctl);

   log_debug("freeing main objects");
   traverse(*get_objtree(), 0, IDX_REL, free_objects, NULL);
   traverse(*get_objtree(), 0, IDX_WAY, free_objects, NULL);
   traverse(*get_objtree(), 0, IDX_NODE, free_objects, NULL);

   if (!norules)
   {
      log_debug("freeing rule objects");
      traverse(rd->rules, 0, IDX_REL, (tree_func_t) free_rules, NULL);
      traverse(rd->rules, 0, IDX_WAY, (tree_func_t) free_rules, NULL);
      traverse(rd->rules, 0, IDX_NODE, (tree_func_t) free_rules, NULL);
   }

   log_debug("freeing main object tree");
   bx_free_tree(*get_objtree());
   log_debug("freeing rules tree");
   bx_free_tree(rd->rules);

   if (ti.path != NULL)
   {
      log_msg(LOG_INFO, "creating tiles in directory %s", ti.path);
      for (i = ti.zlo; i <= ti.zhi; i++)
      {
         log_msg(LOG_INFO, "zoom level %d", i);
         (void) create_tiles(ti.path, rd, i, ti.ftype);
      }
   }

   if (img_file != NULL)
   {
      if ((f = fopen(img_file, "w")) != NULL)
      {
         save_main_image(f, FTYPE_PNG);
         fclose(f);
      }
      else
         log_msg(LOG_ERR, "error opening file %s: %s", img_file, strerror(errno));
   }

   if (pdf_file != NULL)
   {
      if ((f = fopen(pdf_file, "w")) != NULL)
      {
         save_main_image(f, FTYPE_PDF);
         fclose(f);
      }
      else
         log_msg(LOG_ERR, "error opening file %s: %s", pdf_file, strerror(errno));
   }

   if (svg_file != NULL)
   {
      if ((f = fopen(svg_file, "w")) != NULL)
      {
         save_main_image(f, FTYPE_SVG);
         fclose(f);
      }
      else
         log_msg(LOG_ERR, "error opening file %s: %s", svg_file, strerror(errno));
   }

   if (kap_file != NULL)
   {
      log_msg(LOG_NOTICE, "generating KAP file %s", kap_file);
      if ((f = fopen(kap_file, "w")) == NULL)
         log_msg(LOG_WARN, "cannot open file %s: %s", kap_file, strerror(errno));
      else
      {
         //gen_kap_header(f, rd);
         save_kap(f, rd);
         fclose(f);
      }
   }

   if (kap_hfile != NULL)
   {
      log_msg(LOG_NOTICE, "generating KAP header file %s", kap_hfile);
      if ((f = fopen(kap_hfile, "w")) == NULL)
         log_msg(LOG_WARN, "cannot open file %s: %s", kap_file, strerror(errno));
      else
      {
         //gen_kap_header(f, rd);
         gen_kap_header(f, rd);
         fclose(f);
      }
   }

   free(rd->cmdline);

   (void) gettimeofday(&tv_end, NULL);
   tv_end.tv_sec -= tv_start.tv_sec;
   tv_end.tv_usec -= tv_start.tv_usec;
   if (tv_end.tv_usec < 0)
   {
      tv_end.tv_sec--;
      tv_end.tv_usec += 1000000;
   }

   log_msg(LOG_NOTICE, "%d.%03d seconds elapsed. exiting", (unsigned) tv_end.tv_sec, (unsigned) tv_end.tv_usec / 1000);
   log_msg(LOG_NOTICE, "Thanks for using smrender!");
   return EXIT_SUCCESS;
}

