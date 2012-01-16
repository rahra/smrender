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
#include <string.h>
#include <gd.h>
#include <syslog.h>
#include <errno.h>
#include <ctype.h>

#include "smrender.h"
#include "smlog.h"
#include "smrules.h"



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


int act_image(struct onode *nd, struct rdata *rd, struct orule *rl)
{
   int i, j, c, rx, ry, hx, hy, x, y;
   double a;

   mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &x, &y);
   hx = gdImageSX(rl->rule.img.img) / 2;
   hy = gdImageSY(rl->rule.img.img) / 2;

   a = isnan(rl->rule.img.angle) ? color_frequency(rd, x, y, hx, hy, rd->col[WHITE]) : 0;
   a = DEG2RAD(a);

   for (j = 0; j < gdImageSY(rl->rule.img.img); j++)
   {
      for (i = 0; i < gdImageSX(rl->rule.img.img); i++)
      {
         if (a != 0)
            rot_pos(i - hx, j - hy, a, &rx, &ry);
         else
         {
            rx = i - hx;
            ry = hy - j;
         }
         c = gdImageGetPixel(rl->rule.img.img, i, j);
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


double weight_angle(double a, double phase, double weight)
{
   return 0.5 * (cos((a + phase) * 2) + 1) * (1 - weight) + weight;
}


double color_frequency_w(struct rdata *rd, int x, int y, int w, int h, const struct auto_rot *rot)
{
   double a, ma = 0;
   int m = 0, mm = 0;

   // auto detect angle
   for (a = 0; a < 360; a += ANGLE_DIFF)
      {
         m = col_freq(rd, x, y, w, h, DEG2RAD(a), rot->autocol)
            * weight_angle(DEG2RAD(a), DEG2RAD(rot->phase), rot->weight);
         if (mm < m)
         {
            mm = m;
            ma = a;
         }
      }
   return ma;
}


double color_frequency(struct rdata *rd, int x, int y, int w, int h, int col)
{
   struct auto_rot rot = {0, rd->col[WHITE], 1};

   return color_frequency_w(rd, x, y, w, h, &rot);
}
 

#define POS_OFFSET MM2PX(1.3)
#define MAX_OFFSET MM2PX(2.0)
#define DIVX 3
int act_caption(struct onode *nd, struct rdata *rd, struct orule *rl)
{
   int br[8], n;
   char *s, *v;
   gdFTStringExtra fte;
   int x, y, rx, ry, ox, oy, off;
   double ma;

   if ((n = match_attr(nd, rl->rule.cap.key, NULL)) == -1)
   {
      //log_debug("node %ld has no caption tag '%s'", nd->nd.id, rl->rule.cap.key);
      return 0;
   }

   //if (nd->otag[n].v.buf[nd->otag[n].v.len])
   {
      //nd->otag[n].v.buf[nd->otag[n].v.len] = '\0';
      // data must be copied since memory modification is not allowed
      if ((v = malloc(nd->otag[n].v.len + 1)) == NULL)
         log_msg(LOG_ERR, "failed to copy caption string: %s", strerror(errno)), exit(EXIT_FAILURE);
      memcpy(v, nd->otag[n].v.buf, nd->otag[n].v.len);
      v[nd->otag[n].v.len] = '\0';
   }
   //else
   //   v = nd->otag[n].v.buf;

   if (rl->rule.cap.pos & POS_UC)
      for (x = 0; x < nd->otag[n].v.len; x++)
         v[x] = toupper(v[x]);

   mk_paper_coords(nd->nd.lat, nd->nd.lon, rd, &x, &y);
   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;
   gdImageStringFTEx(NULL, br, rl->rule.cap.col, rl->rule.cap.font, rl->rule.cap.size * 2.8699, 0, x, y, v, &fte);

   if (isnan(rl->rule.cap.angle))
   {
      ma = color_frequency_w(rd, x, y, br[4] - br[0] + MAX_OFFSET, br[1] - br[5], &rl->rule.cap.rot);
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
      ma = rl->rule.cap.angle;

      switch (rl->rule.cap.pos & 3)
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
      switch (rl->rule.cap.pos & 12)
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

   if ((s = gdImageStringFTEx(rd->img, br, rl->rule.cap.col, rl->rule.cap.font, rl->rule.cap.size * 2.8699, DEG2RAD(ma), x + rx, y - ry, v, &fte)) != NULL)
      log_msg(LOG_ERR, "error rendering caption: %s", s);

//   else
//      fprintf(stderr, "printed %s at %d,%d\n", nd->otag[n].v.buf, x, y);

   //if (nd->otag[n].v.buf[nd->otag[n].v.len])
      free(v);

   return 0;
}


int act_open_poly(struct onode *wy, struct rdata *rd, struct orule *rl)
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


int act_fill_poly(struct onode *wy, struct rdata *rd, struct orule *rl)
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

   if (rl->rule.draw.fill.used)
   {
      if (rl->rule.draw.fill.style != DRAW_TRANSPARENT)
         gdImageFilledPolygon(rd->img, p, wy->ref_cnt, rl->rule.draw.fill.col);
   }

   if (rl->rule.draw.border.used)
   {
      if (rl->rule.draw.border.style != DRAW_TRANSPARENT)
         gdImagePolygon(rd->img, p, wy->ref_cnt, rl->rule.draw.border.col);
   }

   return 0;
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


double rot_pos(int x, int y, double a, int *rx, int *ry)
{
   double r, b;

   r = sqrt(x * x + y * y);
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


int act_output(struct onode *nd, struct orule *rl)
{
   struct onode *n;
   int i;

   for (i = 0; i < nd->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, nd->ref[i])) == NULL)
         continue;
      print_onode(rl->rule.out.fhandle, n);
   }
   print_onode(rl->rule.out.fhandle, nd);

   return 0;
}

