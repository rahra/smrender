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
#include <fcntl.h>
#include <gd.h>
#include <regex.h>
#include <limits.h>  // contains INT_MAX

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
//#include "seamark.h"
#include "smlog.h"
#include "bxtree.h"
#include "smrender.h"
//#include "smrules.h"


struct rdata rdata_;


void usage(const char *s)
{
   printf("Seamark renderer V1.0, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n\n");
}


double fround(double x, double y)
{
   return x - fmod(x, y);
}


char *cfmt(double c, int d, char *s, int l)
{
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


int bs_cmp2(const bstring_t *s1, const bstring_t *s2)
{
   if (s1->len != s2->len)
      return s1->len > s2->len ? 1 : -1;
   return memcmp(s1->buf, s2->buf, s1->len);
}


int bs_match(const bstring_t *dst, const bstring_t *pat, const struct specialTag *st)
{
   int r;
   char buf[dst->len + 1];

   if (st == NULL)
      return bs_cmp2(dst, pat) == 0;

   if ((st->type & SPECIAL_MASK) == SPECIAL_DIRECT)
   {
      r = bs_cmp2(dst, pat);
      if (st->type & SPECIAL_INVERT)
         return r != 0;
      else
         return r == 0;
   }

   if ((st->type & SPECIAL_MASK) == SPECIAL_REGEX)
   {
      // FIXME: this could be avoid if tags are 0-terminated.
      memcpy(buf, dst->buf, dst->len);
      buf[dst->len] = '\0';
 
      r = regexec(&st->re, buf, 0, NULL, 0);
      if (st->type & SPECIAL_INVERT)
         return r != 0;
      else
         return r == 0;
   }

   return 0;
}


int bs_match_attr(const struct onode *nd, const struct otag *ot)
{
   int i, kmatch, vmatch;

   for (i = 0; i < nd->tag_cnt; i++)
   {
      kmatch = vmatch = 0;

      kmatch = ot->k.len ? bs_match(&nd->otag[i].k, &ot->k, &ot->stk) : 1;
      vmatch = ot->v.len ? bs_match(&nd->otag[i].v, &ot->v, &ot->stv) : 1;

      if (kmatch && (ot->stk.type & SPECIAL_NOT))
         return -1;

      if (vmatch && (ot->stv.type & SPECIAL_NOT))
         return -1;

      if (kmatch && vmatch)
         return i;
   }

   if ((ot->stk.type & SPECIAL_NOT) || (ot->stv.type & SPECIAL_NOT))
      return INT_MAX;

   return -1;
}


/*! Match tag.
 *  @return -1 on error (no match), otherwise return number of tag which matches.
 */
int match_attr(const struct onode *nd, const char *k, const char *v)
{
   struct otag ot;

   memset(&ot, 0, sizeof(ot));

   if (k)
   {
      ot.k.len = strlen(k);
      ot.k.buf = (char*) k;
   }
   if (v)
   {
      ot.v.len = strlen(v);
      ot.v.buf = (char*) v;
   }

   return bs_match_attr(nd, &ot);
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
         fprintf(stderr, "seems to be regex: '%.*s' (%d, %c) \n", b->len, b->buf, b->len, b->buf[b->len - 1]);
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         fprintf(stderr, "preparing regex '%s'\n", b->buf);

         if (regcomp(&t->re, b->buf, REG_EXTENDED | REG_NOSUB))
         {
            fprintf(stderr, "failed to compile regex '%s'\n", b->buf);
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


int parse_draw(const char *src, struct drawStyle *ds, const struct rdata *rd)
{
   char buf[strlen(src) + 1];
   char *s, *sb;

   strcpy(buf, src);
   if ((s = strtok_r(buf, ",", &sb)) == NULL)
   {
      fprintf(stderr, "syntax error in draw rule %s\n", src);
      return -1;
   }
   if (*s == '#')
   {
      fprintf(stderr, "HTML color style (%s) not supported yet, defaulting to black\n", src);
      ds->col = rd->col[BLACK];
   }
   else if (!strcmp(s, "white"))
      ds->col = rd->col[WHITE];
   else if (!strcmp(s, "yellow"))
      ds->col = rd->col[YELLOW];
   else if (!strcmp(s, "black"))
      ds->col = rd->col[BLACK];
   else if (!strcmp(s, "blue"))
      ds->col = rd->col[BLUE];
   else if (!strcmp(s, "magenta"))
      ds->col = rd->col[MAGENTA];
   else if (!strcmp(s, "brown"))
      ds->col = rd->col[BROWN];
   else
   {
      fprintf(stderr, "unknown color %s\n", s);
      return -1;
   }

   if ((s = strtok_r(NULL, ",", &sb)) == NULL)
      return 0;

   fprintf(stderr, "draw width and styles are not parsed yet (sorry...)\n");
   return 0;
}


void prepare_rules(struct onode *nd, struct rdata *rd, void *p)
{
   char *s;
   FILE *f;
   int i;

   for (i = 0; i < nd->tag_cnt; i++)
   {
      if (check_matchtype(&nd->otag[i].k, &nd->otag[i].stk) == -1)
         return;
      if (check_matchtype(&nd->otag[i].v, &nd->otag[i].stv) == -1)
         return;
   }

   if ((i = match_attr(nd, "_action_", NULL)) == -1)
   {
      fprintf(stderr, "rule has no action\n");
      return;
   }

   nd->otag[i].v.buf[nd->otag[i].v.len] = '\0';
   s = strtok(nd->otag[i].v.buf, ":");
   if (!strcmp(s, "img"))
   {
      if ((s = strtok(NULL, ":")) == NULL)
         return;
      if ((f = fopen(s, "r")) == NULL)
      {
         fprintf(stderr, "fopen(%s) failed: %s\n", s, strerror(errno));
         return;
      }

      if ((nd->rule.img.img = gdImageCreateFromPng(f)) == NULL)
         fprintf(stderr, "could not read PNG from %s\n", s);
      (void) fclose(f);

      nd->rule.type = ACT_IMG;
      fprintf(stderr, "successfully imported PNG %s\n", s);
   }
   else if (!strcmp(s, "cap"))
   {
      if ((s = strtok(NULL, ",")) == NULL) return;
      nd->rule.cap.font = s;
      if ((s = strtok(NULL, ",")) == NULL) return;
      nd->rule.cap.size = atof(s);
      if ((s = strtok(NULL, ",")) == NULL) return;
      nd->rule.cap.pos = ppos(s);
      if ((s = strtok(NULL, ",")) == NULL) return;

      if (!strcmp(s, "magenta"))
         nd->rule.cap.col = rd->col[MAGENTA];
      if (!strcmp(s, "yellow"))
         nd->rule.cap.col = rd->col[YELLOW];
      if (!strcmp(s, "white"))
         nd->rule.cap.col = rd->col[WHITE];
      if (!strcmp(s, "blue"))
         nd->rule.cap.col = rd->col[BLUE];
      if (!strcmp(s, "brown"))
         nd->rule.cap.col = rd->col[BROWN];
      else
         nd->rule.cap.col = rd->col[BLACK];

      if ((s = strtok(NULL, ",")) == NULL) return;

      nd->rule.cap.key = s;
      nd->rule.type = ACT_CAP;
      fprintf(stderr, "successfully parsed caption rule\n");
   }
   else if (!strcmp(s, "draw"))
   {
      if ((s = strtok(NULL, "")) == NULL)
      {
         fprintf(stderr, "syntax error in draw rule\n");
         return;
      }

      if (*s != ':')
      {
         s = strtok(s, ":");
         if (parse_draw(s, &nd->rule.draw.fill, rd) == -1)
            return;
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
            fprintf(stderr, "syntax error in draw rule\n");
            return;
         }
         if (!parse_draw(s + 1, &nd->rule.draw.border, rd))
            nd->rule.draw.border.used = 1;
      }

      nd->rule.type = ACT_DRAW;
      fprintf(stderr, "successfully parsed draw rule\n");
   }
   else
   {
      fprintf(stderr, "action type '%s' not supported yet\n", s);
   }

   // remove _action_ tag from tag list
   if (i < nd->tag_cnt - 1)
      memcpy(&nd->otag[i], &nd->otag[i + 1], sizeof(struct otag));
   nd->tag_cnt--;
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
   *x =         (lon - rd->x1c) * rd->w / rd->wc;
   *y = rd->h - (lat - rd->y2c) * rd->h / rd->hc;
}


int coords_inrange(const struct rdata *rd, int x, int y)
{
   return x >= 0 && x < rd->w && y >= 0 && y < rd->h;
}


int act_image(struct onode *nd, struct rdata *rd, struct onode *mnd, int x, int y)
{
   int i, j, c;

   x -= gdImageSX(mnd->rule.img.img) / 2;
   y -= gdImageSY(mnd->rule.img.img) / 2;

   for (j = 0; j < gdImageSY(mnd->rule.img.img); j++)
   {
      for (i = 0; i < gdImageSX(mnd->rule.img.img); i++)
      {
         c = gdImageGetPixel(mnd->rule.img.img, i, j);
//       if (gdTrueColorGetAlpha(c))
//          continue;
//       gdImageSetPixel(rd->img, i + x, j + y, rd->col[BLACK]);
         gdImageSetPixel(rd->img, i + x, j + y, c);
      }
   }

   return 0;
}


int act_caption(struct onode *nd, struct rdata *rd, struct onode *mnd, int x, int y)
{
   int br[8], n;
   char *s;
   gdFTStringExtra fte;
   int a, m, mm, ma;

   if ((n = match_attr(nd, mnd->rule.cap.key, NULL)) == -1)
   {
      fprintf(stderr, "node %ld has no caption tag '%s'\n", nd->nd.id, mnd->rule.cap.key);
      return 0;
   }

   // auto detect angle
   m = mm = ma = 0;
   for (a = 0; a < 360; a += 10)
   {
      m = col_freq(rd, x, y, 50, 10, a, rd->col[WHITE]);
      if (mm < m)
      {
         mm = m;
         ma = a;
      }
   }
   for (a = 0; a < 360; a += 10)
   {
      m = col_freq(rd, x, y, 50, 10, a, rd->col[YELLOW]);
      if (mm < m)
      {
         mm = m;
         ma = a;
      }
   }

   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;

   nd->otag[n].v.buf[nd->otag[n].v.len] = '\0';
   if ((s = gdImageStringFTEx(rd->img, br, mnd->rule.cap.col, mnd->rule.cap.font, mnd->rule.cap.size * 2.8699, ma, x, y, nd->otag[n].v.buf, &fte)) != NULL)
      fprintf(stderr, "error rendering caption: %s\n", s);
//   else
//      fprintf(stderr, "printed %s at %d,%d\n", nd->otag[n].v.buf, x, y);

   return 0;
}


/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
void apply_rules0(struct onode *nd, struct rdata *rd, struct onode *mnd)
{
   int x, y, i;

   if (!mnd->rule.type)
   {
      fprintf(stderr, "ACT_NA rule ignored\n");
      return;
   }

   // check if node has tags
   if (!nd->tag_cnt)
      return;

   for (i = 0; i < mnd->tag_cnt; i++)
      if (bs_match_attr(nd, &mnd->otag[i]) == -1)
         return;

   //fprintf(stderr, "node id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);
   mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &x, &y);

   switch (mnd->rule.type)
   {
      case ACT_IMG:
         act_image(nd, rd, mnd, x, y);
        break;

      case ACT_CAP:
         act_caption(nd, rd, mnd, x, y);
         break;

      default:
         fprintf(stderr, "action type %d not implemented yet\n", mnd->rule.type);
   }
}


void apply_rules(struct onode *nd, struct rdata *rd, void *vp)
{
   fprintf(stderr, "rule id 0x%016lx type %d\n", nd->nd.id, nd->rule.type);
   traverse(rd->nodes, 0, (void (*)(struct onode *, struct rdata *, void *)) apply_rules0, rd, nd);
}


void act_open_poly(struct onode *wy, struct rdata *rd, struct onode *mnd)
{
   int i;
   bx_node_t *nt;
   struct onode *nd;
   gdPoint p[wy->ref_cnt];

   for (i = 0; i < wy->ref_cnt; i++)
   {
      if ((nt = bx_get_node(rd->nodes, wy->ref[i])) == NULL)
      {
         fprintf(stderr, "*** bx_get_node() failed\n");
         return;
      }
      if ((nd = nt->next[0]) == NULL)
      {
         fprintf(stderr, "*** nt->next[0] contains NULL pointer\n");
         return;
      }
      mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
   }

   gdImageOpenPolygon(rd->img, p, wy->ref_cnt, rd->col[BLACK]);
}


void act_fill_poly(struct onode *wy, struct rdata *rd, struct onode *mnd)
{
   int i;
   bx_node_t *nt;
   struct onode *nd;
   gdPoint p[wy->ref_cnt];

   for (i = 0; i < wy->ref_cnt; i++)
   {
      if ((nt = bx_get_node(rd->nodes, wy->ref[i])) == NULL)
      {
         fprintf(stderr, "*** bx_get_node() failed\n");
         return;
      }
      if ((nd = nt->next[0]) == NULL)
      {
         fprintf(stderr, "*** nt->next[0] contains NULL pointer\n");
         return;
      }
      mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
   }

   if (mnd->rule.draw.fill.used)
      gdImageFilledPolygon(rd->img, p, wy->ref_cnt, mnd->rule.draw.fill.col);
   if (mnd->rule.draw.border.used)
      gdImagePolygon(rd->img, p, wy->ref_cnt, mnd->rule.draw.border.col);
}

/*! Match and apply ruleset to node.
 *  @param nd Node which should be rendered.
 *  @param rd Pointer to general rendering parameters.
 *  @param mnd Ruleset.
 */
void apply_wrules0(struct onode *nd, struct rdata *rd, struct onode *mnd)
{
   int i;

   if (!mnd->rule.type)
   {
      fprintf(stderr, "ACT_NA rule ignored\n");
      return;
   }

   // check if node has tags
   if (!nd->tag_cnt)
      return;

   for (i = 0; i < mnd->tag_cnt; i++)
      if (bs_match_attr(nd, &mnd->otag[i]) == -1)
         return;

   //fprintf(stderr, "way id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

   switch (mnd->rule.type)
   {
      case ACT_DRAW:
         if (nd->ref[0] == nd->ref[nd->ref_cnt - 1])
            act_fill_poly(nd, rd, mnd);
         else
            act_open_poly(nd, rd, mnd);
        break;

      default:
         fprintf(stderr, "action type %d not implemented yet\n", mnd->rule.type);
   }
}


void apply_wrules(struct onode *nd, struct rdata *rd, void *vp)
{
   traverse(rd->ways, 0, (void (*)(struct onode *, struct rdata *, void *)) apply_wrules0, rd, nd);
}


#if 0
void draw_coast_fill(struct onode *nd, struct rdata *rd, void *vp)
{
   bx_node_t *nt;
   struct onode *node;
   gdPoint *p;
   int i, j, x, y, c;

   if (match_attr(nd, "natural", "coastline") == -1)
      return;

   if (nd->ref[nd->ref_cnt - 1] == nd->ref[0])
      return;

   fprintf(stderr, "found open coastline: %ld\n", nd->nd.id);

   if ((p = malloc(sizeof(gdPoint) * nd->ref_cnt)) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   for (i = 0, j = 0; i < nd->ref_cnt; i++)
   {
      if ((nt = bx_get_node(rd->nodes, nd->ref[i])) == NULL)
      {
         fprintf(stderr, "*** missing node %ld in way %ld\n", nd->ref[i], nd->nd.id);
         continue;
      }
      // FIXME: add NULL pointer check
      node = nt->next[0];
      mk_paper_coords(node->nd.lat, node->nd.lon, rd, &p[j].x, &p[j].y);
      j++;
   }

   for (i = 0; i < j - 1; i++)
   {
      // check if point is within image
      if (!coords_inrange(rd, p[i].x, p[i].y))
         continue;
      if (!coords_inrange(rd, p[i + 1].x, p[i + 1].y))
      {
         i++;
         continue;
      }

      if (p[i].y < p[i+1].y) // line heads South (land is easterly)
      {
         // find next non-BLACK pixel in easterly direction
         for (x = p[i].x + 1; x < rd->w; x++)
         {
            //gdImageSetPixel(rd->img, x, p[i].y, 0x00ff00);
            if ((c = gdImageGetPixel(rd->img, x, p[i].y)) != rd->col[BLACK])
               break;
         }

         // fill area if it is not filled already
         if ((x < rd->w) && (c != rd->col[YELLOW]))
         {
            //gdImageFill(rd->img, x, p[i].y, rd->col[YELLOW]);
            //fprintf(stderr, "%d %d\n", x, p[i].y);
         }
         else
         {
            fprintf(stderr, "area filled\n");
            break;
         }
         continue;
      }

      if (p[i].y > p[i+1].y) // line heads North (land is westerly)
      {
         // find next non-BLACK pixel in easterly direction
         for (x = p[i].x - 1; x >= 0; x--)
         {
            //gdImageSetPixel(rd->img, x, p[i].y, 0x00ff00);
            if ((c = gdImageGetPixel(rd->img, x, p[i].y)) != rd->col[BLACK])
               break;
         }

         // fill area if it is not filled already
         if ((x >= 0) && (c != rd->col[YELLOW]))
         {
            //gdImageFill(rd->img, x, p[i].y, rd->col[YELLOW]);
            //fprintf(stderr, "%d %d\n", x, p[i].y);
         }
         else
         {
            fprintf(stderr, "area filled or out of range\n");
            break;
         }
         continue;
      }

      // else, line is Meridian

      if (p[i].x < p[i + 1].x)  // line heads West (land is southerly)
      {
         // find next non-BLACK pixel in easterly direction
         for (y = p[i].y + 1; y < rd->h; y++)
         {
            //gdImageSetPixel(rd->img, x, p[i].y, 0x00ff00);
            if ((c = gdImageGetPixel(rd->img, p[i].x, y)) != rd->col[BLACK])
               break;
         }

         // fill area if it is not filled already
         if ((y < rd->h) && (c != rd->col[YELLOW]))
         {
            //gdImageFill(rd->img, p[i].x, y, rd->col[YELLOW]);
            //fprintf(stderr, "%d %d\n", p[i].x, y);
         }
         else
         {
            fprintf(stderr, "area filled or out of range\n");
            break;
         }
         continue;
      }

      if (p[i].x > p[i + 1].x) //line heads East (land is northerly)
      {
         // find next non-BLACK pixel in easterly direction
         for (y = p[i].y - 1; y >= 0; y--)
         {
            //gdImageSetPixel(rd->img, x, p[i].y, 0x00ff00);
            if ((c = gdImageGetPixel(rd->img, p[i].x, y)) != rd->col[BLACK])
               break;
         }

         // fill area if it is not filled already
         if ((y >= 0) && (c != rd->col[YELLOW]))
         {
            //gdImageFill(rd->img, p[i].x, y, rd->col[YELLOW]);
            //fprintf(stderr, "%d %d\n", p[i].x, y);
         }
         else
         {
            fprintf(stderr, "area filled or out of range\n");
            break;
         }
         continue;
      }

      // line is Parallel (...and Meridian)
      fprintf(stderr, "points exactly overlapping\n");
   }

   free(p);
}


void draw_coast(struct onode *nd, struct rdata *rd, void *vp)
{
   bx_node_t *nt;
   struct onode *node;
   gdPoint *p;
   int i, j;

   if (match_attr(nd, "natural", "coastline") == -1)
      return;

   if ((p = malloc(sizeof(gdPoint) * nd->ref_cnt)) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   for (i = 0, j = 0; i < nd->ref_cnt; i++)
   {
      if ((nt = bx_get_node(rd->nodes, nd->ref[i])) == NULL)
      {
         fprintf(stderr, "*** missing node %ld in way %ld\n", nd->ref[i], nd->nd.id);
         continue;
      }
      // FIXME: add NULL pointer check
      node = nt->next[0];
      mk_paper_coords(node->nd.lat, node->nd.lon, rd, &p[j].x, &p[j].y);
      j++;
   }
   if (nd->ref[nd->ref_cnt - 1] == nd->ref[0])
   {
      gdImageFilledPolygon(rd->img, p, j, rd->col[YELLOW]);
      gdImagePolygon(rd->img, p, j, rd->col[BLACK]);
   }
   else
   {
      gdImageOpenPolygon(rd->img, p, j, rd->col[BLACK]);
      //gdImageFillToBorder(rd->img, p[0].x + 1, p[0].y + 1, YELLOW, BLACK);
      /*
      for (i = 0; i < j - 1; i++)
         gdImageLine(rd->img, p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, rd->col[BLACK]);
         */
   }

   free(p);
}
#endif


void print_tree(struct onode *nd, struct rdata *rd)
{
   int i;

         switch (nd->nd.type)
         {
            case OSM_NODE:
               printf("<node version='%d' id='%ld' lat='%f' lon='%f'",  nd->nd.ver, nd->nd.id, nd->nd.lat, nd->nd.lon);
               if (!nd->tag_cnt)
                  printf("/>\n");
               else
               {
                  printf(">\n");
                  for (i = 0; i < nd->tag_cnt; i++)
                     printf(" <tag k='%.*s' v='%.*s'/>\n",
                           nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);
                  printf("</node>\n");
               }

               break;

            case OSM_WAY:

               if (match_attr(nd, "natural", "coastline"))
                  printf("<!-- COASTLINE -->\n");

               printf("<way version='%d' id='%ld'",  nd->nd.ver, nd->nd.id);

               if (!nd->tag_cnt && !nd->ref_cnt)
                  printf("/>\n");
               else
               {
                  printf(">\n");
                  for (i = 0; i < nd->tag_cnt; i++)
                     printf(" <tag k='%.*s' v='%.*s'/>\n",
                           nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);
                  for (i = 0; i < nd->ref_cnt; i++)
                     printf(" <nd ref='%ld'/>\n", nd->ref[i]);
 
                  printf("</way>\n");
               }

               break;

            default:
               printf("<!-- node type %d unknown -->\n", nd->nd.type);
               return;
         }
}


void traverse(const bx_node_t *nt, int d, void (*dhandler)(struct onode*, struct rdata*, void*), struct rdata *rd, void *p)
{
   int i;

   if (nt == NULL)
   {
      fprintf(stderr, "null pointer catched...breaking recursion\n");
      return;
   }

   if (d == sizeof(bx_hash_t) * 8 / BX_RES)
   {
      if (nt->next[0] != NULL)
         dhandler(nt->next[0], rd, p);
      else
         fprintf(stderr, "*** this should not happen: NULL pointer catched\n");

      return;
   }

   for (i = 0; i < 1 << BX_RES; i++)
      if (nt->next[i])
         traverse(nt->next[i], d + 1, dhandler, rd, p);

   return;
}


void print_rdata(FILE *f, const struct rdata *rd)
{
   fprintf(f, "rdata:\nx1c = %.3f, y1c = %.3f, x2c = %.3f, y2c = %.3f\n"
         "mean_lat = %.3f, mean_lat_len = %.3f (%.1f nm)\nwc = %.3f, hc = %.3f\n"
         "w = %d, h = %d px\ndpi = %d\nscale = 1:%.0f\npage size = %.1f x %.1f mm\n"
         "grid = %.1f', ticks = %.2f', subticks = %.2f'\n",
         rd->x1c, rd->y1c, rd->x2c, rd->y2c, rd->mean_lat, rd->mean_lat_len, rd->mean_lat_len * 60,
         rd->wc, rd->hc, rd->w, rd->h, rd->dpi, rd->scale, (double) rd->w / rd->dpi * 25.4, (double) rd->h / rd->dpi * 25.4,
         rd->grd.lat_g * 60, rd->grd.lat_ticks * 60, rd->grd.lat_sticks * 60
         );

   fprintf(f, "G_GRID %.3f\nG_TICKS %.3f\nG_STICKS %.3f\nG_MARGIN %.2f\nG_TW %.2f\nG_STW %.2f\nG_BW %.2f\n",
         G_GRID, G_TICKS, G_STICKS, G_MARGIN, G_TW, G_STW, G_BW);
}


/*
double ticks(double d)
{
   int m;

   m = d * 60;
   if (!m) m = d * 600;
   
if (m >= 10)
{
   m /= 10;
   m *= 10;
}

   return (double) m / 60;
}
*/

/*! Fill a closed polygon (5 points) with coordinates of a rectangle
 *  with a border distance of b millimeters.
 *  @param rd Pointer to struct rdata.
 *  @param p Pointer to gdPoint array. The array MUST contain at least 5 elements.
 *  @param b Distance from edge of paper im millimeters.
 */
void grid_rcalc(const struct rdata *rd, gdPoint *p, double b)
{
   p[0].x = p[0].y = p[1].y = p[3].x = b * rd->dpi / 25.4;
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
   char buf[100];

   gdImageSetThickness(rd->img, round(G_BW * rd->dpi / 25.4));
   for (d = fround(rd->y2c, rd->grd.lat_g); d < rd->y1c; d += rd->grd.lat_g)
   {
      fprintf(stderr, "d = %s (%f)\n", cfmt(d, LAT, buf, sizeof(buf)), d);
      mk_paper_coords(d, rd->x1c, rd, &p[0].x, &p[0].y);
      mk_paper_coords(d, rd->x2c, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);
   }
   for (d = fround(rd->x1c, rd->grd.lon_g); d < rd->x2c; d += rd->grd.lon_g)
   {
      fprintf(stderr, "d = %s (%f)\n", cfmt(d, LAT, buf, sizeof(buf)), d);
      mk_paper_coords(rd->y1c, d, rd, &p[0].x, &p[0].y);
      mk_paper_coords(rd->y2c, d, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);
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
   
   p[0].x = G_MARGIN * rd->dpi / 25.4;
   p[1].x = (G_MARGIN + G_TW + G_STW) * rd->dpi / 25.4;
   p[2].x = (G_MARGIN + G_TW) * rd->dpi / 25.4;
   p[3].x = rd->w - p[0].x;
   p[4].x = rd->w - p[1].x;
   p[5].x = rd->w - p[2].x;
   p[0].y = rd->h - p[1].x;
   d = p[1].x;
   mk_chart_coords(p[0].x, p[0].y, rd, &lat, &lon);
   // draw ticks and subticks on left and right border
   for (l = fround(lat, G_STICKS); l < rd->y2c; l += G_STICKS)
      fprintf(stderr, "%f\n", l);

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

   p[0].x = G_MARGIN * rd->dpi / 25.4;
   p[1].x = (G_MARGIN + G_TW + G_STW) * rd->dpi / 25.4;
   p[2].x = (G_MARGIN + G_TW) * rd->dpi / 25.4;
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
   p[0].y = G_MARGIN * rd->dpi / 25.4;
   p[1].y = (G_MARGIN + G_TW + G_STW) * rd->dpi / 25.4;
   p[2].y = (G_MARGIN + G_TW) * rd->dpi / 25.4;
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
   p[0].y = G_MARGIN * rd->dpi / 25.4;
   p[1].y = (G_MARGIN + G_TW + G_STW) * rd->dpi / 25.4;
   p[2].y = (G_MARGIN + G_TW) * rd->dpi / 25.4;
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
            fprintf(stderr, "Warning: window enlarged in latitude! This may result in incorrect rendering.\n");
         rd->y1c = y1;
         rd->y2c = y2;
         break;

      case PRJ_MERC_BB:
         fprintf(stderr, "*** projection PRJ_MERC_BB not implemented yet\n"), exit(EXIT_FAILURE);
         break;

      default:
         rd->wc = rd->x2c - rd->x1c;
         rd->mean_lat_len = rd->wc * cos(rd->mean_lat * M_PI / 180);
         rd->hc = rd->y1c - rd->y2c;
   }
   rd->scale = (rd->mean_lat_len * 60.0 * 1852 * 100 / 2.54) / ((double) rd->w / (double) rd->dpi);

}


void init_rdata(struct rdata *rd)
{
   memset(rd, 0, sizeof(*rd));

   // A3 paper portrait (300dpi)
   //rd->w = 3507; rd->h = 4961; rd->dpi = 300;
   // A4 paper portrait (300dpi)
   rd->w = 2480; rd->h = 3507; rd->dpi = 300;
   // A4 paper landscape (300dpi)
   rd->h = 2480; rd->w = 3507; rd->dpi = 300;
   // A4 paper portrait (600dpi)
   //rd->w = 4961; rd->h = 7016; rd->dpi = 600;

   rd->grd.lat_ticks = rd->grd.lon_ticks = G_TICKS;
   rd->grd.lat_sticks = rd->grd.lon_sticks = G_STICKS;
   rd->grd.lat_g = rd->grd.lon_g = G_GRID;

   // this should be given by CLI arguments
   /* porec.osm
   rd->x1c = 13.53;
   rd->y1c = 45.28;
   rd->x2c = 13.63;
   rd->y2c = 45.183; */
   rd->x1c = 14.72;
   rd->y1c = 44.23;
   rd->x2c = 15.29;
   rd->y2c = 43.96;
}


int col_freq(struct rdata *rd, int x, int y, int w, int h, double a, int col)
{
   int x1, y1, rx, ry, c = 0;
   double r, b;

   //a = (360 - a + 90) * M_PI / 180;
   a = a * M_PI / 180;

   for (y1 = 0; y1 < h; y1++)
      for (x1 = 0; x1 < w; x1++)
      {
         r = sqrt(x1 * x1 + y1 * y1);
         b = atan((double) y1 / (double) x1);
         rx = r * cos(a - b);
         ry = r * sin(a - b);
         c += (col == gdImageGetPixel(rd->img, x + rx, y + ry));
      }

   //gdImageLine(rd->img, x, y, rx + x, ry + y, rd->col[BROWN]);
   //rx = c * cos(a) * 0.5;
   //ry = c * sin(a) * 0.5;
   //gdImageLine(rd->img, x, y, rx + x, ry + y, rd->col[BLACK]);

   return c;
}


int print_onode(FILE *f, const struct onode *nd)
{
   int i;

   switch (nd->nd.type)
   {
      case OSM_NODE:
         fprintf(f, "<node id=\"%ld\" version=\"%d\" lat=\"%f\" lon=\"%f\" uid=\"%d\">\n",
               nd->nd.id, nd->nd.ver, nd->nd.lat, nd->nd.lon, nd->nd.uid);
         break;

      case OSM_WAY:
         fprintf(f, "<way id=\"%ld\" version=\"%d\" uid=\"%d\">\n",
               nd->nd.id, nd->nd.ver, nd->nd.uid);
         break;

      default:
         fprintf(f, "<!-- unknown node type: %d -->\n", nd->nd.type);
         return -1;
   }

   switch (nd->nd.type)
   {
      case OSM_NODE:
         fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         fprintf(f, "</way>\n");
         break;
   }

   for (i = 0; i < nd->tag_cnt; i++)
      fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);

   for (i = 0; i < nd->ref_cnt; i++)
      fprintf(f, "<nd ref=\"%ld\"/>\n", nd->ref[i]);

   return 0;
}


void onode_stats(struct onode *nd, struct rdata *rd, struct dstats *ds)
{
   if (nd->nd.type == OSM_NODE)
   {
      if (ds->lu.lat < nd->nd.lat) ds->lu.lat = nd->nd.lat;
      if (ds->lu.lon > nd->nd.lon) ds->lu.lon = nd->nd.lon;
      if (ds->rb.lat > nd->nd.lat) ds->rb.lat = nd->nd.lat;
      if (ds->rb.lon < nd->nd.lon) ds->rb.lon = nd->nd.lon;
      if (ds->min_nid > nd->nd.id) ds->min_nid = nd->nd.id;
      if (ds->max_nid < nd->nd.id) ds->max_nid = nd->nd.id;
   }
   else if (nd->nd.type == OSM_WAY)
   {
      if (ds->min_wid > nd->nd.id) ds->min_wid = nd->nd.id;
      if (ds->max_wid < nd->nd.id) ds->max_wid = nd->nd.id;
   }
}


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0;
   struct stat st;
   FILE *f = stdout;
   char *cf = "rules.osm";
   //struct dstats ds;
   struct rdata *rd = &rdata_;

   init_rdata(&rdata_);
   //print_rdata(stderr, &rdata_);
   init_prj(&rdata_, PRJ_MERC_PAGE);
   print_rdata(stderr, &rdata_);

   if ((argc >= 2) && ((fd = open(argv[1], O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   fprintf(stderr, "reading osm input file (file size %ld kb)...\n", (long) st.st_size / 1024);
   (void) read_osm_file(ctl, &rdata_.nodes, &rdata_.ways);
   (void) close(fd);

   if ((fd = open(cf, O_RDONLY)) == -1)
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((cfctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   fprintf(stderr, "reading rules (file size %ld kb)...\n", (long) st.st_size / 1024);
   (void) read_osm_file(cfctl, &rdata_.nrules, &rdata_.wrules);
   (void) close(fd);

#ifdef MEM_USAGE
   fprintf(stderr, "tree memory used: %ld kb\n", (long) bx_sizeof() / 1024);
   fprintf(stderr, "onode memory used: %ld kb\n", (long) onode_mem() / 1024);
#endif

   fprintf(stderr, "gathering stats...\n");
   rd->ds.min_nid = rd->ds.min_wid = (int64_t) 0x7fffffffffffffff;
   rd->ds.max_nid = rd->ds.max_wid = (int64_t) 0x8000000000000000;
   rd->ds.lu.lat = -90;
   rd->ds.rb.lat = 90;
   rd->ds.lu.lon = 180;
   rd->ds.rb.lon = -180;
   traverse(rdata_.nodes, 0, (void (*)(struct onode *, struct rdata *, void *)) onode_stats, &rdata_, &rd->ds);
   traverse(rdata_.ways, 0, (void (*)(struct onode *, struct rdata *, void *)) onode_stats, &rdata_, &rd->ds);
   fprintf(stderr, "min_nid = %ld, max_nid = %ld, min_wid = %ld, max_wid = %ld, %.2f/%.2f x %.2f/%.2f\n",
         rd->ds.min_nid, rd->ds.max_nid, rd->ds.min_wid, rd->ds.max_wid,
         rd->ds.lu.lat, rd->ds.lu.lon, rd->ds.rb.lat, rd->ds.rb.lon);

   if ((rdata_.img = gdImageCreateTrueColor(rdata_.w, rdata_.h)) == NULL)
      perror("gdImage"), exit(EXIT_FAILURE);
   rdata_.col[WHITE] = gdImageColorAllocate(rdata_.img, 255, 255, 255);
   rdata_.col[BLACK] = gdImageColorAllocate(rdata_.img, 0, 0, 0);
   rdata_.col[YELLOW] = gdImageColorAllocate(rdata_.img, 231,209,74);
   rdata_.col[BLUE] = gdImageColorAllocate(rdata_.img, 137, 199, 178);
   rdata_.col[MAGENTA] = gdImageColorAllocate(rdata_.img, 120, 8, 44);
   rdata_.col[BROWN] = gdImageColorAllocate(rdata_.img, 154, 42, 2);

   gdImageFill(rdata_.img, 0, 0, rdata_.col[WHITE]);
   if (!gdFTUseFontConfig(1))
      fprintf(stderr, "fontconfig library not available\n");

   fprintf(stderr, "preparing rules...\n");
   traverse(rdata_.nrules, 0, prepare_rules, &rdata_, NULL);
   traverse(rdata_.wrules, 0, prepare_rules, &rdata_, NULL);

   fprintf(stderr, "preparing coastline...\n");
   cat_poly(&rdata_);

   //traverse(rdata_.nodes, 0, print_tree, &rdata_);
   //traverse(rdata_.ways, 0, print_tree, &rdata_);
   //fprintf(stderr, "rendering coastline (closed polygons)...\n");
   //traverse(rdata_.ways, 0, draw_coast, &rdata_, NULL);
   //traverse(rdata_.ways, 0, draw_coast_fill, &rdata_, NULL);

   fprintf(stderr, "rendering ways...\n");
   traverse(rdata_.wrules, 0, apply_wrules, &rdata_, NULL);
   fprintf(stderr, "rendering nodes...\n");
   traverse(rdata_.nrules, 0, apply_rules, &rdata_, NULL);

   //grid(&rdata_, rdata_.col[BLACK]);

   hpx_free(ctl);

   gdImagePng(rdata_.img, f);

   gdImageDestroy(rdata_.img);

   exit(EXIT_SUCCESS);
}

