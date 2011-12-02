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

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
//#include "seamark.h"
#include "smlog.h"
#include "bxtree.h"
#include "smrender.h"
//#include "smrules.h"


struct rdata rdata_;
int oline_ = 0;


void usage(const char *s)
{
   printf("Seamark renderer V1.0, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n\n");
}



char *cfmt(double c, int d, char *s, int l)
{
   switch (d)
   {
      case LAT:
         snprintf(s, l, "%02.0f %c %1.1f", fabs(c), c < 0 ? 'S' : 'N', (c - floor(c)) * 60.0);
         break;

      case LON:
         snprintf(s, l, "%03.0f %c %1.1f", fabs(c), c < 0 ? 'W' : 'E', (c - floor(c)) * 60.0);
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
   //int m = 0;
   //regex_t re;
   char buf[dst->len + 1];

   if (st == NULL)
      return bs_cmp2(dst, pat) == 0;

   if (st->type == SPECIAL_DIRECT)
      return bs_cmp2(dst, pat) == 0;

   if (st->type == SPECIAL_INVERT)
      return bs_cmp2(dst, pat) != 0;

   if ((st->type & ~SPECIAL_INVERT) == SPECIAL_REGEX)
   {
      // FIXME: this could be avoid if tags are 0-terminated.
      memcpy(buf, dst->buf, dst->len);
      buf[dst->len] = '\0';
 
      if (!regexec(&st->re, buf, 0, NULL, 0))
      {
         if (st->type & SPECIAL_INVERT)
            return 0;
         return 1;
      }
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

      if (kmatch && vmatch)
         return i;
   }

   return -1;
}


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


void mkcoords(double lat, double lon, struct rdata *rd, int *x, int *y)
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

   if ((n = match_attr(nd, mnd->rule.cap.key, NULL)) == -1)
   {
      fprintf(stderr, "node %ld has no caption tag '%s'\n", nd->nd.id, mnd->rule.cap.key);
      return 0;
   }
fprintf(stderr, "font: %s\n", mnd->rule.cap.font);
   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION;
   fte.hdpi = fte.vdpi = rd->dpi;

   nd->otag[n].v.buf[nd->otag[n].v.len] = '\0';
   if ((s = gdImageStringFTEx(rd->img, br, mnd->rule.cap.col, mnd->rule.cap.font, mnd->rule.cap.size * 2.8699, 0, x, y, nd->otag[n].v.buf, &fte)) != NULL)
      fprintf(stderr, "error rendering caption: %s\n", s);
   else
      fprintf(stderr, "printed %s at %d,%d\n", nd->otag[n].v.buf, x, y);

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

   fprintf(stderr, "node id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);
   mkcoords(nd->nd.lat, nd->nd.lon, rd, &x, &y);

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
      mkcoords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
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
      mkcoords(nd->nd.lat, nd->nd.lon, rd, &p[i].x, &p[i].y);
   }

   gdImageFilledPolygon(rd->img, p, wy->ref_cnt, rd->col[BLACK]);
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

   fprintf(stderr, "way id %ld rule match %ld\n", nd->nd.id, mnd->nd.id);

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
      mkcoords(node->nd.lat, node->nd.lon, rd, &p[j].x, &p[j].y);
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
            if ((c = gdImageGetPixel(rd->img, x, p[i].y)) != rd->col[BLACK])
               break;

         // fill area if it is not filled already
         if ((x < rd->w) && (c != rd->col[YELLOW]))
            //gdImageFillToBorder(rd->img, p[0].x + 1, p[0].y + 1, YELLOW, BLACK);
            gdImageFill(rd->img, x, p[i].y, rd->col[YELLOW]);
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
            if ((c = gdImageGetPixel(rd->img, x, p[i].y)) != rd->col[BLACK])
               break;

         // fill area if it is not filled already
         if ((x >= 0) && (c != rd->col[YELLOW]))
            gdImageFill(rd->img, x, p[i].y, rd->col[YELLOW]);
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
            if ((c = gdImageGetPixel(rd->img, p[i].x, y)) != rd->col[BLACK])
               break;

         // fill area if it is not filled already
         if ((y < rd->h) && (c != rd->col[YELLOW]))
            gdImageFill(rd->img, p[i].x, y, rd->col[YELLOW]);
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
            if ((c = gdImageGetPixel(rd->img, p[i].x, y)) != rd->col[BLACK])
               break;

         // fill area if it is not filled already
         if ((y >= 0) && (c != rd->col[YELLOW]))
            gdImageFill(rd->img, p[i].x, y, rd->col[YELLOW]);
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

   fprintf(stderr, "found coastline: %ld\n", nd->nd.id);

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
      mkcoords(node->nd.lat, node->nd.lon, rd, &p[j].x, &p[j].y);
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

   if (d == 8)
   {
      if (nt->next[0] != NULL)
         dhandler(nt->next[0], rd, p);
      else
         fprintf(stderr, "*** this should not happen: NULL pointer catched\n");

      return;
   }

   for (i = 0; i < 256; i++)
      if (nt->next[i])
         traverse(nt->next[i], d + 1, dhandler, rd, p);

   return;
}


void print_rdata(FILE *f, const struct rdata *rd)
{
   fprintf(f, "rdata:\nx1c = %.3f, y1c = %.3f, x2c = %.3f, y2c = %.3f\nmean_lat = %.3f, mean_lat_len = %.3f\nwc = %.3f, hc = %.3f\nw = %d, h = %d\ndpi = %d\nscale = 1:%.0f\n",
         rd->x1c, rd->y1c, rd->x2c, rd->y2c, rd->mean_lat, rd->mean_lat_len, rd->wc, rd->hc, rd->w, rd->h, rd->dpi, rd->scale);
}


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


void grid(struct rdata *rd, int col)
{
   double xt, yt, xn, yn, l;
   gdPoint p[2];
   char buf[100];

   xn = yn = 10;
   xt = rd->wc / xn;
   yt = rd->hc / yn;
   fprintf(stderr, "xticks_ = %f, yticks_ = %f, ", xt, yt);
   xt = ticks(rd->wc / xn);
   yt = ticks(rd->hc / yn);
   fprintf(stderr, "xticks = %f, yticks = %f\n", xt, yt);

   l = rd->y2c - fmod(rd->y2c, yt);
   for (; l <= rd->y1c; l += yt)
   {
      fprintf(stderr, "l = %s (%f)\n", cfmt(l, LAT, buf, sizeof(buf)), l);
      mkcoords(l, rd->x1c, rd, &p[0].x, &p[0].y);
      mkcoords(l, rd->x2c, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);
   }
   l = rd->x1c - fmod(rd->x1c, xt);
   for (; l <= rd->x2c; l += xt)
   {
      fprintf(stderr, "l = %s (%f)\n", cfmt(l, LON, buf, sizeof(buf)), l);
      mkcoords(rd->y2c, l, rd, &p[0].x, &p[0].y);
      mkcoords(rd->y1c, l, rd, &p[1].x, &p[1].y);
      gdImageOpenPolygon(rd->img, p, 2, col);
   }
}


void init_rdata(struct rdata *rd, int p)
{
   rd->mean_lat = (rd->y1c + rd->y2c) / 2;
   switch (p)
   {
      case PRJ_MERC_PAGE:
         rd->wc = rd->x2c - rd->x1c;
         rd->mean_lat_len = rd->wc * cos(rd->mean_lat * M_PI / 180);
         rd->hc = rd->mean_lat_len * rd->h / rd->w;
         rd->y1c = rd->mean_lat + rd->hc / 2.0;
         rd->y2c = rd->mean_lat - rd->hc / 2.0;
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


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl, *cfctl;
   int fd = 0;
   struct stat st;
   FILE *f = stdout;
   char *cf = "rules.osm";

   memset(&rdata_, 0, sizeof(rdata_));
   rdata_.x1c = 13.53;
   rdata_.y1c = 45.28;
   rdata_.x2c = 13.63;
   rdata_.y2c = 45.183;
   rdata_.w = 2480;
   rdata_.h = 3507;
   rdata_.dpi = 300;

   print_rdata(stderr, &rdata_);
   init_rdata(&rdata_, PRJ_MERC_PAGE);
   print_rdata(stderr, &rdata_);

   if ((argc >= 2) && ((fd = open(argv[1], O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   (void) read_osm_file(ctl, &rdata_.nodes, &rdata_.ways);
   (void) close(fd);

   if ((fd = open(cf, O_RDONLY)) == -1)
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((cfctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   (void) read_osm_file(cfctl, &rdata_.nrules, &rdata_.wrules);
   (void) close(fd);

   if ((rdata_.img = gdImageCreateTrueColor(rdata_.w, rdata_.h)) == NULL)
      perror("gdImage"), exit(EXIT_FAILURE);
   rdata_.col[WHITE] = gdImageColorAllocate(rdata_.img, 255, 255, 255);
   rdata_.col[BLACK] = gdImageColorAllocate(rdata_.img, 0, 0, 0);
   rdata_.col[YELLOW] = gdImageColorAllocate(rdata_.img, 231,209,74);
   rdata_.col[BLUE] = gdImageColorAllocate(rdata_.img, 137, 199, 178);
   rdata_.col[MAGENTA] = gdImageColorAllocate(rdata_.img, 120, 8, 44);

   gdImageFill(rdata_.img, 0, 0, rdata_.col[WHITE]);
   if (!gdFTUseFontConfig(1))
      fprintf(stderr, "fontconfig library not available\n");


   traverse(rdata_.nrules, 0, prepare_rules, &rdata_, NULL);
   traverse(rdata_.wrules, 0, prepare_rules, &rdata_, NULL);

   //traverse(rdata_.nodes, 0, print_tree, &rdata_);
   //traverse(rdata_.ways, 0, print_tree, &rdata_);
   traverse(rdata_.ways, 0, draw_coast, &rdata_, NULL);
   traverse(rdata_.ways, 0, draw_coast_fill, &rdata_, NULL);


   //rdata_.ev = dummy_load();
   //traverse(rdata_.nodes, 0, apply_rules, &rdata_, NULL);
   traverse(rdata_.wrules, 0, apply_wrules, &rdata_, NULL);
   traverse(rdata_.nrules, 0, apply_rules, &rdata_, NULL);


   grid(&rdata_, rdata_.col[BLACK]);

   hpx_free(ctl);

   gdImagePng(rdata_.img, f);

   gdImageDestroy(rdata_.img);

   exit(EXIT_SUCCESS);
}

