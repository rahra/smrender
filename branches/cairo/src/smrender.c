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

static volatile sig_atomic_t int_ = 0;
static int render_all_nodes_ = 0;


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


/*! Match and apply ruleset to object if it is visible.
 *  @param o Object which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param r Rule object.
 */
int apply_smrules0(osm_obj_t *o, struct rdata *rd, smrule_t *r)
{
   int i;

   // render only nodes which are on the page
   if (!render_all_nodes_ && o->type == OSM_NODE)
   {
      struct coord c;
      c.lon = ((osm_node_t*) o)->lon;
      c.lat = ((osm_node_t*) o)->lat;
      if (!is_on_page(&c))
         return 0;
   }

   for (i = 0; i < r->oo->tag_cnt; i++)
      if (bs_match_attr(o, &r->oo->otag[i], &r->act->stag[i]) == -1)
         return 0;

   if (o->vis)
      return r->act->main.func(r, o);

   return 0;
}


int call_fini(smrule_t *r)
{
   int e = 0;

   // call de-initialization rule of function rule if available
   if (r->act->fini.func != NULL && !r->act->finished)
   {
      log_msg(LOG_INFO, "calling rule %016lx, %s_fini", (long) r->oo->id, r->act->func_name);
      if ((e = r->act->fini.func(r)))
         log_debug("_fini returned %d", e);
      r->act->finished = 1;
   }

   return e;
}


#ifdef WITH_THREADS

static list_t *li_fini_;


static void __attribute__((constructor)) init_fini_list(void)
{
   if ((li_fini_ = li_new()) == NULL)
      perror("li_new()"), exit(EXIT_FAILURE);
}


static void __attribute__((destructor)) del_fini_list(void)
{
   li_destroy(li_fini_, NULL);
}


int queue_fini(smrule_t *r)
{
   if ((li_add(li_fini_, r)) == NULL)
   {
      log_msg(LOG_ERR, "li_add() failed: %s", strerror(errno));
      return -1;
   }
   return 0;
}


int dequeue_fini(void)
{
   list_t *elem, *prev;

   log_msg(LOG_INFO, "calling pending _finis");
   for (elem = li_last(li_fini_); elem != li_head(li_fini_); elem = prev)
   {
      li_unlink(elem);
      call_fini(elem->data);
      prev = elem->prev;
      li_del(elem, NULL);
   }

   return 0;
}


#endif


int apply_smrules(smrule_t *r, struct rdata *rd, osm_obj_t *o)
{
   int e = 0;

   if (r == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer to rule, ignoring");
      return 1;
   }

   if (!r->oo->vis)
   {
      log_msg(LOG_INFO, "ignoring invisible rule %016lx", (long) r->oo->id);
      return 0;
   }

   if (o != NULL && r->oo->ver != o->ver)
      return 0;

   // FIXME: wtf is this?
   if (r->act->func_name == NULL)
   {
      log_debug("function has no name");
      return 0;
   }

#ifdef WITH_THREADS
   // if rule is not threaded
   if (!sm_is_threaded(r))
   {
      // wait for all threads (previous rules) to finish
      sm_wait_threads();
      // call finalization functions in the appropriate order
      dequeue_fini();
   }
#endif

   log_debug("applying rule id 0x%016lx '%s'", (long) r->oo->id, r->act->func_name);

   if (r->act->main.func != NULL)
   {
#ifdef WITH_THREADS
      if (sm_is_threaded(r))
         e = traverse_queue(*get_objtree(), r->oo->type - 1, (tree_func_t) apply_smrules0, r);
      else
#endif
         e = traverse(*get_objtree(), 0, r->oo->type - 1, (tree_func_t) apply_smrules0, rd, r);
   }
   else
      log_debug("   -> no main function");

   if (e) log_debug("traverse(apply_smrules0) returned %d", e);

   if (e >= 0)
#ifdef WITH_THREADS
      queue_fini(r);
#else
      call_fini(r);
#endif

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
      switch (b->buf[i])
      {
         case '"':
            c += fputs("&quot;", f);
            break;
         case '<':
            c += fputs("&lt;", f);
            break;
         default:
            c += fputc(b->buf[i], f);
      }
   return c;
}


static int fprint_defattr(FILE *f, const osm_obj_t *o, const char *ostr)
{
#define TBUFLEN 24
   char ts[TBUFLEN] = "0000-00-00T00:00:00Z";
   struct tm *tm;

   if ((tm = gmtime(&o->tim)) != NULL)
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

   return fprintf(f, "<%s id=\"%ld\" version=\"%d\" timestamp=\"%s\" uid=\"%d\" visible=\"%s\"",
         ostr, (long) o->id, o->ver, ts, o->uid, o->vis ? "true" : "false");
}


int print_onode(FILE *f, const osm_obj_t *o)
{
   int i;

   if (o == NULL)
   {
      log_warn("NULL pointer catched in print_onode()");
      return -1;
   }

   switch (o->type)
   {
      case OSM_NODE:
         fprint_defattr(f, o, "node");
         if (o->tag_cnt)
            fprintf(f, " lat=\"%.7f\" lon=\"%.7f\">\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         else
            fprintf(f, " lat=\"%.7f\" lon=\"%.7f\"/>\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         fprint_defattr(f, o, "way");
         fprintf(f, ">\n");
         break;

      case OSM_REL:
         fprint_defattr(f, o, "relation");
         fprintf(f, ">\n");
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
         if (o->tag_cnt)
            fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            fprintf(f, "<nd ref=\"%ld\"/>\n", (long) ((osm_way_t*) o)->ref[i]);
         fprintf(f, "</way>\n");
         break;

      case OSM_REL:
         for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
            fprintf(f, "<member type=\"%s\" ref=\"%ld\" role=\"%s\"/>\n",
                  ((osm_rel_t*) o)->mem[i].type == OSM_NODE ? "node" : "way", (long) ((osm_rel_t*) o)->mem[i].id,
                  role_str(((osm_rel_t*) o)->mem[i].role));
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
      
      if ((rd->w < 0) || (rd->h < 0))
         log_msg(LOG_ERR, "page width and height must be a decimal value greater than 0"),
            exit(EXIT_FAILURE);

      if (!rd->w && !rd->h)
         log_msg(LOG_ERR, "width and height cannot both be 0"),
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
         "   -s <ovs> ............ Deprecated, kept for backwards compatibility.\n"
         "   -t <title> .......... Set descriptional chart title.\n"
         "   -T <tile_info> ...... Create tiles.\n"
         "      <tile_info> := <zoom_lo> [ '-' <zoom_hi> ] ':' <tile_path> [ ':' <file_type> ]\n"
         "      <file_type> := 'png' | 'jpg'\n"
         "   -o <image file> ..... Filename of output PNG image.\n"
         "   -O <pdf file> ....... Filename of output PDF file.\n"
         "   -P <page format> .... Select output page format.\n"
         "   -u .................. Output URLs suitable for OSM data download and\n"
         "                         exit.\n"
         "   -V .................. Show chart parameters and exit.\n"
         "   -w <osm file> ....... Output OSM data to file.\n",
         s
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


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0, n, i;
   struct stat st;
   FILE *f;
   char *cf = "rules.osm", *img_file = NULL, *osm_ifile = NULL, *osm_ofile =
      NULL, *osm_rfile = NULL, *kap_file = NULL, *kap_hfile = NULL, *pdf_file = NULL;
   struct rdata *rd;
   struct timeval tv_start, tv_end;
   int landscape = 0, w_mmap = 1, load_filter = 0, init_exit = 0, gen_grid = AUTO_GRID, prt_url = 0;
   char *paper = "A3", *bg = NULL;
   struct filter fi;
   struct dstats rstats;
   struct grid grd;
   osm_obj_t o;
   char *s;
   struct tile_info ti;

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);
   rd = get_rdata();
   init_grid(&grd);
   rd->cmdline = mk_cmd_line((const char**) argv);
   memset(&ti, 0, sizeof(ti));

   while ((n = getopt(argc, argv, "ab:d:fg:Ghi:k:K:lMmo:O:P:r:R:s:t:T:uVw:")) != -1)
      switch (n)
      {
         case 'a':
            render_all_nodes_ = 1;
            break;

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

         case 'O':
            pdf_file = optarg;
            break;

         case 'P':
            paper = optarg;
            break;

         case 'r':
            cf = optarg;
            break;

         case 's':
            log_msg(LOG_NOTICE, "Option -s is deprecated with libcairo support!");
            break;

         case 'R':
            osm_rfile = optarg;
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
      int i = strcnt(argv[optind], ':');
      double param;

      if (i < 2 || i > 3)
         log_msg(LOG_ERR, "format error in window"), exit(EXIT_FAILURE);

      s = strtok(argv[optind], ":");
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
      else
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
   }

   // install exit handlers
   osm_read_exit();

   init_rd_paper(rd, paper, landscape);
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

   rd->fw = rd->w;
   rd->fh = rd->h;

   init_bbox_mll(rd);

   if (prt_url)
   {
      print_url(rd->bb);
      exit(EXIT_SUCCESS);
   }

   rdata_log();

   if (init_exit)
      exit(EXIT_SUCCESS);

   // preparing image
#ifdef HAVE_CAIRO
   cairo_smr_init_main_image(bg);
#endif

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
      (void) read_osm_file(ctl, get_objtree(), &fi, &rd->ds);
   }
   else
   {
      (void) read_osm_file(ctl, get_objtree(), NULL, &rd->ds);
   }

   if (!rd->ds.ncnt)
   {
      log_msg(LOG_ERR, "no data to render");
      exit(EXIT_NODATA);
   }

   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);

   log_msg(LOG_INFO, "stripping filtered way nodes");
   traverse(*get_objtree(), 0, IDX_WAY, (tree_func_t) strip_ways, rd, NULL);

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
#ifdef WITH_THREADS
      sm_wait_threads();
      dequeue_fini();
#endif
      log_msg(LOG_INFO, " ways...");
      traverse(rd->rules, 0, IDX_WAY, (tree_func_t) apply_smrules, rd, &o);
#ifdef WITH_THREADS
      sm_wait_threads();
      dequeue_fini();
#endif
      log_msg(LOG_INFO, " nodes...");
      traverse(rd->rules, 0, IDX_NODE, (tree_func_t) apply_smrules, rd, &o);
#ifdef WITH_THREADS
      sm_wait_threads();
      dequeue_fini();
#endif
   }

   int_ = 0;

   save_osm(osm_ofile, *get_objtree(), &rd->bb, rd->cmdline);
   (void) close(ctl->fd);
   hpx_free(ctl);
   hpx_free(cfctl);

   log_debug("freeing main objects");
   traverse(*get_objtree(), 0, IDX_REL, free_objects, rd, NULL);
   traverse(*get_objtree(), 0, IDX_WAY, free_objects, rd, NULL);
   traverse(*get_objtree(), 0, IDX_NODE, free_objects, rd, NULL);

   log_debug("freeing rule objects");
   traverse(rd->rules, 0, IDX_REL, (tree_func_t) free_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_WAY, (tree_func_t) free_rules, rd, NULL);
   traverse(rd->rules, 0, IDX_NODE, (tree_func_t) free_rules, rd, NULL);

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

