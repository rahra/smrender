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

/*! This program reads an OSM/XML file and parses, filters, and modifies it.
 *  Filter and modification rules are hardcoded.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/time.h>      // gettimeofday()
#include <fcntl.h>         // stat()
#include <gd.h>
#include <regex.h>
#include <limits.h>  // contains INT_MAX
#include <syslog.h>
#include <dlfcn.h>   // dlopen(),...

#include "smrender.h"
#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
#include "smlog.h"
#include "bxtree.h"


static const char *rule_type_[] = {"N/A", "ACT_IMG", "ACT_CAP", "ACT_FUNC", "ACT_DRAW"};


void usage(const char *s)
{
   printf("Seamark renderer V1.0, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n\n");
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


int check_matchtype(bstring_t *b, struct specialTag *t)
{
   t->type = 0;

   if (b->len > 2)
   {
      if ((b->buf[0] == '!') && (b->buf[b->len - 1] == '!'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_INVERT;
      }
      else if ((b->buf[0] == '~') && (b->buf[b->len - 1] == '~'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_NOT;
      }
 
   }

   if (b->len > 2)
   {
      if ((b->buf[0] == '/') && (b->buf[b->len - 1] == '/'))
      {
         log_debug("seems to be regex: '%.*s' (%d, %c)", b->len, b->buf, b->len, b->buf[b->len - 1]);
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;

         if (regcomp(&t->re, b->buf, REG_EXTENDED | REG_NOSUB))
         {
            log_msg(LOG_WARN, "failed to compile regex '%s'", b->buf);
            return -1;
         }
         t->type |= SPECIAL_REGEX;
      }
   }

   return 0;
}


short ppos(const char *s)
{
   char c[] = "nsmewc";
   int p[] = {POS_N, POS_S, POS_M, POS_E, POS_W, POS_C};
   int i;
   short pos = 0;

   for (i = 0; i < strlen(c); i++)
      if (strchr(s, c[i]) != NULL)
         pos |= p[i];

   return pos;
}


int parse_color(const struct rdata *rd, const char *s)
{
   if (*s == '#')
   {
      log_msg(LOG_WARN, "HTML color style (%s) not supported yet, defaulting to black", s);
      return rd->col[BLACK];
   }
   if (!strcmp(s, "white"))
      return rd->col[WHITE];
   if (!strcmp(s, "yellow"))
      return rd->col[YELLOW];
   if (!strcmp(s, "black"))
      return rd->col[BLACK];
   if (!strcmp(s, "blue"))
      return rd->col[BLUE];
   if (!strcmp(s, "magenta"))
      return rd->col[MAGENTA];
   if (!strcmp(s, "brown"))
      return rd->col[BROWN];

   log_msg(LOG_WARN, "unknown color %s, defaulting to black", s);
   return rd->col[BLACK];
}


int parse_draw(const char *src, struct drawStyle *ds, const struct rdata *rd)
{
   char buf[strlen(src) + 1];
   char *s, *sb;

   strcpy(buf, src);
   if ((s = strtok_r(buf, ",", &sb)) == NULL)
   {
      log_msg(LOG_WARN, "syntax error in draw rule %s", src);
      return -1;
   }

   ds->col = parse_color(rd, s);

   if ((s = strtok_r(NULL, ",", &sb)) == NULL)
      return 0;

   log_msg(LOG_WARN, "draw width and styles are not parsed yet (sorry...)");
   return 0;
}


int prepare_rules(struct onode *nd, struct rdata *rd, void *p)
{
   char *s, *lib;
   FILE *f;
   int i;

   for (i = 0; i < nd->tag_cnt; i++)
   {
      if (check_matchtype(&nd->otag[i].k, &nd->otag[i].stk) == -1)
         return 0;
      if (check_matchtype(&nd->otag[i].v, &nd->otag[i].stv) == -1)
         return 0;
   }

   if ((i = match_attr(nd, "_action_", NULL)) == -1)
   {
      log_msg(LOG_WARN, "rule %ld has no action", nd->nd.id);
      return 0;
   }

   nd->otag[i].v.buf[nd->otag[i].v.len] = '\0';
   s = strtok(nd->otag[i].v.buf, ":");
   if (!strcmp(s, "img"))
   {
      if ((s = strtok(NULL, ":")) == NULL)
         return E_SYNTAX;
      if ((f = fopen(s, "r")) == NULL)
      {
         log_msg(LOG_WARN, "fopen(%s) failed: %s", s, strerror(errno));
         return E_SYNTAX;
      }

      nd->rule.img.angle = 0;
      if ((nd->rule.img.img = gdImageCreateFromPng(f)) == NULL)
         log_msg(LOG_WARN, "could not read PNG from %s", s);
      (void) fclose(f);

      nd->rule.type = ACT_IMG;
      log_debug("successfully imported PNG %s", s);
   }
   else if (!strcmp(s, "img-auto"))
   {
      if ((s = strtok(NULL, ":")) == NULL)
         return E_SYNTAX;
      if ((f = fopen(s, "r")) == NULL)
      {
         log_msg(LOG_WARN, "fopen(%s) failed: %s", s, strerror(errno));
         return 0;
      }

      nd->rule.img.angle = NAN;
      if ((nd->rule.img.img = gdImageCreateFromPng(f)) == NULL)
         log_msg(LOG_WARN, "could not read PNG from %s\n", s);
      (void) fclose(f);

      nd->rule.type = ACT_IMG;
      log_debug("img-auto, successfully imported PNG %s", s);
   }
   else if (!strcmp(s, "cap"))
   {
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      nd->rule.cap.font = s;
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      nd->rule.cap.size = atof(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      nd->rule.cap.pos = ppos(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;

      nd->rule.cap.col = parse_color(rd, s);

      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      if (!strcmp(s, "auto"))
         nd->rule.cap.angle = NAN;
      else
         nd->rule.cap.angle = atof(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;

      nd->rule.cap.key = s;
      nd->rule.type = ACT_CAP;
      log_debug("successfully parsed caption rule");
   }
   else if (!strcmp(s, "func"))
   {
      if ((s = strtok(NULL, "@")) == NULL)
      {
         log_msg(LOG_ERR, "syntax error in function rule");
         return E_SYNTAX;
      }
      if ((lib = strtok(NULL, "")) == NULL)
      {
         log_msg(LOG_ERR, "syntax error in function rule");
         return E_SYNTAX;
      }

      // Open shared library
      if ((nd->rule.func.libhandle = dlopen(lib, RTLD_LAZY)) == NULL)
      {
         log_msg(LOG_ERR, "could not open library: %s", dlerror());
         return 0;
      }

      // Clear any existing error
      dlerror();

      nd->rule.func.sym = dlsym(nd->rule.func.libhandle, s);

      // Check for errors
      if ((s = dlerror()) != NULL)
      {
         log_msg(LOG_ERR, "error loading symbol from libary: %s", s);
         return 0;
      }

      nd->rule.type = ACT_FUNC;
      log_debug("successfully parsed function rule");
   }
   else if (!strcmp(s, "draw"))
   {
      if ((s = strtok(NULL, "")) == NULL)
      {
         log_warn("syntax error in draw rule");
         return E_SYNTAX;
      }

      if (*s != ':')
      {
         s = strtok(s, ":");
         if (parse_draw(s, &nd->rule.draw.fill, rd) == -1)
            return E_SYNTAX;
         nd->rule.draw.fill.used = 1;
         if ((s = strtok(NULL, ":")) != NULL)
         {
            if (!parse_draw(s, &nd->rule.draw.border, rd))
               nd->rule.draw.border.used = 1;
         }
      }
      else
      {
         if (strlen(s) <= 1)
         {
            log_warn("syntax error in draw rule");
            return E_SYNTAX;
         }
         if (!parse_draw(s + 1, &nd->rule.draw.border, rd))
            nd->rule.draw.border.used = 1;
      }

      nd->rule.type = ACT_DRAW;
      log_debug("successfully parsed draw rule");
   }
   else
   {
      log_warn("action type '%s' not supported yet", s);
   }

   // remove _action_ tag from tag list, i.e. move last element
   // to position of _action_ tag (order doesn't matter).
   if (i < nd->tag_cnt - 1)
      memmove(&nd->otag[i], &nd->otag[nd->tag_cnt - 1], sizeof(struct otag));
   nd->tag_cnt--;

   return 0;
}


/*! Convert pixel coordinates back into latitude and longitude. Note that this
 *  leads to some inaccuracy.
 */
void mk_chart_coords(int x, int y, struct rdata *rd, double *lat, double *lon)
{
   *lon = rd->wc *          x  / rd->w + rd->x1c;
   *lat = rd->hc * (rd->h - y) / rd->h + rd->y2c;
}


/*! Convert latitude and longitude coordinates into x and y coordinates of
 * pixel image.
 */
void mk_paper_coords(double lat, double lon, struct rdata *rd, int *x, int *y)
{
   *x = round(        (lon - rd->x1c) * rd->w / rd->wc);
   *y = round(rd->h - (lat - rd->y2c) * rd->h / rd->hc);
}


/*
int coords_inrange(const struct rdata *rd, int x, int y)
{
   return x >= 0 && x < rd->w && y >= 0 && y < rd->h;
}
*/

int act_image(struct onode *nd, struct rdata *rd, struct onode *mnd, int x, int y)
{
   int i, j, c, rx, ry, hx, hy;
   double a;

   hx = gdImageSX(mnd->rule.img.img) / 2;
   hy = gdImageSY(mnd->rule.img.img) / 2;

   a = isnan(mnd->rule.img.angle) ? color_frequency(rd, x, y, hx, hy, rd->col[WHITE]) : 0;
   a = DEG2RAD(a);

   for (j = 0; j < gdImageSY(mnd->rule.img.img); j++)
   {
      for (i = 0; i < gdImageSX(mnd->rule.img.img); i++)
      {
         if (a != 0)
            rot_pos(i - hx, j - hy, a, &rx, &ry);
         else
         {
            rx = i - hx;
            ry = hy - j;
         }
         c = gdImageGetPixel(mnd->rule.img.img, i, j);
         gdImageSetPixel(rd->img, x + rx, y - ry, c);
      }
   }

   return 0;
}


void rot_rect(const struct rdata *rd, int x, int y, double a, int br[])
{
   gdPoint p[5];
   int i;

   rot_pos(br[0] - x, br[1] - y, a, &p[0].x, &p[0].y);
   rot_pos(br[2] - x, br[3] - y, a, &p[1].x, &p[1].y);
   rot_pos(br[4] - x, br[5] - y, a, &p[2].x, &p[2].y);
   rot_pos(br[6] - x, br[7] - y, a, &p[3].x, &p[3].y);

   for (i = 0; i < 4; i++)
   {
      p[i].x += x;
      p[i].y = y - p[i].y;
   }

   p[4] = p[0];

   gdImagePolygon(rd->img, p, 5, rd->col[BLACK]);
}


double color_frequency(struct rdata *rd, int x, int y, int w, int h, int col)
{
   double a, ma = 0;
   int m = 0, mm = 0;

   // auto detect angle
   for (a = 0; a < 360; a += ANGLE_DIFF)
      {
         m = col_freq(rd, x, y, w, h, DEG2RAD(a), col);
         if (mm < m)
         {
            mm = m;
            ma = a;
         }
      }
   return ma;
}

 
#define POS_OFFSET MM2PX(1.3)
#define MAX_OFFSET MM2PX(2.0)
#define DIVX 3
int act_caption(struct onode *nd, struct rdata *rd, struct onode *mnd, int x, int y)
{
   int br[8], n;
   char *s;
   gdFTStringExtra fte;
   int rx, ry, ox, oy, off;
   double ma;

   if ((n = match_attr(nd, mnd->rule.cap.key, NULL)) == -1)
   {
      log_debug("node %ld has no caption tag '%s'", nd->nd.id, mnd->rule.cap.key);
      return 0;
   }

   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;

   nd->otag[n].v.buf[nd->otag[n].v.len] = '\0';
   gdImageStringFTEx(NULL, br, mnd->rule.cap.col, mnd->rule.cap.font, mnd->rule.cap.size * 2.8699, 0, x, y, nd->otag[n].v.buf, &fte);

   if (isnan(mnd->rule.cap.angle))
   {
      ma = color_frequency(rd, x, y, br[4] - br[0] + MAX_OFFSET, br[1] - br[5], rd->col[WHITE]);
      off = cf_dist(rd, x, y, br[4] - br[0], br[1] - br[5], DEG2RAD(ma), rd->col[WHITE], MAX_OFFSET);

      oy =(br[1] - br[5]) / DIVX;
      if ((ma < 90) || (ma >= 270))
      {
         ox = off;
      }
      else
      {
         ma -= 180;
         ox = br[0] - br[2] - off;
      }
   }
   else
   {
      ma = mnd->rule.cap.angle;

      switch (mnd->rule.cap.pos & 3)
      {
         case POS_N:
            oy = 0;
            oy = (br[7] - br[3]) / DIVX;
            break;

         case POS_S:
            oy = br[3] - br[7];
            break;

         default:
            oy = (br[3] - br[7]) / DIVX;
      }
      switch (mnd->rule.cap.pos & 12)
      {
         case POS_E:
            ox = 0;
            break;

         case POS_W:
            ox = br[0] - br[2];
            break;

         default:
            ox = (br[0] - br[2]) / DIVX;
      }
   }

  //rot_rect(rd, x, y, DEG2RAD(ma), br);

   rot_pos(ox, oy, DEG2RAD(ma), &rx, &ry);
   //fprintf(stderr, "dx = %d, dy = %d, rx = %d, ry = %d, ma = %.3f, off = %d, '%s'\n", br[0]-br[2],br[1]-br[5], rx, ry, ma, off, nd->otag[n].v.buf);

   if ((s = gdImageStringFTEx(rd->img, br, mnd->rule.cap.col, mnd->rule.cap.font, mnd->rule.cap.size * 2.8699, DEG2RAD(ma), x + rx, y - ry, nd->otag[n].v.buf, &fte)) != NULL)
      log_msg(LOG_ERR, "error rendering caption: %s", s);

//   else
//      fprintf(stderr, "printed %s at %d,%d\n", nd->otag[n].v.buf, x, y);

   return 0;
}


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
int apply_rules0(struct onode *nd, struct rdata *rd, struct onode *mnd)
{
   int x, y, i, e;

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
   mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &x, &y);

   switch (mnd->rule.type)
   {
      case ACT_IMG:
         e = act_image(nd, rd, mnd, x, y);
         break;

      case ACT_CAP:
         e = act_caption(nd, rd, mnd, x, y);
         break;

      case ACT_FUNC:
         e = mnd->rule.func.func(nd);
         break;

      default:
         e = E_ACT_NOT_IMPL;
         log_warn("action type %d not implemented yet", mnd->rule.type);
   }

   return e;
}


int apply_rules(struct onode *nd, struct rdata *rd, void *vp)
{
   log_debug("applying rule id 0x%016lx type %s(%d)", nd->nd.id, rule_type_[nd->rule.type], nd->rule.type);
   return traverse(rd->obj, 0, IDX_NODE, (tree_func_t) apply_rules0, rd, nd);
}


int act_open_poly(struct onode *wy, struct rdata *rd, struct onode *mnd)
{
   int i;
   struct onode *nd;
   gdPoint p[wy->ref_cnt];

   for (i = 0; i < wy->ref_cnt; i++)
   {
      if ((nd = get_object(OSM_NODE, wy->ref[i])) == NULL)
         return E_REF_ERR;

      mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
   }

   gdImageOpenPolygon(rd->img, p, wy->ref_cnt, rd->col[BLACK]);
   return 0;
}


int act_fill_poly(struct onode *wy, struct rdata *rd, struct onode *mnd)
{
   int i;
   struct onode *nd;
   gdPoint p[wy->ref_cnt];

   for (i = 0; i < wy->ref_cnt; i++)
   {
      if ((nd = get_object(OSM_NODE, wy->ref[i])) == NULL)
         return E_REF_ERR;

      mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
   }

   if (mnd->rule.draw.fill.used)
      gdImageFilledPolygon(rd->img, p, wy->ref_cnt, mnd->rule.draw.fill.col);
   if (mnd->rule.draw.border.used)
      gdImagePolygon(rd->img, p, wy->ref_cnt, mnd->rule.draw.border.col);
   return 0;
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

      default:
         e = E_ACT_NOT_IMPL;
         log_msg(LOG_WARN, "action type %d not implemented yet", mnd->rule.type);
   }

   return e;
}


int apply_wrules(struct onode *nd, struct rdata *rd, void *vp)
{
   log_debug("applying rule id 0x%016lx type %s(%d)", nd->nd.id, rule_type_[nd->rule.type], nd->rule.type);
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


/*! Print string into image at a desired position with correct alignment.
 *  @param rd Pointer to struct rdata.
 *  @param x X position within image; the alignment is referred to these coordinates.
 *  @param y Y position (see above).
 *  @param pos Alignment: this is a combination (logical or) of horizontal and
 *  vertical positioning parameters. The vertical alignment shall be one of
 *  POS_N, POS_S, and P_OSM; the horizontal parameter is one of POS_E, POS_W,
 *  and POS_C.
 *  @param col Color of string.
 *  @param ftsize Fontsize in milimeters.
 *  @param ft Pointer to font file.
 *  @param s Pointer to string buffer.
 *  @return The function returns 0 on success, -1 otherwise.
 */
int img_print(const struct rdata *rd, int x, int y, int pos, int col, double ftsize, const char *ft, const char *s)
{
   char *err;
   int br[8], ox, oy;
   gdFTStringExtra fte;

   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;

   gdImageStringFTEx(NULL, br, col, (char*) ft, MM2PT(ftsize), 0, 0, 0, (char*) s, &fte);

   switch (pos & 3)
   {
      case POS_N:
         oy = 0;
         break;

      case POS_S:
         oy = br[1] - br[5];
         break;

      default:
         oy = (br[1] - br[5]) / 2;
   }
   switch (pos & 12)
   {
      case POS_E:
         ox = 0;
         break;
         
      case POS_W:
         ox = br[0] - br[4];
         break;

      default:
         ox = (br[0] - br[4]) / 2;
   }

   err = gdImageStringFTEx(rd->img, br, col, (char*) ft, MM2PT(ftsize), 0, x + ox, y + oy, (char*) s, &fte);
   if (err != NULL)
   {
      log_warn("gdImageStringFTEx error: '%s'", err);
      return -1;
   }

   return 0;
}


/*! Fill a closed polygon (5 points) with coordinates of a rectangle
 *  with a border distance of b millimeters.
 *  @param rd Pointer to struct rdata.
 *  @param p Pointer to gdPoint array. The array MUST contain at least 5 elements.
 *  @param b Distance from edge of paper im millimeters.
 */
void grid_rcalc(const struct rdata *rd, gdPoint *p, double b)
{
   p[0].x = p[0].y = p[1].y = p[3].x = MM2PX(b);
   p[1].x = p[2].x = rd->w - p[0].x;
   p[2].y = p[3].y = rd->h - p[0].y;
   p[4].x = p[0].x;
   p[4].y = p[0].y;
}


/*! ...
 *  Karte im Maßstab 1:100 000 (Silba-Pag): grid 10', ticks 1', subticks 0.25'
 *  ...
 */
void grid(struct rdata *rd, int col)
{
   gdPoint p[7];
   double l, d, lat, lon;
   int x, y;
   char buf[256];
   int min, deg;

   l = MM2PX(G_MARGIN + G_TW + G_STW);
   snprintf(buf, sizeof(buf), "%s by %s, <%s>. Generated with /smrender/. Data source: OSM",
         SW_COPY, SW_AUTHOR, SW_AEMAIL);
   img_print(rd, l, rd->h - l, POS_E | POS_N, rd->col[BLACK], G_SFTSIZE, G_FONT, buf);

   l = MM2PX(G_MARGIN);
   lat = PX2MM(rd->h) - G_MARGIN * 2;
   lon = PX2MM(rd->w) - G_MARGIN * 2;
   fdm(rd->mean_lat, &deg, &min);
   snprintf(buf, sizeof(buf), "Mean Latitude = %02d %c %02d'   Scale = 1:%d (%.1f x %.1f mm)", abs(deg), deg < 0 ? 'S' : 'N', min, (int) round(rd->scale), lon, lat);
   img_print(rd, rd->w / 2, l / 2, POS_C | POS_M, rd->col[BLACK], G_FTSIZE, G_FONT, buf);

   gdImageSetThickness(rd->img, MM2PX(G_BW));
   for (d = fround(rd->y2c, rd->grd.lat_g); d < rd->y1c; d += rd->grd.lat_g)
   {
      //fprintf(stderr, "d = %s (%f)\n", cfmt(d, LAT, buf, sizeof(buf)), d);
      mk_paper_coords(d, rd->x1c, rd, &p[0].x, &p[0].y);
      mk_paper_coords(d, rd->x2c, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);

      fdm(d, &deg, &min);
      if (min == 1.0) min = 0, deg++;
      snprintf(buf, sizeof(buf), "%02d°", deg);
      img_print(rd, l, p[0].y, POS_N | POS_E, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      img_print(rd, rd->w - l, p[0].y, POS_N | POS_W, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      snprintf(buf, sizeof(buf), "%02d'", min);
      img_print(rd, l, p[0].y, POS_S | POS_E, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      img_print(rd, rd->w - l, p[0].y, POS_S | POS_W, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
   }
   for (d = fround(rd->x1c, rd->grd.lon_g); d < rd->x2c; d += rd->grd.lon_g)
   {
      //fprintf(stderr, "d = %s (%f)\n", cfmt(d, LAT, buf, sizeof(buf)), d);
      mk_paper_coords(rd->y1c, d, rd, &p[0].x, &p[0].y);
      mk_paper_coords(rd->y2c, d, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);

      fdm(d, &deg, &min);
      if (min == 1.0) min = 0, deg++;
      snprintf(buf, sizeof(buf), "%03d°", deg);
      img_print(rd, p[0].x, l, POS_S | POS_W, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      img_print(rd, p[0].x, rd->h - l, POS_N | POS_W, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      snprintf(buf, sizeof(buf), "%02d'", min);
      img_print(rd, p[0].x, l, POS_S | POS_E, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
      img_print(rd, p[0].x, rd->h - l, POS_N | POS_E, rd->col[BLACK], G_FTSIZE, G_FONT, buf);
   }

   grid_rcalc(rd, p, G_MARGIN);
   p[5] = p[0];
   gdImagePolygon(rd->img, p, 5, col);
   grid_rcalc(rd, p, G_MARGIN + G_TW);
   gdImagePolygon(rd->img, p, 5, col);
   grid_rcalc(rd, p, G_MARGIN + G_TW + G_STW);
   p[6] = p[0];
   gdImagePolygon(rd->img, p, 5, col);

   gdImageOpenPolygon(rd->img, &p[5], 2, col);
   p[5].x = rd->w - p[5].x;
   p[6].x = rd->w - p[6].x;
   gdImageOpenPolygon(rd->img, &p[5], 2, col);
   p[5].y = rd->h - p[5].y;
   p[6].y = rd->h - p[6].y;
   gdImageOpenPolygon(rd->img, &p[5], 2, col);
   p[5].x = rd->w - p[5].x;
   p[6].x = rd->w - p[6].x; 
   gdImageOpenPolygon(rd->img, &p[5], 2, col);
   
   p[0].x = MM2PX(G_MARGIN);
   p[1].x = MM2PX(G_MARGIN + G_TW + G_STW);
   p[2].x = MM2PX(G_MARGIN + G_TW);
   p[3].x = rd->w - p[0].x;
   p[4].x = rd->w - p[1].x;
   p[5].x = rd->w - p[2].x;
   p[0].y = rd->h - p[1].x;
   d = p[1].x;
   mk_chart_coords(p[0].x, p[0].y, rd, &lat, &lon);
   // draw ticks and subticks on left and right border
   //for (l = fround(lat, G_STICKS); l < rd->y2c; l += G_STICKS)
   //   fprintf(stderr, "%f\n", l);

   for (l = fround(lat, G_STICKS); l < rd->y1c; l += G_STICKS)
   {
      mk_paper_coords(l, lon, rd, &x, &y);
      p[0].y = p[1].y = p[2].y = p[3].y = p[4].y = p[5].y = y;
      if (y > (rd->h - d)) continue;
      if (y < d) break;
      //fprintf(stderr, "l = %s (%f), y = %d, frnd(l,GT) = %f\n", cfmt(l, LAT, buf, sizeof(buf)), l, y, fround(l, G_TICKS));
      gdImageOpenPolygon(rd->img, &p[1], 2, col);
      gdImageOpenPolygon(rd->img, &p[4], 2, col);
   }

   p[0].x = MM2PX(G_MARGIN);
   p[1].x = MM2PX(G_MARGIN + G_TW + G_STW);
   p[2].x = MM2PX(G_MARGIN + G_TW);
   p[3].x = rd->w - p[0].x;
   p[4].x = rd->w - p[1].x;
   p[5].x = rd->w - p[2].x;
   p[0].y = rd->h - p[1].x;
   d = p[1].x;
   mk_chart_coords(p[0].x, p[0].y, rd, &lat, &lon);
   // draw ticks and subticks on left and right border
   for (l = fround(lat, G_TICKS); l < rd->y1c; l += G_TICKS)
   {
      mk_paper_coords(l, lon, rd, &x, &y);
      p[0].y = p[1].y = p[2].y = p[3].y = p[4].y = p[5].y = y;
      if (y > (rd->h - d)) continue;
      if (y < d) break;
      //fprintf(stderr, "l = %s (%f), y = %d, frnd(l,GT) = %f\n", cfmt(l, LAT, buf, sizeof(buf)), l, y, fround(l, G_TICKS));
      gdImageOpenPolygon(rd->img, &p[0], 2, col);
      gdImageOpenPolygon(rd->img, &p[3], 2, col);
   }

   // draw ticks and subticks on top and bottom border
   p[0].y = MM2PX(G_MARGIN);
   p[1].y = MM2PX(G_MARGIN + G_TW + G_STW);
   p[2].y = MM2PX(G_MARGIN + G_TW);
   p[3].y = rd->h - p[0].y;
   p[4].y = rd->h - p[1].y;
   p[5].y = rd->h - p[2].y;
   p[0].x = p[1].y;
   d = p[1].y;
   mk_chart_coords(p[0].x, p[0].y, rd, &lat, &lon);
   for (l = fround(lon, G_STICKS); l < rd->x2c; l += G_STICKS)
   {
      mk_paper_coords(lat, l, rd, &x, &y);
      p[0].x = p[1].x = p[2].x = p[3].x = p[4].x = p[5].x = x;
      if (x < d) continue;
      if (x > (rd->w - d)) break;
      //fprintf(stderr, "l = %s (%f), y = %d, frnd(l,GT) = %f\n", cfmt(l, LON, buf, sizeof(buf)), l, y, fround(l, G_TICKS));
      gdImageOpenPolygon(rd->img, &p[1], 2, col);
      gdImageOpenPolygon(rd->img, &p[4], 2, col);
   }

   // draw ticks and subticks on top and bottom border
   p[0].y = MM2PX(G_MARGIN);
   p[1].y = MM2PX(G_MARGIN + G_TW + G_STW);
   p[2].y = MM2PX(G_MARGIN + G_TW);
   p[3].y = rd->h - p[0].y;
   p[4].y = rd->h - p[1].y;
   p[5].y = rd->h - p[2].y;
   p[0].x = p[1].y;
   d = p[1].y;
   mk_chart_coords(p[0].x, p[0].y, rd, &lat, &lon);
   for (l = fround(lon, G_TICKS); l < rd->x2c; l += G_TICKS)
   {
      mk_paper_coords(lat, l, rd, &x, &y);
      p[0].x = p[1].x = p[2].x = p[3].x = p[4].x = p[5].x = x;
      if (x < d) continue;
      if (x > (rd->w - d)) break;
      //fprintf(stderr, "l = %s (%f), y = %d, frnd(l,GT) = %f\n", cfmt(l, LON, buf, sizeof(buf)), l, y, fround(l, G_TICKS));
      gdImageOpenPolygon(rd->img, &p[0], 2, col);
      gdImageOpenPolygon(rd->img, &p[3], 2, col);
   }


}


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


double rot_pos(int x, int y, double a, int *rx, int *ry)
{
   double r, b;

   r = sqrt(x * x + y * y);
   //b = atan((double) y1 / (double) x1);
   b = atan2((double) y, (double) x);
   *rx = round(r * cos(a - b));
   *ry = round(r * sin(a - b));

   return r;
}


int cf_dist(struct rdata *rd, int x, int y, int w, int h, double a, int col, int mdist)
{
   int rx, ry, d, freq, max_freq = 0, dist = 0;

   for (d = 0; d < mdist; d++)
   {
      rot_pos(d, 0, a, &rx, &ry);
      freq = col_freq(rd, x + rx, y - ry, w, h, a, col);
      if (max_freq < freq)
      {
         max_freq = freq;
         dist = d;
      }
   }

   return dist;
}



int col_freq(struct rdata *rd, int x, int y, int w, int h, double a, int col)
{
   int x1, y1, rx, ry, c = 0;

   for (y1 = -h / 2; y1 < h / 2; y1++)
      for (x1 = 0; x1 < w; x1++)
      {
         rot_pos(x1, y1, a, &rx, &ry);
         c += (col == gdImageGetPixel(rd->img, x + rx, y - ry));
      }

   return c;
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


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0;
   struct stat st;
   FILE *f = stdout;
   char *cf = "rules.osm";
   struct rdata *rd;
   struct timeval tv_start, tv_end;

   (void) gettimeofday(&tv_start, NULL);
   init_log("stderr", LOG_DEBUG);

   log_msg(LOG_INFO, "initializing structures");
   rd = init_rdata();
   //print_rdata(stderr, rd);
   init_prj(rd, PRJ_MERC_PAGE);
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

   if ((argc >= 2) && ((fd = open(argv[1], O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading osm data (file size %ld kb)", (long) st.st_size / 1024);
   (void) read_osm_file(ctl, &rd->obj);
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

#ifdef MEM_USAGE
   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);
#endif

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

   log_msg(LOG_INFO, "preparing coastline");
   cat_poly(rd);

   log_msg(LOG_INFO, "rendering ways");
   traverse(rd->rules, 0, IDX_WAY, apply_wrules, rd, NULL);
   log_msg(LOG_INFO, "rendering nodes");
   traverse(rd->rules, 0, IDX_NODE, apply_rules, rd, NULL);

   log_msg(LOG_INFO, "creating grid and legend");
   grid(rd, rd->col[BLACK]);

   save_osm(rd, "out.osm");
   hpx_free(ctl);
   hpx_free(cfctl);

   log_msg(LOG_INFO, "saving image");
   gdImagePng(rd->img, f);
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

