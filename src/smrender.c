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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>
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

#include "smrender_dev.h"


#define COORD_LAT 0
#define COORD_LON 1

#define ISNORTH(x) (strchr("Nn", (x)) != NULL)
#define ISSOUTH(x) (strchr("Ss", (x)) != NULL)
#define ISEAST(x) (strchr("EeOo", (x)) != NULL)
#define ISWEST(x) (strchr("Ww", (x)) != NULL)
#define ISLAT(x) (ISNORTH(x) || ISSOUTH(x))
#define ISLON(x) (ISEAST(x) || ISWEST(x))


static volatile sig_atomic_t int_ = 0;


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

   for (; isspace(*s); s++);
   if (*s == '-')
   {
      s++;
      n = -1.0;
   }
   for (*a = 0.0; isdigit(*s); s++)
   {
      *a *= 10.0;
      *a += *s - '0';
   }

   for (; isspace(*s); s++);
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
      for (e = 1.0, f = 0.0; isdigit(*s); e *= 10.0, s++)
      {
         f *= 10.0;
         f += *s - '0';
      }
      *a += f / e;
      *a *= n;

      for (; isspace(*s); s++);
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
   for (; isspace(*s); s++);
   f = atof(s);
   *a += f / 60.0;
   *a *= n;

   return r;
}


void int_handler(int sig)
{
   int_++;
}


void install_sigint(void)
{
   struct sigaction sa;

   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = int_handler;
   sa.sa_flags = SA_RESETHAND;

   if (sigaction(SIGINT, &sa, NULL) == -1)
      log_msg(LOG_WARNING, "SIGINT handler cannot be installed: %s", strerror(errno));
   else
      log_msg(LOG_INFO, "SIGINT installed (pid = %ld)", (long) getpid());
}


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_smrules0(osm_obj_t *o, struct rdata *rd, smrule_t *r)
{
   int i;

   for (i = 0; i < r->oo->tag_cnt; i++)
      if (bs_match_attr(o, &r->oo->otag[i], &r->act->stag[i]) == -1)
         return 0;

   return sm_exec_rule(r, o, r->act->main.func);
}


int apply_smrules(smrule_t *r, struct rdata *rd, osm_obj_t *o)
{
   int e = 0;

   if (r == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer to rule, ignoring");
      return 1;
   }

   if (o != NULL && r->oo->ver != o->ver)
      return 0;

   if (r->act->func_name == NULL)
      return 0;

   log_debug("applying rule id 0x%016lx '%s'", (long) r->oo->id, r->act->func_name);

   if (r->act->main.func != NULL)
      e = traverse(rd->obj, 0, r->oo->type - 1, (tree_func_t) apply_smrules0, rd, r);
   else
      log_debug("   -> no main function");

   if (e) log_debug("traverse(apply_smrules0) returned %d", e);

#ifdef WITH_THREADS
   if (r->act->flags & ACTION_THREADED)
      sm_wait_threads();
#endif

   // call de-initialization rule of function rule if available
   if (e >= 0 && r->act->fini.func != NULL)
   {
      e = r->act->fini.func(r);
      if (e) log_debug("_fini returned %d", e);
   }

   return e;
}


int norm_rule_node(osm_obj_t *o, struct rdata *rd, void *p)
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


int norm_rule_way(osm_obj_t *o, struct rdata *rd, void *p)
{
   static double lat;
   osm_node_t *n;

   if (((osm_way_t*) o)->ref_cnt > 0)
      return 0;

   lat += RULE_LAT_DIFF;

   n = malloc_node(0);
   n->obj.id = --((struct dstats*) p)->min_nid;
   n->obj.ver = 1;
   n->lat = lat;
   n->lon = 0;
   put_object0(&rd->rules, n->obj.id, n, IDX_NODE);
   n = malloc_node(0);
   n->obj.id = --((struct dstats*) p)->min_nid;
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
   char buf[32];

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
            if ((e = dhandler(nt->next[i], rd, p)))
            {
               (void) func_name(buf, sizeof(buf), dhandler);
               log_msg(LOG_WARNING, "dhandler(), sym = '%s', addr = '%p' returned %d", buf, dhandler, e);
               if (e < 0)
               {
                  log_msg(LOG_INFO, "breaking recursion");
                  return e;
               }
            }
         }
      }
      return e;
   }

   for (i = 0; i < 1 << BX_RES; i++)
      if (nt->next[i])
      {
         if ((e = traverse(nt->next[i], d + 1, idx, dhandler, rd, p)) < 0)
            return e;
         /*
         if (e < 0)
         {
            log_msg(LOG_WARNING, "traverse() returned %d, breaking recursion.", e);
            return e;
         }
         else if (e > 0)
            log_msg(LOG_INFO, "traverse() returned %d", e);
            */
      }

   return 0;
}


void print_url(struct bbox bb)
{
   char *url[] = {
      "http://www.overpass-api.de/api/xapi?map?",
      "http://overpass.osm.rambler.ru/cgi/xapi?map?",
      "http://jxapi.openstreetmap.org/xapi/api/0.6/map?",
      "http://open.mapquestapi.com/xapi/api/0.6/map?",
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
      printf("%sbbox=%.3f,%.3f,%.3f,%.3f\n", url[i], bb.ll.lon, bb.ll.lat, bb.ru.lon, bb.ru.lat);
}


void print_rdata(const struct rdata *rd)
{
   log_msg(LOG_NOTICE, "*** chart parameters for rendering ****");
   log_msg(LOG_NOTICE, "   %.3f %.3f -- %.3f %.3f",
         rd->bb.ru.lat, rd->bb.ll.lon, rd->bb.ru.lat, rd->bb.ru.lon);
   log_msg(LOG_NOTICE, "   %.3f %.3f -- %.3f %.3f",
         rd->bb.ll.lat, rd->bb.ll.lon, rd->bb.ll.lat, rd->bb.ru.lon);
   log_msg(LOG_NOTICE, "   wc = %.3f°, hc = %.3f°", rd->wc, rd->hc);
   log_msg(LOG_NOTICE, "   mean_lat = %.3f°, mean_lat_len = %.3f (%.1f nm)",
         rd->mean_lat, rd->mean_lat_len, rd->mean_lat_len * 60);
   log_msg(LOG_NOTICE, "   lath = %f, lath_len = %f", rd->lath, rd->lath_len);
   log_msg(LOG_NOTICE, "   page size = %.1f x %.1f mm, oversampling = %d",
         PX2MM(rd->w), PX2MM(rd->h), rd->ovs);
   log_msg(LOG_NOTICE, "   rendering: %dx%d px, dpi = %d",
         rd->w, rd->h, rd->dpi);
   log_msg(LOG_NOTICE, "   final: %dx%d px, dpi = %d",
         rd->fw, rd->fh, rd->ovs ? rd->dpi / rd->ovs : rd->dpi);
   log_msg(LOG_NOTICE, "   1 px = %.3f mm, 1mm = %d px", PX2MM(1), (int) MM2PX(1));
   log_msg(LOG_NOTICE, "   scale 1:%.0f, %.1f x %.1f nm",
         rd->scale, rd->wc * 60 * cos(DEG2RAD(rd->mean_lat)), rd->hc * 60);

   log_debug("   G_GRID %.3f, G_TICKS %.3f, G_STICKS %.3f, G_MARGIN %.2f, G_TW %.2f, G_STW %.2f, G_BW %.2f",
         G_GRID, G_TICKS, G_STICKS, G_MARGIN, G_TW, G_STW, G_BW);
   log_msg(LOG_NOTICE, "***");
}


void init_bbox_mll(struct rdata *rd)
{
   rd->wc = rd->mean_lat_len / cos(rd->mean_lat * M_PI / 180);
   rd->bb.ll.lon = rd->mean_lon - rd->wc / 2;
   rd->bb.ru.lon = rd->mean_lon + rd->wc / 2;
   rd->hc = rd->mean_lat_len * rd->h / rd->w;
   rd->bb.ru.lat = rd->mean_lat + rd->hc / 2.0;
   rd->bb.ll.lat = rd->mean_lat - rd->hc / 2.0;
   rd->scale = (rd->mean_lat_len * 60.0 * 1852 * 100 / 2.54) / ((double) rd->w / (double) rd->dpi);
   rd->lath = asinh(tan(DEG2RAD(rd->mean_lat)));
   rd->lath_len = asinh(tan(DEG2RAD(rd->bb.ru.lat))) - asinh(tan(DEG2RAD(rd->bb.ll.lat)));
}


int bs_safe_put_xml(FILE *f, const bstring_t *b)
{
   int i, c;

   for (i = 0, c = 0; i < b->len; i++)
   {
      if (b->buf[i] == '"')
      {
         c += fputs("&quot;", f);
         continue;
      }
      c += fputc(b->buf[i], f);
   }
   return c;
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

   // FIXME: 'visible' missing?
   switch (o->type)
   {
      case OSM_NODE:
         fprintf(f, "<node id=\"%ld\" version=\"%d\" lat=\"%.7f\" lon=\"%.7f\" timestamp=\"%s\" uid=\"%d\">\n",
               (long) o->id, o->ver, ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon, ts, o->uid);
         break;

      case OSM_WAY:
         fprintf(f, "<way id=\"%ld\" version=\"%d\" timestamp=\"%s\" uid=\"%d\">\n",
               (long) o->id, o->ver, ts, o->uid);
         break;

      case OSM_REL:
         fprintf(f, "<relation id=\"%ld\" version=\"%d\" timestamp=\"%s\" uid=\"%d\">\n",
               (long) o->id, o->ver, ts, o->uid);
         break;

      default:
         fprintf(f, "<!-- unknown node type: %d -->\n", o->type);
         return -1;
   }

   for (i = 0; i < o->tag_cnt; i++)
   {
      fputs("<tag k=\"", f);
      bs_safe_put_xml(f, &o->otag[i].k);
      fputs("\" v=\"", f);
      bs_safe_put_xml(f, &o->otag[i].v);
      fputs("\"/>\n", f);
      /*fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            (int) o->otag[i].k.len, o->otag[i].k.buf, (int) o->otag[i].v.len, o->otag[i].v.buf);*/
   }

  switch (o->type)
   {
      case OSM_NODE:
         fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            fprintf(f, "<nd ref=\"%ld\"/>\n", (long) ((osm_way_t*) o)->ref[i]);
         fprintf(f, "</way>\n");
         break;

      case OSM_REL:
         for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
            fprintf(f, "<member type=\"%s\" ref=\"%ld\" role=\"\"/>\n",
                  ((osm_rel_t*) o)->mem[i].type == OSM_NODE ? "node" : "way", (long) ((osm_rel_t*) o)->mem[i].id);
         fprintf(f, "</relation>\n");
         break;
   }

   return 0;
}


int free_rules(smrule_t *r, struct rdata *rd, void *p)
{
   free_obj(r->oo);
   free_fparam(r->act->fp);
   // action must not be freed because it is part of the rule (see alloc_rule())
   free(r);
   return 0;
}


int free_objects(osm_obj_t *o, struct rdata *rd, void *p)
{
   free_obj(o);
   return 0;
}


/*! Save OSM data of tree to file s.
 *  @param s Filename of output file.
 *  @param Pointer to bxtree containing the information.
 *  @param bb Optional bounding box (written to tag <bounds>).
 *  @param info Optional information written to the header as comment (<!-- info -->).
 *  @return The function returns 0, or -1 in case of error.
 */
int save_osm(const char *s, bx_node_t *tree, const struct bbox *bb, const char *info)
{
   FILE *f;

   if (s == NULL)
      return -1;

   log_msg(LOG_INFO, "saving osm output to '%s'", s);
   if ((f = fopen(s, "w")) != NULL)
   {
      fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n"
                 "<osm version='0.6' generator='smrender'>\n");
      if (info != NULL)
         fprintf(f, "<!--\n%s\n-->\n", info);
      if (bb != NULL)
         fprintf(f, "<bounds minlat='%f' minlon='%f' maxlat='%f' maxlon='%f'/>\n",
               bb->ll.lat, bb->ll.lon, bb->ru.lat, bb->ru.lon);
      traverse(tree, 0, IDX_NODE, print_tree, NULL, f);
      traverse(tree, 0, IDX_WAY, print_tree, NULL, f);
      traverse(tree, 0, IDX_REL, print_tree, NULL, f);
      fprintf(f, "</osm>\n");
      fclose(f);
   }
   else
      log_msg(LOG_WARN, "could not open '%s': %s", s, strerror(errno));

   return 0;
}


static struct rdata rd_;


static void __attribute__((constructor)) init_rdata(void)
{
   log_debug("initializing struct rdata");
   memset(&rd_, 0, sizeof(rd_));
   rd_.dpi = 300;
   rd_.ovs = DEFAULT_OVS;
   rd_.title = "";
   set_util_rd(&rd_);
}


struct rdata inline *get_rdata(void)
{
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


void usage(const char *s)
{
   printf("Seamark renderer V" PACKAGE_VERSION ", (c) 2011-2012, Bernhard R. Fischer, <bf@abenteuerland.at>.\n"
         "usage: %s [OPTIONS] <window>\n"
         "   <window> := <lat>:<lon>:<size>\n"
         "               <lat> and <lon> specify the coordinates of the centerpoint.\n"
         "   <size>   := <scale> | <length>'d' | <length>'m'\n"
         "               <scale> Scale of chart.\n"
         "               <length> Length of mean latitude in either degrees ('d') or\n"
         "                        nautical miles ('m')\n"
         "   -b <color> .......... Choose background color ('white' is default).\n"
         "   -d <density> ........ Set image density (300 is default).\n"
         "   -f .................. Use loading filter.\n"
         "   -g <grd>[:<t>[:<s>]]  Distance of grid/ticks/subticks in minutes.\n"
         "   -G .................. Do not generate grid nodes/ways.\n"
         "   -i <osm input> ...... OSM input data (default is stdin).\n"
         "   -k <filename> ....... Generate KAP file.\n"
         "   -K <filename> ....... Generate KAP header file.\n"
         "   -l .................. Select landscape output.\n"
         "   -M .................. Input file is memory mapped (default).\n"
         "   -m .................. Input file is read into heap memory.\n"
         "   -r <rules file> ..... Rules file ('rules.osm' is default).\n"
         "   -s <ovs> ............ Set oversampling factor (0-10) (default = %d).\n"
         "   -t <title> .......... Set descriptional chart title.\n"
         "   -o <image file> ..... Filename of output image (stdout is default).\n"
         "   -P <page format> .... Select output page format.\n"
         "   -u .................. Output URLs suitable for OSM data download and\n"
         "                         exit.\n"
         "   -V .................. Show chart parameters and exit.\n"
         "   -w <osm file> ....... Output OSM data to file.\n",
         s, DEFAULT_OVS
         );
   printf("\nSee http://www.abenteuerland.at/smrender/ for more information.\n");
}


int cmp_int(const int *a, const int *b)
{
   if (*a < *b)
      return -1;
   if (*a > *b)
      return 1;
   return 0;
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


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0, n;
   struct stat st;
   FILE *f = stdout;
   char *cf = "rules.osm", *img_file = NULL, *osm_ifile = NULL, *osm_ofile =
      NULL, *osm_rfile = NULL, *kap_file = NULL, *kap_hfile = NULL;
   struct rdata *rd;
   struct timeval tv_start, tv_end;
   int landscape = 0, w_mmap = 1, load_filter = 0, init_exit = 0, gen_grid = AUTO_GRID, prt_url = 0;
   char *paper = "A3", *bg = NULL;
   struct filter fi;
   struct dstats rstats;
   struct grid grd;
   osm_obj_t o;
   char *s;
   double param;

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);
   rd = get_rdata();
   init_grid(&grd);
   rd->cmdline = mk_cmd_line((const char**) argv);

   while ((n = getopt(argc, argv, "b:d:fg:Ghi:k:K:lMmo:P:r:R:s:t:uVw:")) != -1)
      switch (n)
      {
         case 'b':
            bg = optarg;
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

         case 'M':
#ifndef WITH_MMAP
            log_msg(LOG_ERR, "memory mapping support disabled, recompile with WITH_MMAP");
            exit(EXIT_FAILURE);
#endif
            w_mmap = 1;
            break;

         case 'm':
            w_mmap = 0;
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
            rd->ovs = atoi(optarg);
            if (rd->ovs < 0)
               rd->ovs = 0;
            if (rd->ovs > 10)
               rd->ovs = 10;
            break;

         case 'R':
            osm_rfile = optarg;
            break;

         case 't':
            rd->title = optarg;
            break;

         case 'u':
            prt_url = 1;
            break;

         case 'V':
            init_exit = 1;
            break;

         case 'w':
            osm_ofile = optarg;
            break;
      }

   if (argv[optind] == NULL)
   {
      log_msg(LOG_WARN, "window parameter missing, setting defaults 0:0:100000");
      rd->scale = 100000;
   }
   else
   {
      if ((s = strtok(argv[optind], ":")) == NULL)
         log_msg(LOG_ERR, "latitude paramter missing"), exit(EXIT_FAILURE);

      n = parse_coord(s, &param);
      if (n == COORD_LON)
         rd->mean_lon = param;
      else
         rd->mean_lat = param;

      if ((s = strtok(NULL, ":")) == NULL)
         log_msg(LOG_ERR, "longitude paramter missing"), exit(EXIT_FAILURE);

      n = parse_coord(s, &param);
      if (n == COORD_LAT)
         rd->mean_lat = param;
      else
         rd->mean_lon = param;

      if ((s = strtok(NULL, ":")) == NULL)
         log_msg(LOG_ERR, "size parameter missing"), exit(EXIT_FAILURE);

      if ((param = atof(s)) <= 0)
         log_msg(LOG_ERR, "illegal size argument for"), exit(EXIT_FAILURE);
 
      if (isdigit((unsigned) s[strlen(s) - 1]) || (s[strlen(s) - 1] == '.'))
         rd->scale = param;
      else if (s[strlen(s) - 1] == 'm')
         rd->mean_lat_len = param / 60;
      else if (s[strlen(s) - 1] == 'd')
         rd->wc = param;
      else
         log_msg(LOG_ERR, "illegal size parameter"), exit(EXIT_FAILURE);
   }

   // install exit handlers
   osm_read_exit();
   bx_exit();

   if (rd->ovs)
      rd->dpi *= rd->ovs;

   init_rd_paper(rd, paper, landscape);
   if (rd->ovs > 1)
   {
      rd->fw = rd->w / rd->ovs;
      rd->fh = rd->h / rd->ovs;
   }
   else
   {
      rd->fw = rd->w;
      rd->fh = rd->h;
   }

   if (rd->scale > 0)
      rd->mean_lat_len = rd->scale * ((double) rd->w / (double) rd->dpi) * 2.54 / (60.0 * 1852 * 100);
   else if (rd->wc > 0)
      rd->mean_lat_len  = rd->wc * cos(rd->mean_lat * M_PI / 180);

   init_bbox_mll(rd);

   if (prt_url)
   {
      print_url(rd->bb);
      exit(EXIT_SUCCESS);
   }

   print_rdata(rd);

   if (init_exit)
      exit(EXIT_SUCCESS);

   // preparing image
   init_main_image(rd, bg);

   if ((cfctl = open_osm_source(cf, 0)) == NULL)
      exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading rules (file size %ld kb)", (long) cfctl->len / 1024);
   (void) read_osm_file(cfctl, &rd->rules, NULL, &rstats);
   (void) close(cfctl->fd);

   if (!rstats.ncnt && !rstats.wcnt && !rstats.rcnt)
   {
      log_msg(LOG_ERR, "no rules found");
      exit(EXIT_NORULES);
   }

   qsort(rstats.ver, rstats.ver_cnt, sizeof(int), (int(*)(const void*, const void*)) cmp_int);
   for (n = 0; n < rstats.ver_cnt; n++)
      log_msg(LOG_DEBUG, " rstats.ver[%d] = %d", n, rstats.ver[n]);

   if (osm_rfile != NULL)
   {
      traverse(rd->rules, 0, IDX_NODE, norm_rule_node, rd, NULL);
      traverse(rd->rules, 0, IDX_WAY, norm_rule_way, rd, &rstats);
      // FIXME: saving relation rules missing
      save_osm(osm_rfile, rd->rules, NULL, NULL);
   }

   log_msg(LOG_INFO, "preparing node rules");
   if (traverse(rd->rules, 0, IDX_NODE, (tree_func_t) init_rules, rd, NULL) < 0)
      log_msg(LOG_ERR, "rule parser failed"),
         exit(EXIT_FAILURE);
   log_msg(LOG_INFO, "preparing way rules");
   if (traverse(rd->rules, 0, IDX_WAY, (tree_func_t) init_rules, rd, NULL) < 0)
      log_msg(LOG_ERR, "rule parser failed"),
         exit(EXIT_FAILURE);
   log_msg(LOG_INFO, "preparing relation rules");
   if (traverse(rd->rules, 0, IDX_REL, (tree_func_t) init_rules, rd, NULL) < 0)
      log_msg(LOG_ERR, "rule parser failed"),
         exit(EXIT_FAILURE);

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
   else
   {
      // FIXME
      log_msg(LOG_CRIT, "***** Smrender currently does not work without mmap(). Sorry guys, this is a bug and will be fixed. *****");
      exit(EXIT_FAILURE);
   }
   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading osm data (file size %ld kb, memory at %p)",
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
      (void) read_osm_file(ctl, &rd->obj, &fi, &rd->ds);
   }
   else
   {
      (void) read_osm_file(ctl, &rd->obj, NULL, &rd->ds);
   }

   if (!rd->ds.ncnt)
   {
      log_msg(LOG_ERR, "no data to render");
      exit(EXIT_NODATA);
   }

   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);

   log_msg(LOG_INFO, "stripping filtered way nodes");
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) strip_ways, rd, NULL);

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

   memset(&o, 0, sizeof(o));
   for (n = 0; (n < rstats.ver_cnt) && !int_; n++)
   {
      log_msg(LOG_INFO, "rendering pass %d (ver = %d)", n, rstats.ver[n]);
      o.ver = rstats.ver[n];

      // FIXME: order rel -> way -> node?
      log_msg(LOG_INFO, " relations...");
      traverse(rd->rules, 0, IDX_REL, (tree_func_t) apply_smrules, rd, &o);
      log_msg(LOG_INFO, " ways...");
      traverse(rd->rules, 0, IDX_WAY, (tree_func_t) apply_smrules, rd, &o);
      log_msg(LOG_INFO, " nodes...");
      traverse(rd->rules, 0, IDX_NODE, (tree_func_t) apply_smrules, rd, &o);
   }
   int_ = 0;

   save_osm(osm_ofile, rd->obj, &rd->bb, rd->cmdline);
   (void) close(ctl->fd);
   hpx_free(ctl);
   hpx_free(cfctl);

   log_debug("freeing main objects");
   traverse(rd->obj, 0, IDX_REL, free_objects, rd, NULL);
   traverse(rd->obj, 0, IDX_WAY, free_objects, rd, NULL);
   traverse(rd->obj, 0, IDX_NODE, free_objects, rd, NULL);

   log_debug("freeing rule objects");
   traverse(rd->rules, 0, IDX_REL, (tree_func_t) free_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_WAY, (tree_func_t) free_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_NODE, (tree_func_t) free_rules, rd, NULL);

   log_debug("freeing main object tree");
   bx_free_tree(rd->obj);
   log_debug("freeing rules tree");
   bx_free_tree(rd->rules);

   if (rd->ovs > 1)
      reduce_resolution(rd);

   if (img_file != NULL)
   {
      if ((f = fopen(img_file, "w")) == NULL)
         log_msg(LOG_ERR, "error opening file %s: %s", img_file, strerror(errno));
      else
      {
         save_main_image(rd, f);
         fclose(f);
      }
   }

   if (kap_file != NULL)
   {
      log_msg(LOG_INFO, "generating KAP file %s", kap_file);
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
      log_msg(LOG_INFO, "generating KAP header file %s", kap_hfile);
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

   log_msg(LOG_INFO, "%d.%03d seconds elapsed. exiting", (unsigned) tv_end.tv_sec, (unsigned) tv_end.tv_usec / 1000);
   log_msg(LOG_INFO, "Thanks for using smrender!");
   return EXIT_SUCCESS;
}

