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

int match_attr(const struct onode *nd, const char *k, const char *v)
{
   int i;

   for (i = 0; i < nd->tag_cnt; i++)
   {
      if (k != NULL)
      {
         if (!bs_cmp(nd->otag[i].k, k))
         {
            if (v == NULL)
               return i;
            if (!bs_cmp(nd->otag[i].v, v))
               return i;
         }
      }
      else
      {
         if (!bs_cmp(nd->otag[i].v, v))
            return i;
      }
   }

   return -1;
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

/*
void draw_nodes(struct onode *nd, struct rdata *rd)
{
   int x, y, i, j;

   if (match_attr(nd, rd->ev->rule->k, rd->ev->rule->v) == -1)
      return;

   fprintf(stderr, "node rule match\n");
   
   mkcoords(nd->nd.lat, nd->nd.lon, rd, &x, &y);
   x -= gdImageSX(rd->ev->img) / 2;
   y -= gdImageSY(rd->ev->img) / 2;

   for (j = 0; j < gdImageSY(rd->ev->img); j++)
   {
         for (i = 0; i < gdImageSX(rd->ev->img); i++)
         {
            if (gdTrueColorGetAlpha(gdImageGetPixel(rd->ev->img, i, j)))
               continue;
            gdImageSetPixel(rd->img, i + x, j + y, rd->col[BLACK]);
         }
   }
}
*/


void draw_coast_fill(struct onode *nd, struct rdata *rd)
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


void draw_coast(struct onode *nd, struct rdata *rd)
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


void traverse(const bx_node_t *nt, int d, void (*dhandler)(struct onode*, struct rdata*), struct rdata *rd)
{
   int i;

   if (d == 8)
   {
      if (nt->next[0] != NULL)
         dhandler(nt->next[0], rd);
      else
         fprintf(stderr, "*** this should not happen: NULL pointer catched\n");

      return;
   }

   for (i = 0; i < 256; i++)
      if (nt->next[i])
         traverse(nt->next[i], d + 1, dhandler, rd);

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

   if ((rdata_.img = gdImageCreate(rdata_.w, rdata_.h)) == NULL)
      perror("gdImage"), exit(EXIT_FAILURE);
   rdata_.col[WHITE] = gdImageColorAllocate(rdata_.img, 255, 255, 255);
   rdata_.col[BLACK] = gdImageColorAllocate(rdata_.img, 0, 0, 0);
   rdata_.col[YELLOW] = gdImageColorAllocate(rdata_.img, 231,209,74);
   rdata_.col[BLUE] = gdImageColorAllocate(rdata_.img, 137, 199, 178);
   rdata_.col[VIOLETT] = gdImageColorAllocate(rdata_.img, 120, 8, 44);


   //traverse(rdata_.nodes, 0, print_tree, &rdata_);
   //traverse(rdata_.ways, 0, print_tree, &rdata_);
   traverse(rdata_.ways, 0, draw_coast, &rdata_);
   traverse(rdata_.ways, 0, draw_coast_fill, &rdata_);


   //rdata_.ev = dummy_load();
   //traverse(rdata_.nodes, 0, draw_nodes, &rdata_);


   grid(&rdata_, rdata_.col[BLACK]);

   hpx_free(ctl);

   gdImagePng(rdata_.img, f);

   gdImageDestroy(rdata_.img);

   exit(EXIT_SUCCESS);
}

