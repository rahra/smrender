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

#include "smrender.h"
#include "libhpxml.h"
#include "smlog.h"
#include "smrules.h"
#include "smrparse.h"


static struct rdata rd_;


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


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_rules0(struct onode *nd, struct rdata *rd, struct orule *rl)
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

   for (i = 0; i < rl->ond->tag_cnt; i++)
      if (bs_match_attr(nd, &rl->ond->otag[i], &rl->rule.stag[i]) == -1)
         return 0;

   //fprintf(stderr, "node id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (rl->rule.type)
   {
      case ACT_IMG:
         e = act_image(nd, rd, rl);
         break;

      case ACT_CAP:
         e = act_caption(nd, rd, rl);
         break;

      case ACT_FUNC:
         e = rl->rule.func.main.func(nd);
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
int apply_wrules0(struct onode *nd, struct rdata *rd, struct orule *rl)
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

   for (i = 0; i < rl->ond->tag_cnt; i++)
      if (bs_match_attr(nd, &rl->ond->otag[i], &rl->rule.stag[i]) == -1)
         return 0;

   //fprintf(stderr, "way id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (rl->rule.type)
   {
      case ACT_DRAW:
         if (nd->ref[0] == nd->ref[nd->ref_cnt - 1])
            e = act_fill_poly(nd, rd, rl);
         else
            e = act_open_poly(nd, rd, rl);
         break;

      case ACT_FUNC:
         e = rl->rule.func.main.func(nd);
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


int apply_rules(struct orule *rl, struct rdata *rd, struct osm_node *nd)
{
   int e = 0;

   log_debug("applying rule id 0x%016lx type %s(%d)", rl->ond->nd.id, rule_type_str(rl->rule.type), rl->rule.type);

   if (nd != NULL)
   {
      if (rl->ond->nd.ver != nd->ver)
         return 0;
   }

   // call initialization rule of function rule if available
   if ((rl->rule.type == ACT_FUNC) && (rl->rule.func.ini.func != NULL))
      rl->rule.func.ini.func(rl->rule.func.parm);

   switch (rl->ond->nd.type)
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

   /*if ((rl->rule.type == ACT_OUTPUT) && (rl->rule.out.fhandle != NULL))
   {
      fprintf(rl->rule.out.fhandle, "</osm>\n");
      fclose(rl->rule.out.fhandle);
   }*/

   return e;
}


int print_tree(struct onode *nd, struct rdata *rd, void *p)
{
   print_onode(p, nd);
   return 0;
}


int strip_ways(struct onode *w, struct rdata *rd, void *p)
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
      log_debug("way %ld has no nodes", w->nd.id);
   }

   return 0;
}


int traverse(const bx_node_t *nt, int d, int idx, tree_func_t dhandler, struct rdata *rd, void *p)
{
   int i, e, sidx, eidx;

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


int print_onode(FILE *f, const struct onode *nd)
{
   int i;
#define TBUFLEN 24
   char ts[TBUFLEN] = "0000-00-00T00:00:00Z";
   struct tm *tm;

   if (nd == NULL)
   {
      log_warn("NULL pointer catched in print_onode()");
      return -1;
   }

   if ((tm = gmtime(&nd->nd.tim)) != NULL)
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

   switch (nd->nd.type)
   {
      case OSM_NODE:
         fprintf(f, "<node id=\"%ld\" version=\"%d\" lat=\"%f\" lon=\"%f\" timestamp=\"%s\" uid=\"%d\">\n",
               nd->nd.id, nd->nd.ver, nd->nd.lat, nd->nd.lon, ts, nd->nd.uid);
         break;

      case OSM_WAY:
         fprintf(f, "<way id=\"%ld\" version=\"%d\" timestamp=\"%s\" uid=\"%d\">\n",
               nd->nd.id, nd->nd.ver, ts, nd->nd.uid);
         break;

      default:
         fprintf(f, "<!-- unknown node type: %d -->\n", nd->nd.type);
         return -1;
   }

   for (i = 0; i < nd->tag_cnt; i++)
      fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            (int) nd->otag[i].k.len, nd->otag[i].k.buf, (int) nd->otag[i].v.len, nd->otag[i].v.buf);

   for (i = 0; i < nd->ref_cnt; i++)
      fprintf(f, "<nd ref=\"%ld\"/>\n", nd->ref[i]);

   switch (nd->nd.type)
   {
      case OSM_NODE:
         fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
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

 
int onode_stats(struct onode *nd, struct rdata *rd, struct dstats *ds)
{
   int i;

   if (nd->nd.type == OSM_NODE)
   {
      ds->ncnt++;
      if (ds->lu.lat < nd->nd.lat) ds->lu.lat = nd->nd.lat;
      if (ds->lu.lon > nd->nd.lon) ds->lu.lon = nd->nd.lon;
      if (ds->rb.lat > nd->nd.lat) ds->rb.lat = nd->nd.lat;
      if (ds->rb.lon < nd->nd.lon) ds->rb.lon = nd->nd.lon;
      if (ds->min_nid > nd->nd.id) ds->min_nid = nd->nd.id;
      if (ds->max_nid < nd->nd.id) ds->max_nid = nd->nd.id;
   }
   else if (nd->nd.type == OSM_WAY)
   {
      ds->wcnt++;
      if (ds->min_wid > nd->nd.id) ds->min_wid = nd->nd.id;
      if (ds->max_wid < nd->nd.id) ds->max_wid = nd->nd.id;
   }
   if ((void*) nd > ds->hi_addr)
      ds->hi_addr = nd;
   if ((void*) nd < ds->lo_addr)
      ds->lo_addr = nd;

   // look if version is registered
   for (i = 0; i < ds->ver_cnt; i++)
   {
      if (ds->ver[i] == nd->nd.ver)
         break;
   }
   //version was not found and array has enough entries
   if ((i >= ds->ver_cnt) && (ds->ver_cnt < MAX_ITER))
   {
      ds->ver[ds->ver_cnt] = nd->nd.ver;
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
   double a4_w, a4_h;

   a4_w = MM2PX(210);
   a4_h = MM2PX(296.9848);

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
         "usage: %s -c <...> -(m|s) <...> [OPTIONS]\n"
         "   -c <lat>:<lon> ...... coordinates if center point.\n"
         "   -C .................. Do not close open coastline polygons.\n"
         "   -d <density> ........ Set image density (300 is default).\n"
         "   -f .................. Use loading filter.\n"
         "   -g <grd> ............ Distance of grid in degrees.\n"
         "   -G .................. Do not generate grid nodes/ways.\n"
         "   -i <osm input> ...... OSM input data (default is stdin).\n"
         "   -l .................. Select landscape output.\n"
         "   -M .................. Input file is memory mapped.\n"
         "   -r <rules file> ..... Rules file ('rules.osm' is default).\n"
         "   -s (<scale>|<length>[dm])\n"
         "                         Select scale of chart or length of mean latitude\n"
         "                         (parallel) in nautical miles (m) or in degrees (d).\n"
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
   int gen_grid = 1, prep_coast = 1, landscape = 0, w_mmap = 0, load_filter = 0, cset = 0;
   char *paper = "A3";
   struct filter fi;
   struct dstats rstats;
   struct osm_node nd;
   char *s;
   double param;

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);
   log_msg(LOG_INFO, "initializing structures");
   rd = init_rdata();
   set_util_rd(rd);

   while ((n = getopt(argc, argv, "c:Cd:fg:Ghi:lMo:P:r:s:w:")) != -1)
      switch (n)
      {
         case 'c':
            if ((s = strtok(optarg, ":")) == NULL)
               log_msg(LOG_ERR, "illegal coordinate paramter"), exit(EXIT_FAILURE);
            rd->mean_lat = atof(s);
            if ((s = strtok(NULL, ":")) == NULL)
               log_msg(LOG_ERR, "illegal coordinate paramter"), exit(EXIT_FAILURE);
            rd->mean_lon = atof(s);
            cset = 1;
            break;

         case 'C':
            prep_coast = 0;
            break;

         case 'd':
            if ((rd->dpi = atoi(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal dpi argument %s", optarg),
                  exit(EXIT_FAILURE);
            break;

         case 'g':
            rd->grd.lat_g = rd->grd.lon_g = atof(optarg);
            rd->grd.lat_ticks = rd->grd.lon_ticks = rd->grd.lat_g / 10;
            if (!((int) round(rd->grd.lat_ticks * 600) % 4))
               rd->grd.lat_sticks = rd->grd.lon_sticks = rd->grd.lat_ticks / 4;
            else
               rd->grd.lat_sticks = rd->grd.lon_sticks = rd->grd.lat_ticks / 5;
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

         case 's':
            if ((param = atof(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal argument for mean lat length %s", optarg),
                  exit(EXIT_FAILURE);
 
            if (isdigit(optarg[strlen(optarg) - 1]) || (optarg[strlen(optarg) - 1] == '.'))
               rd->scale = param;
            else if (optarg[strlen(optarg) - 1] == 'm')
               rd->mean_lat_len = param / 60;
            else if (optarg[strlen(optarg) - 1] == 'd')
               rd->wc = param;
            else
               log_msg(LOG_ERR, "illegal parameter for option -s"),
                  exit(EXIT_FAILURE);
            break;

         case 'w':
            osm_ofile = optarg;
            break;
      }

   if ((rd->scale == 0) && (rd->mean_lat_len == 0) && (rd->wc == 0))
      log_msg(LOG_ERR, "option -s is mandatory"),
         exit(EXIT_FAILURE);
   if (!cset)
      log_msg(LOG_ERR, "option -c is mandatory"),
         exit(EXIT_FAILURE);

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
         perror("open"), exit(EXIT_FAILURE);

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
         perror("open"), exit(EXIT_FAILURE);

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
   if (prep_coast)
   {
      log_msg(LOG_INFO, "preparing coastline");
      cat_poly(rd);
   }

   if (gen_grid)
   {
      log_msg(LOG_INFO, "generating grid nodes/ways");
      grid2(rd);
   }

   memset(&nd, 0, sizeof(nd));
   for (n = 0; n < rstats.ver_cnt; n++)
   {
      log_msg(LOG_INFO, "rendering pass %d (ver = %d)", n, rstats.ver[n]);
      nd.ver = rstats.ver[n];

      log_msg(LOG_INFO, " ways...");
      traverse(rd->rules, 0, IDX_WAY, (tree_func_t) apply_rules, rd, &nd);
      log_msg(LOG_INFO, " nodes...");
      traverse(rd->rules, 0, IDX_NODE, (tree_func_t) apply_rules, rd, &nd);
   }

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

