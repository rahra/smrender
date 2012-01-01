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
int apply_rules0(struct onode *nd, struct rdata *rd, struct onode *mnd)
{
   int i, e;

   if (!mnd->rule.type)
   {
      //log_debug("ACT_NA rule ignored");
      return E_RTYPE_NA;
   }

   // check if node has tags
   //if (!nd->tag_cnt)
   //   return 0;

   for (i = 0; i < mnd->tag_cnt; i++)
      if (bs_match_attr(nd, &mnd->otag[i]) == -1)
         return 0;

   //fprintf(stderr, "node id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (mnd->rule.type)
   {
      case ACT_IMG:
         e = act_image(nd, rd, mnd);
         break;

      case ACT_CAP:
         e = act_caption(nd, rd, mnd);
         break;

      case ACT_FUNC:
         e = mnd->rule.func.func(nd);
         break;

      case ACT_IGNORE:
         e = -1;
         break;

      default:
         e = E_ACT_NOT_IMPL;
         log_warn("action type %d not implemented yet", mnd->rule.type);
   }

   return e;
}


int apply_rules(struct onode *nd, struct rdata *rd, void *vp)
{
   log_debug("applying rule id 0x%016lx type %s(%d)", nd->nd.id, rule_type_str(nd->rule.type), nd->rule.type);
   return traverse(rd->obj, 0, IDX_NODE, (tree_func_t) apply_rules0, rd, nd);
}


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_wrules0(struct onode *nd, struct rdata *rd, struct onode *mnd)
{
   int i, e;

   if (!mnd->rule.type)
   {
      //log_debug("ACT_NA rule ignored");
      return E_RTYPE_NA;
   }

   // check if node has tags
   //if (!nd->tag_cnt)
   //   return 0;

   for (i = 0; i < mnd->tag_cnt; i++)
      if (bs_match_attr(nd, &mnd->otag[i]) == -1)
         return 0;

   //fprintf(stderr, "way id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (mnd->rule.type)
   {
      case ACT_DRAW:
         if (nd->ref[0] == nd->ref[nd->ref_cnt - 1])
            e = act_fill_poly(nd, rd, mnd);
         else
            e = act_open_poly(nd, rd, mnd);
         break;

      case ACT_FUNC:
         e = mnd->rule.func.func(nd);
         break;

      case ACT_IGNORE:
         e = -1;
         break;

      default:
         e = E_ACT_NOT_IMPL;
         log_msg(LOG_WARN, "action type %d not implemented yet", mnd->rule.type);
   }

   return e;
}


int apply_wrules(struct onode *nd, struct rdata *rd, void *vp)
{
   log_debug("applying rule id 0x%016lx type %s(%d)", nd->nd.id, rule_type_str(nd->rule.type), nd->rule.type);
   return traverse(rd->obj, 0, IDX_WAY, (tree_func_t) apply_wrules0, rd, nd);
}


int print_tree(struct onode *nd, struct rdata *rd, void *p)
{
   print_onode(p, nd);
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
         rd->x1c, rd->y1c, rd->x2c, rd->y2c);
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


void init_bbox_scale(struct rdata *rd)
{
   rd->mean_lat_len = rd->scale * ((double) rd->w / (double) rd->dpi) * 2.54 / (60.0 * 1852 * 100);
}


#if 0
void init_prj(struct rdata *rd, int p)
{
   double y1, y2;

   rd->mean_lat = (rd->y1c + rd->y2c) / 2;
   switch (p)
   {
      case PRJ_MERC_PAGE:
         rd->wc = rd->x2c - rd->x1c;
         rd->mean_lat_len = rd->wc * cos(rd->mean_lat * M_PI / 180);
         rd->hc = rd->mean_lat_len * rd->h / rd->w;
         y1 = rd->mean_lat + rd->hc / 2.0;
         y2 = rd->mean_lat - rd->hc / 2.0;
         if ((y1 > rd->y1c) || (y2 < rd->y2c))
            log_warn("window enlarged in latitude! This may result in incorrect rendering.");
         rd->y1c = y1;
         rd->y2c = y2;
         break;

      case PRJ_MERC_BB:
         log_msg(LOG_ALERT, "projection PRJ_MERC_BB not implemented yet"), exit(EXIT_FAILURE);
         break;

      default:
         rd->wc = rd->x2c - rd->x1c;
         rd->mean_lat_len = rd->wc * cos(rd->mean_lat * M_PI / 180);
         rd->hc = rd->y1c - rd->y2c;
   }
   rd->scale = (rd->mean_lat_len * 60.0 * 1852 * 100 / 2.54) / ((double) rd->w / (double) rd->dpi);

}
#endif


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
            nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);

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
   ds->min_nid = ds->min_wid = (int64_t) 0x7fffffffffffffff;
   ds->max_nid = ds->max_wid = (int64_t) 0x8000000000000000;
   ds->lu.lat = -90;
   ds->rb.lat = 90;
   ds->lu.lon = 180;
   ds->rb.lon = -180;
}

 
int onode_stats(struct onode *nd, struct rdata *rd, struct dstats *ds)
{
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


#if 0
void swap_data(void *a, void *b, int n)
{
   char c[n];

   memcpy(c, a, n);
   memcpy(a, b, n);
   memcpy(b, c, n);
}
#endif



struct rdata *init_rdata(void)
{
   memset(&rd_, 0, sizeof(rd_));
   rd_.dpi = 300;

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


   // A3 paper portrait (300dpi)
   //rd->w = 3507; rd->h = 4961; rd->dpi = 300;
   // A4 paper portrait (300dpi)
   //rd->w = 2480; rd->h = 3507; rd->dpi = 300;
   // A4 paper landscape (300dpi)
   //rd->h = 2480; rd->w = 3507; rd->dpi = 300;
   // A4 paper portrait (600dpi)
   //rd->w = 4961; rd->h = 7016; rd->dpi = 600;
   // A2 paper landscape (300dpi)
   //rd->w = 7016; rd->h = 4961; rd->dpi = 300;
   // A1 paper landscape (300dpi)
   //rd->w = 9933; rd->h = 7016; rd->dpi = 300;
   // A1 paper portrait (300dpi)
   //rd->h = 9933; rd->w = 7016; rd->dpi = 300;

   rd->grd.lat_ticks = rd->grd.lon_ticks = G_TICKS;
   rd->grd.lat_sticks = rd->grd.lon_sticks = G_STICKS;
   rd->grd.lat_g = rd->grd.lon_g = G_GRID;

   // init callback function pointers
   //rd->cb.log_msg = log_msg;
   //rd->cb.get_object = get_object;
   //rd->cb.put_object = put_object;
   //rd->cb.malloc_object = malloc_object;
   //rd->cb.match_attr = match_attr;

   // this should be given by CLI arguments
   /* porec.osm
   rd->x1c = 13.53;
   rd->y1c = 45.28;
   rd->x2c = 13.63;
   rd->y2c = 45.183; */

   //dugi.osm
   //rd->x1c = 14.72;
   //rd->y1c = 44.23;
   //rd->x2c = 15.29;
   //rd->y2c = 43.96;

   //croatia...osm
   //rd->x1c = 13.9;
   //rd->y1c = 45.75;
   //rd->x2c = 15.4;
   //rd->y2c = 43.0;

   //croatia_big...osm
   //rd->x1c = 13.5;
   //rd->y1c = 45.5;
   //rd->x2c = 15.5;
   //rd->y2c = 43.5;

   /* treasure_island
   rd->x1c = 24.33;
   rd->y1c = 37.51;
   rd->x2c = 24.98;
   rd->y2c = 37.16;
   */
}


void init_rd_image(struct rdata *rd)
{
}


void usage(const char *s)
{
   printf("Seamark renderer V1.1, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n"
         "usage: %s [OPTIONS]\n"
         "   -G .................. Do not generate grid nodes/ways.\n"
         "   -C .................. Do not close open coastline polygons.\n"
         "   -d <density> ........ Set image density (300 is default).\n"
         "   -i <osm input> ...... OSM input data (defaulta is stdin).\n"
         "   -l .................. Select landscape output.\n"
         "   -m <length> ......... Length of mean latitude in degrees.\n"
         "   -r <rules file> ..... Rules file ('rules.osm' is default).\n"
         "   -s <scale> .......... Select scale of chart.\n"
         "   -o <image file> ..... Filename of output image (stdout is default).\n"
         "   -P <page format> .... Select output page format.\n"
         "   -w <osm file> ....... Output OSM data to file.\n"
         "   -x <longitude> ...... Longitude of center point.\n"
         "   -y <latitude> ....... Latitude if center point.\n",
         s
         );
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
   int gen_grid = 1, prep_coast = 1, landscape = 0;
   char *paper = "A3";

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);
   log_msg(LOG_INFO, "initializing structures");
   rd = init_rdata();
   set_util_rd(rd);

   while ((n = getopt(argc, argv, "Cd:Ghi:lm:o:P:r:s:w:x:y:")) != -1)
      switch (n)
      {
         case 'C':
            prep_coast = 0;
            break;

         case 'd':
            if ((rd->dpi = atoi(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal dpi argument %s", optarg),
                  exit(EXIT_FAILURE);
            break;

         case 'G':
            gen_grid = 0;
            break;

         case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);

         case 'i':
            osm_ifile = optarg;
            break;

         case 'm':
            if ((rd->mean_lat_len = atoi(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal argument for mean lat length %s", optarg),
                  exit(EXIT_FAILURE);
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
            if ((rd->scale = atof(optarg)) <= 0)
               log_msg(LOG_ERR, "illegal scale argument %s", optarg),
                  exit(EXIT_FAILURE);
            break;

         case 'w':
            osm_ofile = optarg;
            break;

         case 'x':
            rd->mean_lon = atof(optarg);
            break;

         case 'y':
            rd->mean_lat = atof(optarg);
            break;
      }

   if ((rd->scale != 0) && (rd->mean_lat_len != 0))
      log_msg(LOG_ERR, "specifying scale AND mean latitude length is not allowed"),
         exit(EXIT_FAILURE);
   if ((rd->scale == 0) && (rd->mean_lat_len == 0))
      log_msg(LOG_ERR, "either -s or -m is mandatory"),
         exit(EXIT_FAILURE);

   init_rd_paper(rd, paper, landscape);
   if (rd->mean_lat_len == 0)
      init_bbox_scale(rd);
   init_bbox_mll(rd);

   //init_prj(rd, PRJ_MERC_PAGE);
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

   if ((osm_ifile != NULL) && ((fd = open(osm_ifile, O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading osm data (file size %ld kb)", (long) st.st_size / 1024);
   (void) read_osm_file(ctl, &rd->obj);
   if (osm_ifile != NULL)
      (void) close(fd);

   if ((fd = open(cf, O_RDONLY)) == -1)
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((cfctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading rules (file size %ld kb)", (long) st.st_size / 1024);
   (void) read_osm_file(cfctl, &rd->rules);
   (void) close(fd);

//#ifdef MEM_USAGE
   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);
//#endif

   log_msg(LOG_INFO, "gathering stats");
   init_stats(&rd->ds);
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) onode_stats, rd, &rd->ds);
   traverse(rd->obj, 0, IDX_NODE, (tree_func_t) onode_stats, rd, &rd->ds);
   log_msg(LOG_INFO, "ncnt = %ld, min_nid = %ld, max_nid = %ld",
         rd->ds.ncnt, rd->ds.min_nid, rd->ds.max_nid);
   log_msg(LOG_INFO, "wcnt = %ld, min_wid = %ld, max_wid = %ld",
         rd->ds.wcnt, rd->ds.min_wid, rd->ds.max_wid);
   log_msg(LOG_INFO, "left upper %.2f/%.2f, right bottom %.2f/%.2f",
         rd->ds.lu.lat, rd->ds.lu.lon, rd->ds.rb.lat, rd->ds.rb.lon);

   log_msg(LOG_INFO, "preparing rules");
   traverse(rd->rules, 0, IDX_NODE, prepare_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_WAY, prepare_rules, rd, NULL);

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

   log_msg(LOG_INFO, "rendering ways");
   traverse(rd->rules, 0, IDX_WAY, apply_wrules, rd, NULL);
   log_msg(LOG_INFO, "rendering nodes");
   traverse(rd->rules, 0, IDX_NODE, apply_rules, rd, NULL);

   save_osm(rd, osm_ofile);
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

