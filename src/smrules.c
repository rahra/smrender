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
#include <syslog.h>
#include <errno.h>
#include <ctype.h>

#include "smrender.h"
#include "smlog.h"
#include "smrules.h"
#include "smcoast.h"


void init_main_image(struct rdata *rd, const char *bg)
{
   // preparing image
   if ((rd->img = gdImageCreateTrueColor(rd->w, rd->h)) == NULL)
      perror("gdImage"), exit(EXIT_FAILURE);
   rd->col[WHITE] = gdImageColorAllocate(rd->img, 255, 255, 255);
   rd->col[BLACK] = gdImageColorAllocate(rd->img, 0, 0, 0);
   rd->col[YELLOW] = gdImageColorAllocate(rd->img, 231,209,74);
   rd->col[BLUE] = gdImageColorAllocate(rd->img, 137, 199, 178);
   rd->col[MAGENTA] = gdImageColorAllocate(rd->img, 120, 8, 44);
   rd->col[BROWN] = gdImageColorAllocate(rd->img, 154, 42, 2);
   rd->col[TRANSPARENT] = gdTransparent;

   rd->col[BGCOLOR] = bg == NULL ? rd->col[WHITE] : parse_color(rd, bg);
   log_msg(LOG_DEBUG, "background color is set to 0x%08x", rd->col[BGCOLOR]);
   gdImageFill(rd->img, 0, 0, rd->col[BGCOLOR]);

#define gdImageFTUseFontConfig gdFTUseFontConfig
   if (!gdImageFTUseFontConfig(1))
      log_msg(LOG_NOTICE, "fontconfig library not available");
}


void save_main_image(struct rdata *rd, FILE *f)
{
   gdImagePng(rd->img, f);
   gdImageDestroy(rd->img);
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
 

int act_cap_ini(smrule_t *r)
{
   struct actCaption cap;
   char *s;

   memset(&cap, 0, sizeof(cap));

   if ((cap.font = get_param("font", NULL, &r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'font' missing");
      return 1;
   }
   if (get_param("size", &cap.size, &r->act) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'size' missing");
      return 1;
   }
   if ((cap.key = get_param("key", NULL, &r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'key' missing");
      return 1;
   }
   if (*cap.key == '*')
   {
      cap.key++;
      cap.pos |= POS_UC;
   }
   if ((s = get_param("color", NULL, &r->act)) != NULL)
      cap.col = parse_color(get_rdata(), s);
   if ((s = get_param("angle", &cap.angle, &r->act)) != NULL)
   {
      if (!strcmp(s, "auto"))
      {
         cap.angle = NAN;
         if ((s = get_param("auto-color", NULL, &r->act)) != NULL)
         {
            cap.rot.autocol = parse_color(get_rdata(), s);
         }
         if ((s = get_param("weight", &cap.rot.weight, &r->act)) == NULL)
            cap.rot.weight = 1;
         (void) get_param("weight", &cap.rot.phase, &r->act);
      }
   }
   if ((s = get_param("align", NULL, &r->act)) != NULL)
   {
      if (!strcmp(s, "east"))
         cap.pos |= POS_E;
      else if (!strcmp(s, "west"))
         cap.pos |= POS_W;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }
   if ((s = get_param("halign", NULL, &r->act)) != NULL)
   {
      if (!strcmp(s, "north"))
         cap.pos |= POS_N;
      else if (!strcmp(s, "south"))
         cap.pos |= POS_S;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }

   if ((r->data = malloc(sizeof(cap))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   log_msg(LOG_DEBUG, "%04x, %08x, '%s', '%s', %.1f, %.1f, {%.1f, %08x, %.1f}",
         cap.pos, cap.col, cap.font, cap.key, cap.size, cap.angle,
         cap.rot.phase, cap.rot.autocol, cap.rot.weight);
   memcpy(r->data, &cap, sizeof(cap));
   return 0;
}


#define POS_OFFSET MM2PX(1.3)
#define MAX_OFFSET MM2PX(2.0)
#define DIVX 3


int cap_node(smrule_t *r, osm_node_t *n)
{
   struct actCaption *cap = r->data;
   struct rdata *rd = get_rdata();
   int br[8], m;
   char *s, *v;
   gdFTStringExtra fte;
   int x, y, rx, ry, ox, oy, off;
   double ma;

   if ((m = match_attr((osm_obj_t*) n, cap->key, NULL)) == -1)
   {
      //log_debug("node %ld has no caption tag '%s'", nd->nd.id, rl->rule.cap.key);
      return 0;
   }

   if ((v = malloc(n->obj.otag[m].v.len + 1)) == NULL)
      log_msg(LOG_ERR, "failed to copy caption string: %s", strerror(errno)), exit(EXIT_FAILURE);
   memcpy(v, n->obj.otag[m].v.buf, n->obj.otag[m].v.len);
   v[n->obj.otag[m].v.len] = '\0';

   if (cap->pos & POS_UC)
      for (x = 0; x < n->obj.otag[m].v.len; x++)
         v[x] = toupper(v[x]);

   mk_paper_coords(n->lat, n->lon, rd, &x, &y);
   memset(&fte, 0, sizeof(fte));
   fte.flags = gdFTEX_RESOLUTION | gdFTEX_CHARMAP;
   fte.charmap = gdFTEX_Unicode;
   fte.hdpi = fte.vdpi = rd->dpi;
   gdImageStringFTEx(NULL, br, cap->col, cap->font, cap->size * 2.8699, 0, x, y, v, &fte);

   if (isnan(cap->angle))
   {
      ma = color_frequency_w(rd, x, y, br[4] - br[0] + MAX_OFFSET, br[1] - br[5], &cap->rot);
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
      ma = cap->angle;

      switch (cap->pos & 3)
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
      switch (cap->pos & 12)
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

   if ((s = gdImageStringFTEx(rd->img, br, cap->col, cap->font, cap->size * 2.8699, DEG2RAD(ma), x + rx, y - ry, v, &fte)) != NULL)
      log_msg(LOG_ERR, "error rendering caption: %s", s);

   free(v);
   return 0;
}


int cap_way(smrule_t *r, osm_way_t *w)
{
   struct actCaption *cap = r->data;
   struct actCaption tmp_cap;
   struct rdata *rd = get_rdata();
   struct coord c;
   double ar;
   osm_node_t *n;
   smrule_t *tmp_rule;
   int e;

   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return 0;

   if ((tmp_rule = malloc(sizeof(smrule_t) + sizeof(struct stag) * r->act.tag_cnt)) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc temp rule: %s", strerror(errno));
      return -1;
   }

   memcpy(tmp_rule, r, sizeof(smrule_t) + sizeof(struct stag) * r->act.tag_cnt);

   n = malloc_node(w->obj.tag_cnt);
   memcpy(n->obj.otag, w->obj.otag, sizeof(struct otag) * w->obj.tag_cnt);
   n->lat = c.lat;
   n->lon = c.lon;
   memcpy(&tmp_cap, cap, sizeof(tmp_cap));
   tmp_rule->data = &tmp_cap;
   tmp_cap.size = 100 * sqrt(fabs(ar) / (rd->mean_lat_len * rd->hc * 3600));
#define MIN_AUTO_SIZE 0.7
#define MAX_AUTO_SIZE 12.0
   if (tmp_cap.size < MIN_AUTO_SIZE) tmp_cap.size = MIN_AUTO_SIZE;
   if (tmp_cap.size > MAX_AUTO_SIZE) tmp_cap.size = MAX_AUTO_SIZE;
   //log_debug("r->rule.cap.size = %f (%f 1/1000)", r->rule.cap.size, r->rule.cap.size / 100 * 1000);

   e = cap_node(tmp_rule, n);
   free_obj((osm_obj_t*) n);

   return e;
}


int act_cap(smrule_t *r, osm_obj_t *o)
{
   switch (o->type)
   {
      case OSM_NODE:
         return cap_node(r, (osm_node_t*) o);

      case OSM_WAY:
         return cap_way(r, (osm_way_t*) o);
   }
   log_msg(LOG_WARN, "type %d not implemented yet", o->type);
   return -1;
}


int act_cap_fini(smrule_t *r)
{
   if (r->data != NULL)
   {
      free(r->data);
      r->data = NULL;
   }
   return 0;
}


int poly_mpcoords(osm_way_t *w, struct rdata *rd, gdPoint *p)
{
   int i;
   osm_node_t *n;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
         return -1;

      mk_paper_coords(n->lat, n->lon, rd, &p[i].x, &p[i].y);
   }
   return 0;
}


int set_style(struct rdata *rd, int style, int col)
{
#define MAX_STYLE_BUF 300
#define STYLE_SHORT_LEN 0.4
#define STYLE_LONG_LEN 1.2
   static int sdef[MAX_STYLE_BUF];
   int len, i;

   if (MM2PX(STYLE_LONG_LEN) + MM2PX(STYLE_SHORT_LEN) >= MAX_STYLE_BUF)
   {
      log_msg(LOG_CRIT, "style buffer to small for %d dpi, increase MAX_STYLE_BUF", (int) rd->dpi);
      return -1;
   }

   switch (style)
   {
      case DRAW_SOLID:
         sdef[0] = col;
         len = 1;
         break;

      case DRAW_DOTTED:
         len = 0;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = col;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = gdTransparent;
         break;

      case DRAW_DASHED:
         len = 0;
         for (i = 0; i < MM2PX(STYLE_LONG_LEN); i++, len++)
            sdef[len] = col;
         for (i = 0; i < MM2PX(STYLE_SHORT_LEN); i++, len++)
            sdef[len] = gdTransparent;
         break;

      default:
         log_msg(LOG_EMERG, "unknown drawing style %d!", style);
         return -1;
   }

   gdImageSetStyle(rd->img, sdef, len);
   return 0;
}

#if 0
int act_open_poly(osm_way_t *w, struct rdata *rd, struct orule *rl)
{
   int e, t, c;
   gdPoint p[w->ref_cnt];

   if ((e = poly_mpcoords(w, rd, p)))
      return e;

   // save thickness
   e = rd->img->thick;

   if (rl->rule.draw.border.used && (rl->rule.draw.border.style != DRAW_TRANSPARENT))
   {

      if (!(t = MM2PX(rl->rule.draw.border.width + rl->rule.draw.fill.width * (rl->rule.draw.fill.used != 0))))
         t = 1;

      gdImageSetThickness(rd->img, t);
      gdImageSetAntiAliased(rd->img, rl->rule.draw.border.col);
      // this is a bugfix for libgd: antialised lines with a thickness > 1 do not work
      c = t > 1 ? rl->rule.draw.border.col : gdAntiAliased;
      if (rl->rule.draw.border.style == DRAW_SOLID)
      {
         gdImageOpenPolygon(rd->img, p, w->ref_cnt, c);
      }
      else
      {
         (void) set_style(rd, rl->rule.draw.border.style, c);
         gdImageOpenPolygon(rd->img, p, w->ref_cnt, gdStyled);
      }
   }

   if (rl->rule.draw.fill.used && (rl->rule.draw.fill.style != DRAW_TRANSPARENT))
   {
      if (!(t = MM2PX(rl->rule.draw.fill.width - rl->rule.draw.border.width * (rl->rule.draw.border.used != 0))))
         t = 1;

      gdImageSetThickness(rd->img, t);
      gdImageSetAntiAliased(rd->img, rl->rule.draw.fill.col);
      // this is a bugfix for libgd: antialised lines with a thickness > 1 do not work
      c = t > 1 ? rl->rule.draw.fill.col : gdAntiAliased;
      if (rl->rule.draw.fill.style == DRAW_SOLID)
      {
         gdImageOpenPolygon(rd->img, p, w->ref_cnt, c);
      }
      else
      {
         (void) set_style(rd, rl->rule.draw.fill.style, c);
         gdImageOpenPolygon(rd->img, p, w->ref_cnt, gdStyled);
      }
   }

   // restore thickness
   gdImageSetThickness(rd->img, e);

   return 0;
}


int act_fill_poly(osm_way_t *w, struct rdata *rd, struct orule *rl)
{
   int e, t, c;
   gdPoint p[w->ref_cnt];

   if ((e = poly_mpcoords(w, rd, p)))
      return e;

   // save thickness
   e = rd->img->thick;

   if (rl->rule.draw.fill.used && (rl->rule.draw.fill.style != DRAW_TRANSPARENT))
   {
      gdImageSetAntiAliased(rd->img, rl->rule.draw.fill.col);
      gdImageFilledPolygon(rd->img, p, w->ref_cnt, gdAntiAliased);
   }

   if (rl->rule.draw.border.used && (rl->rule.draw.border.style != DRAW_TRANSPARENT))
   {
      if (!(t = MM2PX(rl->rule.draw.border.width)))
         t = 1;

      gdImageSetThickness(rd->img, t);
      gdImageSetAntiAliased(rd->img, rl->rule.draw.border.col);
      // this is a bugfix for libgd: antialised lines with a thickness > 1 do not work
      c = t > 1 ? rl->rule.draw.fill.col : gdAntiAliased;
      if (rl->rule.draw.border.style == DRAW_SOLID)
      {
         gdImageOpenPolygon(rd->img, p, w->ref_cnt, c);
      }
      else
      {
         (void) set_style(rd, rl->rule.draw.border.style, c);
         gdImagePolygon(rd->img, p, w->ref_cnt, gdStyled);
      }
   }

   // restore thickness
   gdImageSetThickness(rd->img, e);

   return 0;
}
#endif


int act_img_ini(smrule_t *r)
{
   struct actImage img;
   char *name;
   FILE *f;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_WARN, "img() only applicable to nodes");
      return -1;
   }

   if ((name = get_param("file", NULL, &r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return -1;
   }

   if ((f = fopen(name, "r")) == NULL)
   {
      log_msg(LOG_WARN, "cannot open file %s: %s", name, strerror(errno));
      return -1;
   }

   memset(&img, 0, sizeof(img));

   img.img = gdImageCreateFromPng(f);
   (void) fclose(f);

   if (img.img == NULL)
   {
      log_msg(LOG_WARN, "could not read PNG from %s", name);
      return -1;
   }

   if ((name = get_param("angle", &img.angle, &r->act)) != NULL)
   {
      if (!strcmp(name, "auto"))
      {
         img.angle = NAN;
         // FIXME: additional auto-rotation settings should be parsed here
      }
   }
   
   if ((r->data = malloc(sizeof(img))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memcpy(r->data, &img, sizeof(img));

   return 0;
}


int act_img(smrule_t *r, osm_node_t *n)
{
   struct actImage *img = r->data;
   struct rdata *rd = get_rdata();
   int hx, hy, x, y;
   double a;

   mk_paper_coords(n->lat, n->lon, rd, &x, &y);
   hx = gdImageSX(img->img) / 2;
   hy = gdImageSY(img->img) / 2;
   a = isnan(img->angle) ? color_frequency(rd, x, y, hx, hy, rd->col[WHITE]) : 0;

   gdImageCopyRotated(rd->img, img->img, x, y, 0, 0,
         gdImageSX(img->img), gdImageSY(img->img), round(a));

   return 0;
}


int act_img_fini(smrule_t *r)
{
   struct actImage *img = r->data;

   if (img != NULL && img->img != NULL)
      gdImageDestroy(img->img);

   free (img);
   r->data = NULL;

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
#if 0
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
#endif


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


int act_draw_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
   struct actDraw *d;
   double a;
   char *s;

   // just to be on the safe side
   if (r->oo->type != OSM_WAY)
   {
      log_msg(LOG_WARN, "'draw' may be applied to ways only");
      return 1;
   }

   if ((d = malloc(sizeof(*d))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memset(d, 0, sizeof(*d));
   r->data = d;

   // parse fill settings
   if ((s = get_param("color", NULL, &r->act)) != NULL)
   {
      d->fill.col = parse_color(rd, s);
      d->fill.used = 1;
   }
   if (get_param("width", &d->fill.width, &r->act) == NULL)
      d->fill.width = 0;
   d->fill.style = parse_style(get_param("style", NULL, &r->act));

   // parse border settings
   if ((s = get_param("bcolor", NULL, &r->act)) != NULL)
   {
      d->border.col = parse_color(rd, s);
      d->border.used = 1;
   }
   if (get_param("bwidth", &d->border.width, &r->act) == NULL)
      d->border.width = 0;
   d->border.style = parse_style(get_param("bstyle", NULL, &r->act));

   // honor direction of ways
   if (get_param("directional", &a, &r->act) == NULL)
      a = 0;
   d->directional = a != 0;

   if (get_param("ignore_open", &a, &r->act) == NULL)
      a = 0;
   d->collect_open = a == 0;

   d->wl = init_wlist();

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

   return 0;
}


int act_draw(smrule_t *r, osm_obj_t *o)
{
   struct actDraw *d = r->data;

   if (!d->collect_open && !is_closed_poly((osm_way_t*) o))
      return 0;

   return gather_poly0((osm_way_t*) o, &d->wl);
}


void poly_fill(struct rdata *rd, gdImage *img, osm_way_t *w, int fg, int bg, int cw, int thick)
{
   int e, t;
   gdPoint p[w->ref_cnt];

   if ((e = poly_mpcoords(w, rd, p)))
   {
      log_msg(LOG_CRIT, "poly_mpcoords returned %d, skipping", e);
      return;
   }

   if (is_closed_poly(w))
   {
      gdImageFilledPolygon(img, p, w->ref_cnt, cw ? bg : gdAntiAliased);
   }
   else
   {
      t = img->thick;
      gdImageSetThickness(img, thick);
      gdImageOpenPolygon(img, p, w->ref_cnt, thick > 1 ? fg : gdAntiAliased);
      gdImageSetThickness(img, t);
   }
}

 
int act_draw_fini(smrule_t *r)
{
   struct actDraw *d = r->data;
   struct rdata *rd;
   struct coord c;
   gdImage *img;
   int fg, bg;
   int i;
 
   if (!d->wl->ref_cnt)
   {
      free(d->wl);
      return 1;
   }

   // FIXME: we need this only if 'directional'
   for (i = 0; i < d->wl->ref_cnt; i++)
   {
      if (is_closed_poly(d->wl->ref[i].w))
      {
         poly_area(d->wl->ref[i].w, &c, &d->wl->ref[i].area);
         if (d->wl->ref[i].area < 0)
         {
            d->wl->ref[i].area = fabs(d->wl->ref[i].area);
            d->wl->ref[i].cw = d->directional;
         }
      }
   }

   if (d->directional)
      qsort(d->wl->ref, d->wl->ref_cnt, sizeof(struct poly), (int(*)(const void *, const void *)) compare_poly_area);

   rd = get_rdata();

   if (d->fill.used)
   {
      img = gdImageCreateTrueColor(gdImageSX(rd->img), gdImageSY(rd->img));
      bg = rd->col[WHITE];
      fg = d->fill.col;
      gdImageColorTransparent(img, bg);
      gdImageSetAntiAliased(img, fg);
      gdImageFilledRectangle(img, 0, 0, gdImageSX(img), gdImageSY(img), d->wl->ref[0].cw ? gdAntiAliased : bg);

      for (i = 0; i < d->wl->ref_cnt; i++)
      poly_fill(rd, img, d->wl->ref[i].w, fg, bg, d->wl->ref[i].cw, d->fill.width > 0 ? MM2PX(d->fill.width) : 1);

      gdImageCopy(rd->img, img, 0, 0, 0, 0, gdImageSX(img), gdImageSY(img));
      gdImageDestroy(img);
   }

   free(d->wl);
   free(d);
   r->data = NULL;

   return 0;
}


int act_templ(smrule_t *r, osm_obj_t *o)
{
   return 0;
}

