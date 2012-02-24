/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <stdint.h>
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

#include "smrender.h"
#include "libhpxml.h"
#include "smlog.h"
#include "smrules.h"
#include "smrparse.h"


static struct rdata rd_;
static volatile sig_atomic_t int_ = 0;


void int_handler(int sig)
{
   int_++;
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
}


/*! Returns degrees and minutes of a fractional coordinate.
 */
void fdm(double x, int *deg, int *min)
{
   double d;

   *min = round(modf(x, &d) * 60);
   *deg = round(d);
   if (*min == 60)
   {
      (*deg)++;
      *min = 0;
   }
}


double fround(double x, double y)
{
   return x - fmod(x, y);
}


char *cfmt(double c, int d, char *s, int l)
{
   // FIXME: modf should be used instead
   switch (d)
   {
      case LAT:
         snprintf(s, l, "%02.0f %c %1.2f", fabs(c), c < 0 ? 'S' : 'N', (c - floor(c)) * 60.0);
         break;

      case LON:
         snprintf(s, l, "%03.0f %c %1.2f", fabs(c), c < 0 ? 'W' : 'E', (c - floor(c)) * 60.0);
         break;

      default:
         *s = '\0';
   }
   return s;
}


/*! Calculate the area and the centroid of a closed polygon.
 *  @param w Pointer to closed polygon.
 *  @param c Pointer to struct coord which will receive the coordinates of the centroid.
 *  @param ar Pointer to variable which will receive the area of the polygon.
 *  The result is the area measured in nautical square miles.
 *  @return Returns 0 on success, -1 on error.
 */
int poly_area(const osm_way_t *w, struct coord *c, double *ar)
{
   double f, x[2];
   osm_node_t *n[2];
   int i;

   if ((n[1] = (osm_node_t*) get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_ERR, "something is wrong with way %ld: node does not exist", w->obj.id);
      return -1;
   }

   *ar = 0;
   c->lat = 0;
   c->lon = 0;

   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      n[0] = n[1];
      if ((n[1] = (osm_node_t*) get_object(OSM_NODE, w->ref[i + 1])) == NULL)
      {
         log_msg(LOG_ERR, "something is wrong with way %ld: node does not exist", w->obj.id);
         return -1;
      }

      x[0] = n[0]->lon * cos(DEG2RAD(n[0]->lat));
      x[1] = n[1]->lon * cos(DEG2RAD(n[1]->lat));
      f = x[0] * n[1]->lat - x[1] * n[0]->lat;
      c->lon += (x[0] + x[1]) * f;
      c->lat += (n[0]->lat + n[1]->lat) * f;
      *ar += f;
      //log_debug("%d %f %f %f %f %f %f %f/%f %f/%f", i, f, sx, sy, cx, cy, ar, n[0]->nd.lon, n[0]->nd.lat, n[1]->nd.lon, n[1]->nd.lat);
   }

   c->lat /= 3.0 * *ar;
   c->lon /= 3.0 * *ar * cos(DEG2RAD(c->lat));
   *ar = fabs(*ar) * 1800.0;

   return 0;
}


int act_poly_area(osm_way_t *w)
{
   double ar;
   struct otag *ot;
   struct coord c;
   char buf[256], *s;

   if (!poly_area(w, &c, &ar))
   {
      log_msg(LOG_DEBUG, "poly_area of %ld = %f", w->obj.id, ar);
      if ((ot = realloc(w->obj.otag, sizeof(struct otag) * (w->obj.tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_DEBUG, "could not realloc tag list: %s", strerror(errno));
         return 0;
      }
      w->obj.otag = ot;
      snprintf(buf, sizeof(buf), "%.8f", ar);
      if ((s = strdup(buf)) == NULL)
      {
         log_msg(LOG_DEBUG, "could not strdup");
         return 0;
      }
      set_const_tag(&w->obj.otag[w->obj.tag_cnt], "smrender:area", s);
      w->obj.tag_cnt++;
   }

   return 0;
}


int act_poly_centroid(osm_way_t *w)
{
   struct coord c;
   double ar;
   osm_node_t *n;
   char buf[256], *s;

   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return -1;

   n = malloc_node(w->obj.tag_cnt + 1);
   n->obj.id = unique_node_id();
   n->obj.ver = 1;
   n->obj.tim = time(NULL);
   n->lat = c.lat;
   n->lon = c.lon;

   snprintf(buf, sizeof(buf), "%ld", w->obj.id);
   if ((s = strdup(buf)) == NULL)
   {
      free_obj((osm_obj_t*) n);
      log_msg(LOG_DEBUG, "could not strdup: %s", strerror(errno));
      return 0;
   }
   set_const_tag(&n->obj.otag[0], "smrender:id:way", s);
   memcpy(&n->obj.otag[1], &w->obj.otag[0], sizeof(struct otag) * w->obj.tag_cnt);
   put_object((osm_obj_t*) n);
 
   log_debug("centroid %.3f/%.3f, ar = %f, way = %ld, node %ld", n->lat, n->lon, ar, w->obj.id, n->obj.id);

   return 0;
}


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_rules0(osm_node_t *n, struct rdata *rd, struct orule *rl)
{
   int i, e;

   if (!rl->rule.type)
   {
      //log_debug("ACT_NA rule ignored");
      return E_RTYPE_NA;
   }

   // check if node has tags
   //if (!nd->tag_cnt)
   //   return 0;

   for (i = 0; i < rl->oo->tag_cnt; i++)
      if (bs_match_attr((osm_obj_t*) n, &rl->oo->otag[i], &rl->rule.stag[i]) == -1)
         return 0;

   //fprintf(stderr, "node id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (rl->rule.type)
   {
      case ACT_IMG:
         e = act_image(n, rd, rl);
         break;

      case ACT_CAP:
         e = act_caption(n, rd, rl);
         break;

      case ACT_FUNC:
         e = rl->rule.func.main.func((osm_obj_t*) n);
         break;

         /*
      case ACT_OUTPUT:
         e = act_output(nd, rl);
         break;

      case ACT_IGNORE:
         e = -1;
         break;
         */

      default:
         e = E_ACT_NOT_IMPL;
         log_warn("action type %d not implemented yet", rl->rule.type);
   }

   return e;
}



/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_wrules0(osm_way_t *w, struct rdata *rd, struct orule *rl)
{
   int i, e;

   if (!rl->rule.type)
   {
      //log_debug("ACT_NA rule ignored");
      return E_RTYPE_NA;
   }

   // check if node has tags
   //if (!nd->tag_cnt)
   //   return 0;

   for (i = 0; i < rl->oo->tag_cnt; i++)
      if (bs_match_attr((osm_obj_t*) w, &rl->oo->otag[i], &rl->rule.stag[i]) == -1)
         return 0;

   //fprintf(stderr, "way id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (rl->rule.type)
   {
      case ACT_DRAW:
         if (w->ref[0] == w->ref[w->ref_cnt - 1])
            e = act_fill_poly(w, rd, rl);
         else
            e = act_open_poly(w, rd, rl);
         break;

      case ACT_FUNC:
         e = rl->rule.func.main.func((osm_obj_t*) w);
         break;

      case ACT_CAP:
         e = act_wcaption(w, rd, rl);
         break;

         /*
      case ACT_OUTPUT:
         e = act_output(nd, rl);
         break;

      case ACT_IGNORE:
         e = -1;
         break;
         */

      default:
         e = E_ACT_NOT_IMPL;
         log_msg(LOG_WARN, "action type %d not implemented yet", rl->rule.type);
   }

   return e;
}


int apply_rules(struct orule *rl, struct rdata *rd, osm_node_t *n)
{
   int e = 0;

   log_debug("applying rule id 0x%016lx type %s(%d)", rl->oo->id, rule_type_str(rl->rule.type), rl->rule.type);

   if (n != NULL)
   {
      if (rl->oo->ver != n->obj.ver)
         return 0;
   }

   // call initialization rule of function rule if available
   if ((rl->rule.type == ACT_FUNC) && (rl->rule.func.ini.func != NULL))
      rl->rule.func.ini.func(rl);

   switch (rl->oo->type)
   {
      case OSM_NODE:
         e = traverse(rd->obj, 0, IDX_NODE, (tree_func_t) apply_rules0, rd, rl);
         break;

      case OSM_WAY:
         e = traverse(rd->obj, 0, IDX_WAY, (tree_func_t) apply_wrules0, rd, rl);
         break;

      default:
         log_debug("unknown rule type");
   }

   // call de-initialization rule of function rule if available
   if ((rl->rule.type == ACT_FUNC) && (rl->rule.func.fini.func != NULL))
      rl->rule.func.fini.func();

   return e;
}


int print_tree(osm_obj_t *o, struct rdata *rd, void *p)
{
   print_onode(p, o);
   return 0;
}


int strip_ways(osm_way_t *w, struct rdata *rd, void *p)
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


int traverse(const bx_node_t *nt, int d, int idx, tree_func_t dhandler, struct rdata *rd, void *p)
{
   int i, e, sidx, eidx;
   static int sig_msg = 0;

   if (int_)
   {
      if (!sig_msg)
      {
         sig_msg = 1;
         log_msg(LOG_NOTICE, "SIGINT catched, breaking rendering recursion");
      }
      return 0;
   }

   if (nt == NULL)
   {
      log_msg(LOG_WARN, "null pointer catched...breaking recursion");
      return -1;
   }

   if ((idx < -1) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_CRIT, "traverse(): idx (%d) out of range", idx);
      return -1;
   }

   if (d == sizeof(bx_hash_t) * 8 / BX_RES)
   {
      if (idx == -1)
      {
         sidx = 0;
         eidx = 1 << BX_RES;
      }
      else
      {
         sidx = idx;
         eidx = sidx + 1;
      }

      for (i = sidx, e = 0; i < eidx; i++)
      {
         if (nt->next[i] != NULL)
         {
            e = dhandler(nt->next[i], rd, p);
            if (e < 0)
            {
               //log_msg(LOG_WARNING, "dhandler() returned %d, breaking recursion.", e);
               return e;
            }
         }
      }
      return e;
   }

   for (i = 0; i < 1 << BX_RES; i++)
      if (nt->next[i])
      {
         e = traverse(nt->next[i], d + 1, idx, dhandler, rd, p);
         if (e < 0)
         {
            log_msg(LOG_WARNING, "traverse() returned %d, breaking recursion.", e);
            return e;
         }
//         else if (e > 0)
//            log_msg(LOG_INFO, "traverse() returned %d", e);
      }

   return 0;
}


void print_rdata(FILE *f, const struct rdata *rd)
{
   log_msg(LOG_NOTICE, "render data: left upper %.3f/%.3f, right bottom %.3f/%.3f",
         rd->y1c, rd->x1c, rd->y2c, rd->x2c);
   log_msg(LOG_NOTICE, "   mean_lat = %.3f°, mean_lat_len = %.3f° (%.1f nm)",
         rd->mean_lat, rd->mean_lat_len, rd->mean_lat_len * 60);
   log_msg(LOG_NOTICE, "   %dx%d px, dpi = %d, page size = %.1f x %.1f mm",
         rd->w, rd->h, rd->dpi, PX2MM(rd->w), PX2MM(rd->h));
   log_msg(LOG_NOTICE, "   scale 1:%.0f, %.1f x %.1f nm",
         rd->scale, rd->wc * 60 * cos(DEG2RAD(rd->mean_lat)), rd->hc * 60);
   log_msg(LOG_NOTICE, "   grid = %.1f', ticks = %.2f', subticks = %.2f'",
         rd->grd.lat_g * 60, rd->grd.lat_ticks * 60, rd->grd.lat_sticks * 60);

   log_debug("G_GRID %.3f, G_TICKS %.3f, G_STICKS %.3f, G_MARGIN %.2f, G_TW %.2f, G_STW %.2f, G_BW %.2f",
         G_GRID, G_TICKS, G_STICKS, G_MARGIN, G_TW, G_STW, G_BW);
}


void init_bbox_mll(struct rdata *rd)
{
   rd->wc = rd->mean_lat_len / cos(rd->mean_lat * M_PI / 180);
   rd->x1c = rd->mean_lon - rd->wc / 2;
   rd->x2c = rd->mean_lon + rd->wc / 2;
   rd->hc = rd->mean_lat_len * rd->h / rd->w;
   rd->y1c = rd->mean_lat + rd->hc / 2.0;
   rd->y2c = rd->mean_lat - rd->hc / 2.0;
   rd->scale = (rd->mean_lat_len * 60.0 * 1852 * 100 / 2.54) / ((double) rd->w / (double) rd->dpi);
}


int print_onode(FILE *f, const osm_obj_t *o)
{
   int i;
#define TBUFLEN 24
   char ts[TBUFLEN] = "0000-00-00T00:00:00Z";
   struct tm *tm;

   if (o == NULL)
   {
      log_warn("NULL pointer catched in print_onode()");
      return -1;
   }

   if ((tm = gmtime(&o->tim)) != NULL)
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

   switch (o->type)
   {
      case OSM_NODE:
         fprintf(f, "<node id=\"%ld\" version=\"%d\" lat=\"%f\" lon=\"%f\" timestamp=\"%s\" uid=\"%d\">\n",
               o->id, o->ver, ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon, ts, o->uid);
         break;

      case OSM_WAY:
         fprintf(f, "<way id=\"%ld\" version=\"%d\" timestamp=\"%s\" uid=\"%d\">\n",
               o->id, o->ver, ts, o->uid);
         break;

      default:
         fprintf(f, "<!-- unknown node type: %d -->\n", o->type);
         return -1;
   }

   for (i = 0; i < o->tag_cnt; i++)
      fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            (int) o->otag[i].k.len, o->otag[i].k.buf, (int) o->otag[i].v.len, o->otag[i].v.buf);

  switch (o->type)
   {
      case OSM_NODE:
         fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            fprintf(f, "<nd ref=\"%ld\"/>\n", ((osm_way_t*) o)->ref[i]);
         fprintf(f, "</way>\n");
         break;
   }

   return 0;
}


void init_stats(struct dstats *ds)
{
   memset(ds, 0, sizeof(*ds));
   ds->min_nid = ds->min_wid = INT64_MAX;
   ds->max_nid = ds->max_wid = INT64_MIN;
   ds->lu.lat = -90;
   ds->rb.lat = 90;
   ds->lu.lon = 180;
   ds->rb.lon = -180;
   ds->lo_addr = (void*) (-1L);
   //ds->hi_addr = 0;
}

 
void node_stats(osm_node_t *n, struct dstats *ds)
{
   ds->ncnt++;
   if (ds->lu.lat < n->lat) ds->lu.lat = n->lat;
   if (ds->lu.lon > n->lon) ds->lu.lon = n->lon;
   if (ds->rb.lat > n->lat) ds->rb.lat = n->lat;
   if (ds->rb.lon < n->lon) ds->rb.lon = n->lon;
}

int onode_stats(osm_obj_t *o, struct rdata *rd, struct dstats *ds)
{
   int i;

   if (o->type == OSM_NODE)
   {
      ds->ncnt++;
      node_stats((osm_node_t*) o, ds);
      if (ds->min_nid > o->id) ds->min_nid = o->id;
      if (ds->max_nid < o->id) ds->max_nid = o->id;
   }
   else if (o->type == OSM_WAY)
   {
      ds->wcnt++;
      if (ds->min_wid > o->id) ds->min_wid = o->id;
      if (ds->max_wid < o->id) ds->max_wid = o->id;
   }
   if ((void*) o > ds->hi_addr)
      ds->hi_addr = o;
   if ((void*) o < ds->lo_addr)
      ds->lo_addr = o;

   // look if version is registered
   for (i = 0; i < ds->ver_cnt; i++)
   {
      if (ds->ver[i] == o->ver)
         break;
   }
   //version was not found and array has enough entries
   if ((i >= ds->ver_cnt) && (ds->ver_cnt < MAX_ITER))
   {
      ds->ver[ds->ver_cnt] = o->ver;
      ds->ver_cnt++;
   }

   return 0;
}


int save_osm(struct rdata *rd, const char *s)
{
   FILE *f;

   if (s == NULL)
      return -1;

   log_msg(LOG_INFO, "saving osm output to '%s'", s);
   if ((f = fopen(s, "w")) != NULL)
   {
      fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
      traverse(rd->obj, 0, IDX_NODE, print_tree, rd, f);
      traverse(rd->obj, 0, IDX_WAY, print_tree, rd, f);
      fprintf(f, "</osm>\n");
      fclose(f);
   }
   else
      log_msg(LOG_WARN, "could not open '%s': %s", s, strerror(errno));

   return 0;
}


struct rdata *init_rdata(void)
{
   memset(&rd_, 0, sizeof(rd_));
   rd_.dpi = 300;
   rd_.grd.lat_ticks = rd_.grd.lon_ticks = G_TICKS;
   rd_.grd.lat_sticks = rd_.grd.lon_sticks = G_STICKS;
   rd_.grd.lat_g = rd_.grd.lon_g = G_GRID;

   return &rd_;
}


/*! Initializes data about paper (image) size.
 *  rd->dpi must be pre-initialized!
 */
void init_rd_paper(struct rdata *rd, const char *paper, int landscape)
{
   char buf[strlen(paper) + 1], *s;
   double a4_w, a4_h;

   a4_w = MM2PX(210);
   a4_h = MM2PX(296.9848);

   if (strchr(paper, 'x'))
   {
      strcpy(buf, paper);
      if ((s = strtok(buf, "x")) == NULL)
         log_msg(LOG_ERR, "strtok returned NULL"),
            exit(EXIT_FAILURE);
      rd->w = MM2PX(atof(s));
      if ((s = strtok(NULL, "x")) == NULL)
         log_msg(LOG_ERR, "format error in page size: '%s'", paper),
            exit(EXIT_FAILURE);
      rd->h = MM2PX(atof(s));
      
      if ((rd->w <= 0) || (rd->h <= 0))
         log_msg(LOG_ERR, "page width and height must be a decimal value greater than 0"),
            exit(EXIT_FAILURE);

      return;
   }

   if (!strcasecmp(paper, "A4"))
   {
      rd->w = a4_w;
      rd->h = a4_h;
   }
   else if (!strcasecmp(paper, "A3"))
   {
      rd->w = a4_h;
      rd->h = a4_w * 2;
   }
   else if (!strcasecmp(paper, "A2"))
   {
      rd->w = a4_w * 2;
      rd->h = a4_h * 2;
   }
   else if (!strcasecmp(paper, "A1"))
   {
      rd->w = a4_h * 2;
      rd->h = a4_w * 4;
   }
   else if (!strcasecmp(paper, "A0"))
   {
      rd->w = a4_w * 4;
      rd->h = a4_h * 4;
   }
   else
   {
      log_msg(LOG_WARN, "unknown page size %s, defaulting to A4", paper);
      rd->w = a4_w;
      rd->h = a4_h;
   }

   if (landscape)
   {
      a4_w = rd->w;
      rd->w = rd->h;
      rd->h = a4_w;
   }
}


void init_rd_image(struct rdata *rd)
{
}


void usage(const char *s)
{
   printf("Seamark renderer V1.1, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n"
         "usage: %s [OPTIONS] <window>\n"
         "   <window> := <lat>:<lon>:<size>\n"
         "               <lat> and <lon> specify the coordinates of the centerpoint.\n"
         "   <size>   := <scale> | <length>'d' | <length>'m'\n"
         "               <scale> Scale of chart.\n"
         "               <length> Length of mean meridian in either degrees ('d') or\n"
         "                        nautical miles ('m')\n"
         "   -d <density> ........ Set image density (300 is default).\n"
         "   -f .................. Use loading filter.\n"
         "   -g <grd>[:<t>[:<s>]]  Distance of grid/ticks/subticks in minutes.\n"
         "   -G .................. Do not generate grid nodes/ways.\n"
         "   -i <osm input> ...... OSM input data (default is stdin).\n"
         "   -l .................. Select landscape output.\n"
         "   -M .................. Input file is memory mapped.\n"
         "   -r <rules file> ..... Rules file ('rules.osm' is default).\n"
         "   -o <image file> ..... Filename of output image (stdout is default).\n"
         "   -P <page format> .... Select output page format.\n"
         "   -w <osm file> ....... Output OSM data to file.\n",
         s
         );
}


int cmp_int(const int *a, const int *b)
{
   if (*a < *b)
      return -1;
   if (*a > *b)
      return 1;
   return 0;
}


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0, n;
   struct stat st;
   FILE *f = stdout;
   char *cf = "rules.osm", *img_file = NULL, *osm_ifile = NULL, *osm_ofile = NULL;
   struct rdata *rd;
   struct timeval tv_start, tv_end;
   int gen_grid = 1, landscape = 0, w_mmap = 0, load_filter = 0;
   char *paper = "A3";
   struct filter fi;
   struct dstats rstats;
   //struct osm_node nd;
   osm_obj_t o;
   char *s;
   double param;

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);
   log_msg(LOG_INFO, "initializing structures");
   rd = init_rdata();
   set_util_rd(rd);

   while ((n = getopt(argc, argv, "d:fg:Ghi:lMo:P:r:w:")) != -1)
      switch (n)
      {
         case 'd':
            if ((rd->dpi = atoi(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal dpi argument %s", optarg),
                  exit(EXIT_FAILURE);
            break;

         case 'g':
            if ((s = strtok(optarg, ":")) == NULL)
               log_msg(LOG_ERR, "ill grid parameter"), exit(EXIT_FAILURE);

            rd->grd.lat_g = rd->grd.lon_g = atof(s) / 60;

            if ((s = strtok(NULL, ":")) == NULL)
            {
               rd->grd.lat_ticks = rd->grd.lon_ticks = rd->grd.lat_g / 10;
               break;
            }
            rd->grd.lat_ticks = rd->grd.lon_ticks = atof(s) / 60;


            if ((s = strtok(NULL, ":")) == NULL)
            {
               if (!((int) round(rd->grd.lat_ticks * 600) % 4))
                  rd->grd.lat_sticks = rd->grd.lon_sticks = rd->grd.lat_ticks / 4;
               else
                  rd->grd.lat_sticks = rd->grd.lon_sticks = rd->grd.lat_ticks / 5;
               break;
            }

            rd->grd.lat_sticks = rd->grd.lon_sticks = atof(s) / 60;
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

         case 'M':
#ifndef WITH_MMAP
            log_msg(LOG_ERR, "memory mapping support disable, recompile with WITH_MMAP");
            exit(EXIT_FAILURE);
#endif
            w_mmap = 1;
            break;

         case 'l':
            landscape = 1;
            break;

         case 'o':
            img_file = optarg;
            break;

         case 'P':
            paper = optarg;
            break;

         case 'r':
            cf = optarg;
            break;

         case 'w':
            osm_ofile = optarg;
            break;
      }

   if (argv[optind] == NULL)
      log_msg(LOG_ERR, "window parameter mandatory"), exit(EXIT_FAILURE);

   if ((s = strtok(argv[optind], ":")) == NULL)
      log_msg(LOG_ERR, "latitude paramter missing"), exit(EXIT_FAILURE);
   rd->mean_lat = atof(s);
   if ((s = strtok(NULL, ":")) == NULL)
      log_msg(LOG_ERR, "longitude paramter missing"), exit(EXIT_FAILURE);
   rd->mean_lon = atof(s);
   if ((s = strtok(NULL, ":")) == NULL)
      log_msg(LOG_ERR, "size parameter missing"), exit(EXIT_FAILURE);

   if ((param = atof(s)) <= 0)
      log_msg(LOG_ERR, "illegal size argument for"), exit(EXIT_FAILURE);
 
   if (isdigit(s[strlen(s) - 1]) || (s[strlen(s) - 1] == '.'))
      rd->scale = param;
   else if (s[strlen(s) - 1] == 'm')
      rd->mean_lat_len = param / 60;
   else if (s[strlen(s) - 1] == 'd')
      rd->wc = param;
   else
      log_msg(LOG_ERR, "illegal size parameter"), exit(EXIT_FAILURE);
 
   install_sigusr1();

   // install exit handlers
   osm_read_exit();
   bx_exit();

   init_rd_paper(rd, paper, landscape);

   if (rd->scale > 0)
      rd->mean_lat_len = rd->scale * ((double) rd->w / (double) rd->dpi) * 2.54 / (60.0 * 1852 * 100);
   else if (rd->wc > 0)
      rd->mean_lat_len  = rd->wc * cos(rd->mean_lat * M_PI / 180);

   init_bbox_mll(rd);

   // FIXME: Why stderr?
   print_rdata(stderr, rd);

   // preparing image
   if ((rd->img = gdImageCreateTrueColor(rd->w, rd->h)) == NULL)
      perror("gdImage"), exit(EXIT_FAILURE);
   rd->col[WHITE] = gdImageColorAllocate(rd->img, 255, 255, 255);
   rd->col[BLACK] = gdImageColorAllocate(rd->img, 0, 0, 0);
   rd->col[YELLOW] = gdImageColorAllocate(rd->img, 231,209,74);
   rd->col[BLUE] = gdImageColorAllocate(rd->img, 137, 199, 178);
   rd->col[MAGENTA] = gdImageColorAllocate(rd->img, 120, 8, 44);
   rd->col[BROWN] = gdImageColorAllocate(rd->img, 154, 42, 2);
   gdImageFill(rd->img, 0, 0, rd->col[WHITE]);
   if (!gdFTUseFontConfig(1))
      log_msg(LOG_NOTICE, "fontconfig library not available");

   if ((fd = open(cf, O_RDONLY)) == -1)
      log_msg(LOG_ERR, "cannot open file %s: %s", cf, strerror(errno)),
         exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((cfctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading rules (file size %ld kb)", (long) st.st_size / 1024);
   (void) read_osm_file(cfctl, &rd->rules, NULL);
   (void) close(cfctl->fd);

   log_msg(LOG_INFO, "gathering rule stats");
   init_stats(&rstats);
   traverse(rd->rules, 0, IDX_WAY, (tree_func_t) onode_stats, rd, &rstats);
   traverse(rd->rules, 0, IDX_NODE, (tree_func_t) onode_stats, rd, &rstats);
   qsort(rstats.ver, rstats.ver_cnt, sizeof(int), (int(*)(const void*, const void*)) cmp_int);
   for (n = 0; n < rstats.ver_cnt; n++)
      log_msg(LOG_DEBUG, " rstats.ver[%d] = %d", n, rstats.ver[n]);
 
   log_msg(LOG_INFO, "preparing rules");
   traverse(rd->rules, 0, IDX_NODE, (tree_func_t) prepare_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_WAY, (tree_func_t) prepare_rules, rd, NULL);

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

   log_msg(LOG_INFO, "reading osm data (file size %ld kb, memory at %p)",
         (long) labs(st.st_size) / 1024, ctl->buf.buf);

   if (load_filter)
   {
      memset(&fi, 0, sizeof(fi));
      fi.c1.lat = rd->y1c + rd->hc * 0.05;
      fi.c1.lon = rd->x1c - rd->wc * 0.05;
      fi.c2.lat = rd->y2c - rd->hc * 0.05;
      fi.c2.lon = rd->x2c + rd->wc * 0.05;
      fi.use_bbox = 1;
      log_msg(LOG_INFO, "using input bounding box %.3f/%.3f - %.3f/%.3f",
            fi.c1.lat, fi.c1.lon, fi.c2.lat, fi.c2.lon);
      (void) read_osm_file(ctl, &rd->obj, &fi);
   }
   else
   {
      (void) read_osm_file(ctl, &rd->obj, NULL);
   }

   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);

   log_msg(LOG_INFO, "stripping filtered way nodes");
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) strip_ways, rd, NULL);

   log_msg(LOG_INFO, "gathering stats");
   init_stats(&rd->ds);
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) onode_stats, rd, &rd->ds);
   traverse(rd->obj, 0, IDX_NODE, (tree_func_t) onode_stats, rd, &rd->ds);
   log_msg(LOG_INFO, " ncnt = %ld, min_nid = %ld, max_nid = %ld",
         rd->ds.ncnt, rd->ds.min_nid, rd->ds.max_nid);
   log_msg(LOG_INFO, " wcnt = %ld, min_wid = %ld, max_wid = %ld",
         rd->ds.wcnt, rd->ds.min_wid, rd->ds.max_wid);
   log_msg(LOG_INFO, " left upper %.2f/%.2f, right bottom %.2f/%.2f",
         rd->ds.lu.lat, rd->ds.lu.lon, rd->ds.rb.lat, rd->ds.rb.lon);
   log_msg(LOG_INFO, " lo_addr = %p, hi_addr = %p", rd->ds.lo_addr, rd->ds.hi_addr);

   init_cat_poly(rd);

   if (gen_grid)
   {
      log_msg(LOG_INFO, "generating grid nodes/ways");
      grid2(rd);
   }

   install_sigint();

   memset(&o, 0, sizeof(o));
   for (n = 0; (n < rstats.ver_cnt) && !int_; n++)
   {
      log_msg(LOG_INFO, "rendering pass %d (ver = %d)", n, rstats.ver[n]);
      o.ver = rstats.ver[n];

      log_msg(LOG_INFO, " ways...");
      traverse(rd->rules, 0, IDX_WAY, (tree_func_t) apply_rules, rd, &o);
      log_msg(LOG_INFO, " nodes...");
      traverse(rd->rules, 0, IDX_NODE, (tree_func_t) apply_rules, rd, &o);
   }
   int_ = 0;

   save_osm(rd, osm_ofile);
   (void) close(ctl->fd);
   hpx_free(ctl);
   hpx_free(cfctl);

   log_msg(LOG_INFO, "saving image");
   if (img_file != NULL)
   {
      if ((f = fopen(img_file, "w")) == NULL)
         log_msg(LOG_ERR, "error opening file %s: %s", img_file, strerror(errno)),
            exit(EXIT_FAILURE);
   }
   gdImagePng(rd->img, f);
   if (img_file != NULL)
      fclose(f);
   gdImageDestroy(rd->img);

   (void) gettimeofday(&tv_end, NULL);
   tv_end.tv_sec -= tv_start.tv_sec;
   tv_end.tv_usec -= tv_start.tv_usec;
   if (tv_end.tv_usec < 0)
   {
      tv_end.tv_sec--;
      tv_end.tv_usec += 1000000;
   }

   log_msg(LOG_INFO, "%d.%03d seconds elapsed. exiting", tv_end.tv_sec, tv_end.tv_usec / 1000);
   log_msg(LOG_INFO, "Thanks for using smrender!");
   return EXIT_SUCCESS;
}

