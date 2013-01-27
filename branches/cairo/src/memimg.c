/* Copyright 2012 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This files contains the code for memory based image processing.
 *  It is used for region comparison and "rectification" of image parts. This
 *  is necessary for the auto-rotation functions.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef HAVE_GD
#include <gd.h>
#endif
#ifdef HAVE_CAIRO
#include <cairo.h>
#endif
#include <pthread.h>
#include <signal.h>

#include "memimg.h"
#include "smrender.h"


#ifdef WITH_THREADS
#define MI_THREADS 4
#endif
#define MI_TH_RR(x, y) ((x + y) % MI_THREADS)


struct mi_thread_param
{
   pthread_t th;
   pthread_cond_t *boss, worker;
   pthread_mutex_t *mutex;
   mem_img_t *dst, *src;
   struct diff_vec *dv;
   int xoff;
   int res;
   int status;
};


char *bar(double);


static int color_mix(int c1, int c2)
{
   int r, g, b, a, c;

   //printf("OPQ_COMP(c1) = %.2f r = %.2f ", OPQ_COMP(c1), RED_COMP(c1) * OPQ_COMP(c1) + RED_COMP(c2) * OPQ_COMP(c2));
   r = round((RED_COMP(c1) * OPQ_COMP(c1) + RED_COMP(c2) * OPQ_COMP(c2)) * 255.0 / 2.0);
   g = round((GRN_COMP(c1) * OPQ_COMP(c1) + GRN_COMP(c2) * OPQ_COMP(c2)) * 255.0 / 2.0);
   b = round((BLU_COMP(c1) * OPQ_COMP(c1) + BLU_COMP(c2) * OPQ_COMP(c2)) * 255.0 / 2.0);
   a = round(((TRN_COMP(c1) + TRN_COMP(c2))) * 255.0 / 2.0);

   c = a << 24 | r << 16 | g << 8 | b;

   //printf("mix %08x | %08x = %08x\n", c1, c2, c);

   return c;
}


double inline color_compare(int c1, int c2)
{
   return (
         SQR(RED_COMP(c1) - RED_COMP(c2)) +
         SQR(GRN_COMP(c1) - GRN_COMP(c2)) +
         SQR(BLU_COMP(c1) - BLU_COMP(c2))
         ) / 3.0;
}


int color_comp(double d)
{
   int c;

   c = round(d * CMUL);
   if (c > CMUL) c = CMUL;
   if (c < 0) c = 0;

   return c;
}


int rb_color(double r, double b)
{
   int R, B;

   R = color_comp(r);
   B = color_comp(b);

   return R << 16 | B;
}


int grey_color(double d)
{
   int c;

   c = color_comp(d);

   return c << 16 | c << 8 | c;
}


void mi_free(mem_img_t *mi)
{
   free(mi);
}


mem_img_t *mi_from_gdimage(image_t *img)
{
   mem_img_t *mi;
   int i;

#ifdef HAVE_CAIRO
   unsigned char *d;
   int x, y, w;

   cairo_surface_flush(img);
   if ((d = cairo_image_surface_get_data(img)) == NULL)
      return NULL;

   if ((mi = mi_create(cairo_image_surface_get_width(img), cairo_image_surface_get_height(img))) == NULL)
      return NULL;

   w = cairo_image_surface_get_stride(img);

   for (y = 0, i = 0; y < cairo_image_surface_get_height(img); y++)
      for (x = 0; x < cairo_image_surface_get_width(img); x++, i++)
         mi->p[i] = *((uint32_t*) (d + x * sizeof(uint32_t) + y * w));
#endif

#ifdef HAVE_GD
   if ((mi = mi_create(gdImageSX(img), gdImageSY(img))) == NULL)
      return NULL;

   for (i = 0; i < mi->w * mi->h; i++)
      mi->p[i] = gdImageGetPixel(img, i % mi->w, i / mi->w);
#endif

   return mi;
}


image_t *mi_to_gdimage(mem_img_t *mi)
{
   image_t *img;

#ifdef HAVE_GD
   int i;

   if ((img = gdImageCreateTrueColor(mi->w, mi->h)) == NULL)
      return NULL;

   gdImageSaveAlpha(img, 1);
   gdImageAlphaBlending(img, 0);

   for (i = 0; i < mi->w * mi->h; i++)
      gdImageSetPixel(img, i % mi->w, i / mi->w, mi->p[i]);
#endif

   return img;
}


void mi_init_plane(mem_img_t *mi, int c)
{
   int i;

   for (i = 0; i < mi->w * mi->h; i++)
      mi->p[i] = c;
}


mem_img_t *mi_create(int w, int h)
{
   mem_img_t *mi;

   if ((mi = malloc(w * h * sizeof(*mi->p) + sizeof(*mi))) == NULL)
      return NULL;

   mi->w = w;
   mi->h = h;
   mi->p = (pixel_t*) (mi + 1);

   return mi;
}


int inline mi_getpixel(const mem_img_t *mi, int x, int y)
{
   if ((y < 0) || (y >= mi->h) || (x < 0) || (x >= mi->w)) return -1;
   return mi->p[y * mi->w + x];
}


void inline mi_setpixel(mem_img_t *mi, int x, int y, int c)
{
   if ((y < 0) || (y >= mi->h) || (x < 0) || (x >= mi->w)) return;
   mi->p[y * mi->w + x] = c;
}


void mi_remove_blind(mem_img_t *mi)
{
   int x, y, c, d;

   // remove blind dots
   for (x = 0; x < mi->w; x++)
      for (y = 0; y < mi->h; y++)
      {
         if (!y && (mi_getpixel(mi, x, y) == -1) && (mi->h > 1))
         {
            // FIXME: y + 1 may be out of bounds
            if ((c = mi_getpixel(mi, x, y + 1)) != -1)
               mi_setpixel(mi, x, y, c);
            continue;
         }
         if ((y == mi->h - 1) && (mi_getpixel(mi, x, y) == -1) && y)
         {
            // FIXME: y - 1 may be out of bounds
            if ((c = mi_getpixel(mi, x, y - 1)) != -1)
               mi_setpixel(mi, x, y, c);
            continue;
         }
         if (mi_getpixel(mi, x, y) == -1)
         {
            c = mi_getpixel(mi, x, y - 1);
            d = mi_getpixel(mi, x, y + 1);
            if ((c != -1) && (d != -1))
               mi_setpixel(mi, x, y, color_mix(c, d));
            else if (c != -1)
               mi_setpixel(mi, x, y, c);
            else if (d != -1)
               mi_setpixel(mi, x, y, d);
         }
      }
}


mem_img_t *rectify_circle(image_t *img, int cx, int cy, int R)
{
   mem_img_t *mi;
   double U, x0, y0, fi, l;
   int x, y, yl, c, maxY, w, h, s;
   unsigned char * d;

   U = 2.0 * M_PI * R;
   maxY = round(U);
   mi = mi_create(R, maxY);
   mi_init_plane(mi, -1);

#ifdef HAVE_GD
   w = gdImageSX(img);
   h = gdImageSY(img);
#endif
#ifdef HAVE_CAIRO
   w = cairo_image_surface_get_width(img);
   h = cairo_image_surface_get_height(img);
   d = cairo_image_surface_get_data(img);
   s = cairo_image_surface_get_stride(img);
   cairo_surface_flush(img);
#endif

   for (y = -R; y < R; y++)
   {
      for (x = -R; x < R; x++)
      {
         x0 = hypot(x, y);
         if (x0 <= R)
         {
            fi = atan2(y, x);
            if (fi < 0.0) fi += 2.0 * M_PI;
            y0 = fi * R;
            l = (x0 != 0.0 ? R / x0 : U) / 2.0;
            if ((x + cx) < 0 || (x + cx) >= w || (cy - y) < 0 || (cy - y) >= h)
               c = 0x7f000000;
            else
#ifdef HAVE_CAIRO
               c = *((uint32_t*) (d + cro_pixel_pos(x + cx, cy - y, s)));
#endif
#ifdef HAVE_GD
               c = gdImageGetPixel(img, x + cx, cy - y);
#endif
            for (yl = round(y0 - l); yl < round(y0 + l); yl++)
               mi_setpixel(mi, round(x0), maxY - yl - 1, c);
         }
      }
   }

   mi_remove_blind(mi);

   return mi;
}


mem_img_t *mi_from_diff_vec(const struct diff_vec *dv, int len, int xvar)
{
   mem_img_t *mi;
   double a, r;
   int x, y, R, i, j;

   mi = mi_create(len + xvar - 1, len + xvar - 1);
   mi_init_plane(mi, 0x7f000000);

   R = (len  + xvar - 1) >> 1;

   for (x = -R; x < R; x++)
      for (y = -R; y < R; y++)
      {
         r = hypot(x, y);
         //printf("x/y = %d/%d, r = %.2f\n", x, y, r);
         if (round(r) <= R)
         {
            a = atan2(y, x);
            if (a < 0.0) a += 2.0 * M_PI;
            i = round((a / (2.0 * M_PI)) * (len - 1));
            if (i >= len) i = len - 1;
            if (i < 0) i = 0;
            j = round(r / R * (xvar - 1));
            if (j >= xvar) j = xvar - 1;
            if (j < 0) j = 0;
            //printf("x = %d, y = %d, r = %f, a = %f, i = %d, j = %d\n", x, y, r, a, i, j);
            mi_setpixel(mi, x + R, y + R, grey_color(dv[i + j * len].dv_diff));
            //mi_setpixel(mi, x + R, y + R, rb_color(a / (2.0 * M_PI), dv[i + j * len].dv_diff));
         }
      }
   
   return mi;
}


/*! Compare the small image src to a region within the larger image dst.
 *  @param dst Destionation image (background).
 *  @param src Source image.
 *  @param x Horizontal coordinate of left uper corner within dst.
 *  @param y Vertical coordinate of left upper corner within dst.
 *  @param xwrap If the source image is wider than the destination and xwrap is
 *  1, the src is wrapped around as of dst would be a cylinder.
 *  @param ywrap If the source image is higher than the destination and ywrap is
 *  1, the src is wrapped around as of dst would be a cylinder.
 *  @return The function returns a value between 0.0 and 1.0. The latter means
 *  "total difference".
 */
double mi_cmp_region(const mem_img_t *dst, const mem_img_t *src, int x, int y, int xwrap, int ywrap)
{
   double diff = 0.0, trans_wgt, c;
   int cmp_w, cmp_h;
   int p[2], x0, y0;

   // check boundary of comparison (src not wider than x + dst)
   cmp_w = !xwrap && x + src->w >= dst->w ? dst->w - x : src->w;
   cmp_h = !ywrap && y + src->h >= dst->h ? dst->h - y : src->h;

   trans_wgt = cmp_w * cmp_h;

//#define DIRECT_ADDRESSING
#ifdef DIRECT_ADDRESSING
   int i, start;
   // start address within dst
   start = y * dst->w;
   for (i = 0; i < cmp_w * cmp_h; i++)
   {
      x0 = i % cmp_w;
      y0 = i / cmp_w;
      p[0] = src->p[x0 + y0 * src->w];
      p[1] = dst->p[((x + x0) % dst->w) + ((y + y0) & dst->h) * dst->w];
#else
   for (y0 = 0; y0 < cmp_h; y0++)   
   for (x0 = 0; x0 < cmp_w; x0++)
   {
#endif
      p[0] = mi_getpixel(src, x0, y0);
      p[1] = mi_getpixel(dst, (x + x0) % dst->w, (y + y0) % dst->h);
      //trans_wgt -= (TRN_COMP(p[0]) + TRN_COMP(p[1])) * 0.5;
      trans_wgt -= fmax(TRN_COMP(p[0]), TRN_COMP(p[1]));
      c = color_compare(p[0], p[1]);
      c *= 1.0 - (TRN_COMP(p[0]) + TRN_COMP(p[1])) * 0.5;
      diff += c;
      //printf("x0/y0 = %d/%d, s %08x - d %08x, trans_wgt = %.3f, diff = %.3f, c = %.3f\n", x0, y0, p[0], p[1], trans_wgt, diff, c);
   }

   //printf("   diff = %.3f, trans_wgt = %.3f, ", diff, trans_wgt);
   //return diff / trans_wgt;
   return diff / (cmp_w * cmp_h);
}


/*! Comparision function to compare to struct diff_vec structures.
 *  @return 1 if src->diff is less than dst->diff, -1 if src->diff is greater
 *  that dst->diff and 0 if both are equal.
 */
int cmp_dv(const struct diff_vec *src, const struct diff_vec *dst)
{
   if (src->dv_diff > dst->dv_diff)
      return -1;
   if (src->dv_diff < dst->dv_diff)
      return 1;
   return 0;
}


/*! This function returns a vecor of struct diff_vec with dst->h (height of
 * destination image) entries. It compares to source image each "line" of the
 * destination image. The comparision itself is done by mi_cmp_region(). The
 * diff_vec structure must point to a valid memory region with at least dst->h
 * entries. Each struct diff_vec will be filled with the difference parameter
 * (0.0 to 1.0) and the angle where the first line is 0 radians (which also is
 * 2 pi) and is increasing by dst->h / 2 pi radians per line. The src is
 * wrapped around at the end of dst as if dst would be a cylinder.
 *
 * @param dst Pointer to destination image.
 * @param src Pointer to source image.
 * @param dv Pointer to array of struct diff_vec of dst->h elements.
 * @param xoff X offset from the left edge.
 * @param res This defines the pixel resolution. If set to 1, every pixel
 * position is tested, if set to 2 every second position is tested and so on.
 */
void mi_diff_vector_vert(const mem_img_t *dst, const mem_img_t *src, struct diff_vec *dv, int xoff, int res)
{
   int i, j;

   for (i = 0; i < dst->h; i += res)
   {
      dv[i].dv_diff = mi_cmp_region(dst, src, xoff, i, 0, 1);
      dv[i].dv_angle = (double) (dst->h - i - 1) / dst->h * 2.0 * M_PI;
      dv[i].dv_x = xoff;
      dv[i].dv_y = i;
      for (j = 1; j < res && j + i < dst->h; j++)
         dv[i + j] = dv[i];
      //printf("angle = %.1f, diff = %f, x = %d, %s\n", dv[i].dv_angle * 180.0 / M_PI, dv[i].dv_diff, xoff, bar(dv[i].dv_diff));
   }
}


static int mi_save(const char *s, mem_img_t *mi)
{
   image_t *img;
   FILE *f;

   img = mi_to_gdimage(mi);
   if ((f = fopen(s, "w")) == NULL)
      return -1;

#ifdef HAVE_GD
   gdImagePng(img, f);
   gdImageDestroy(img);
#endif

   fclose(f);

   return 0;
}


void mi_diff_vec_minmax(const struct diff_vec *dv, int len, double *min, double *max)
{
   *min = 1.0;
   *max = 0.0;

   for (; len; len--, dv++)
   {
      if (*min > dv->dv_diff)
         *min = dv->dv_diff;
      if (*max < dv->dv_diff)
         *max = dv->dv_diff;
   }
}


void mi_stretch_diff_vec(struct diff_vec *dv, int len, double min, double max)
{
   for (; len; len--, dv++)
   {
      dv->dv_diff -= min;
      dv->dv_diff *= 1.0 / (max - min);
   }
}


#ifdef MI_THREADS
void *mi_diff_vector_vert_thread(struct mi_thread_param *tp)
{
   sigset_t sset;
   int e;

   sigemptyset(&sset);
   if ((e = pthread_sigmask(SIG_BLOCK, &sset, NULL)))
      log_msg(LOG_ERR, "pthread_sigmask() failed: %s", strerror(e));

   for (;;)
   {
      pthread_mutex_lock(tp->mutex);
      while (tp->status != 1)
      {
         if (tp->status == -1)
         {
            pthread_mutex_unlock(tp->mutex);
            return NULL;
         }
         //if (!tp->status)
            pthread_cond_wait(&tp->worker, tp->mutex);
      }
      pthread_mutex_unlock(tp->mutex);

      mi_diff_vector_vert(tp->dst, tp->src, tp->dv, tp->xoff, tp->res);

      pthread_mutex_lock(tp->mutex);
      tp->status = 0;
      pthread_cond_signal(tp->boss);
      pthread_mutex_unlock(tp->mutex);
   }
   return NULL;
}
#endif


/* Find number of continuous diff_vecs with equal dv_diff.
 * @param dv Pointer to diff_vec array, must be sorted by dv_diff descendingly
 * and dv_index ascendingly.
 * @param len Number of elements in dv.
 * @return Number of continues elements at the beginning of dv.
 */
#define QUANT_F 10.0
#define QUANT(x) round((x) * QUANT_F)

int diff_vec_count_eq(const struct diff_vec *dv, int len)
{
   int i;

   for (i = 0; i < len - 1; i++)
   {
      if (dv[i].dv_index < dv[i + 1].dv_index - 1)
         break;

      if (QUANT(dv[i].dv_diff) > QUANT(dv[i + 1].dv_diff))
         break;
   }

   return i + 1;
}


void index_diff_vec(struct diff_vec *dv, int len)
{
   int i;

   for (i = 0; i < len; i++, dv++)
      dv->dv_index = i;
}


void weight_diff_vec(struct diff_vec *dv, int len, double phase, double weight)
{
   for (; len; len--, dv++)
      dv->dv_diff *= 1.0 - (1.0 - weight) * (1.0 - cos(dv->dv_angle * 2.0 + phase)) / 2.0;
}


#ifdef MI_THREADS

static pthread_cond_t cn_boss = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static struct mi_thread_param mt[MI_THREADS];

static void mi_wait_threads(void)
{
   int i;

   // wait for all threads to finish and destroy them
   for (i = 0; i < MI_THREADS; i++)
   {
      pthread_mutex_lock(&mx);
      while (mt[i].status == 1)
         pthread_cond_wait(&cn_boss, &mx);
      pthread_mutex_unlock(&mx);
   }
}


void __attribute__((constructor)) init_mi_threads(void)
{
   int i, e;
 
   log_debug("creating %d worker threads", MI_THREADS);
   for (i = 0; i < MI_THREADS; i++)
   {
      mt[i].boss = &cn_boss;
      pthread_cond_init(&mt[i].worker, NULL);
      mt[i].mutex = &mx;
      mt[i].status = 0;
      if ((e = pthread_create(&mt[i].th, NULL, (void*(*)(void*)) mi_diff_vector_vert_thread, &mt[i])))
         log_msg(LOG_ERR, "pthread_create() failed: %s", strerror(e)),
            exit(EXIT_FAILURE);
   }
}


void __attribute__((destructor)) destroy_mi_threads(void)
{
   int i;

   // wait for all threads to finish and destroy them
   log_debug("joining and destroying workers");
   mi_wait_threads();
   for (i = 0; i < MI_THREADS; i++)
   {
      pthread_mutex_lock(&mx);
      mt[i].status = -1;
      pthread_cond_signal(&mt[i].worker);
      pthread_mutex_unlock(&mx);
      pthread_join(mt[i].th, NULL);
      pthread_cond_destroy(&mt[i].worker);
   }
   pthread_cond_destroy(&cn_boss);
   pthread_mutex_destroy(&mx);
}

#endif


/*! Calculate difference of source image to destination image where the source
 * image is rotate 360 degress around a center point in the destination.
 * Optionally it rotates the image several times, for each rotation the source
 * is moved one pixel away (to the outside) from the center point.
 * 
 * @param dst Pointer to destination image.
 * @param src Pointer to the source image.
 * @param x Center point x coordinate.
 * @param y Center point y coordinate.
 * @param xvar Number of rotations and shifts to the out side. It should be
 * greater or equal 1.
 * @param res This defines the pixel resolution. If set to 1, every pixel
 * position is test, if set to 2 every second position is tested and so on.
 * @param dv Pointer to struct dv_vec Pointer.
 * @return Returns the number of items in dv.
 */
int get_diff_vec(image_t *dst, image_t *src, int x, int y, int xvar, int res, struct diff_vec **dv)
{
   mem_img_t *mi[2];
   int i, j;

   // safety check
   if (xvar < 1) xvar = 1;

#ifdef HAVE_CAIRO
   mi[0] = rectify_circle(dst, x, y, cairo_image_surface_get_width(src) + xvar - 1);
#endif
#ifdef HAVE_GD
   mi[0] = rectify_circle(dst, x, y, gdImageSX(src) + xvar - 1);
#endif

   // <debug>
   /*char buf[32];
   snprintf(buf, sizeof(buf), "rectify_%d-%d.png", x, y);
   mi_save(buf, mi[0]);*/
   // </debug>

   if ((*dv = malloc(mi[0]->h * sizeof(**dv) * xvar)) == NULL)
   {
      mi_free(mi[0]);
      return -1;
   }

   mi[1] = mi_from_gdimage(src);

#ifdef MI_THREADS
  for (i = 0; i < xvar; i += res)
   {
      pthread_mutex_lock(&mx);
      for (;;)
      {
         for (j = 0; j < MI_THREADS; j++)
         {
            if (!mt[j].status)
            {
               mt[j].dst = mi[0];
               mt[j].src = mi[1];
               mt[j].res = res;
               mt[j].dv = *dv + mi[0]->h * i;
               mt[j].xoff = i;
               mt[j].status = 1;
               pthread_cond_signal(&mt[j].worker);
               break;
            }
         }

         if (j < MI_THREADS)
            break;

         pthread_cond_wait(&cn_boss, &mx);
      }
      pthread_mutex_unlock(&mx);
   }

   mi_wait_threads();

#else

   for (i = 0; i < xvar; i += res)
      mi_diff_vector_vert(mi[0], mi[1], *dv + mi[0]->h * i, i, res);

#endif

   for (i = 0; i < xvar; i += res)
      for (j = 1; j < res && j + i < xvar; j++)
         memcpy(*dv + mi[0]->h * (i + j), *dv + mi[0]->h * i, sizeof(**dv) * mi[0]->h);
 
   i = mi[0]->h;
   mi_free(mi[1]);
   mi_free(mi[0]);

   return i;
}


int get_best_rotation(image_t *dst, image_t *src, int x, int y, int xvar, int resolution, struct diff_vec *res)
{
   struct diff_vec *dv;
   int n;

   if (xvar < 1) xvar = 1;

   if ((n = get_diff_vec(dst, src, x, y, xvar, resolution, &dv)) == -1)
      return -1;

   weight_diff_vec(dv, n * xvar, 0, 0.7);

//#define MI_STRETCH
#ifdef MI_STRETCH
   double min, max;
   mi_diff_vec_minmax(dv, n * xvar, &min, &max);
   mi_stretch_diff_vec(dv, n * xvar, min, max);
#endif
//#define MI_SAVE_DV 
#ifdef MI_SAVE_DV 
   mem_img_t *mi;
   gdImage *img;
   mi = mi_from_diff_vec(dv, n, xvar);
   img = mi_to_gdimage(mi);
   save_gdimage("grey.png", img);
   gdImageDestroy(img);
   mi_free(mi);
#endif

   qsort(dv, n * xvar, sizeof(*dv), (int(*)(const void*, const void*)) cmp_dv);
   *res = dv[0];

   free(dv);
   return 0;
}

