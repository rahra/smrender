/* Copyright 2013 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! This file contains the code for median cut algorithm which is used to
 * reduce the colors of an image to a specified number.
 * The algorthim was ported to C from the original C++ sample at
 * http://en.literateprograms.org/Median_cut_algorithm_(C_Plus_Plus)?oldid=12754
 *
 * For testing, compile with
 * gcc -Wall -DTEST_MEDIAN_CUT -DHAVE_CONFIG_H `pkg-config --libs --cflags cairo` -std=c99 -I../libsmrender -I..  -lsmrender -omedian_cut median_cut.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef HAVE_CAIRO
#include <cairo.h>
#endif

#include "smrender.h"

#define NUM_DIMENSIONS 3


// type of a dimension
typedef unsigned char mc_pdim_t;

// point with number of dimensions
typedef struct mc_point
{
    mc_pdim_t x[NUM_DIMENSIONS];
} mc_point_t;

// block with number of points
typedef struct mc_block
{
    mc_point_t min_corner, max_corner, avg;
    mc_point_t* points;
    int len;
} mc_block_t;


/*! Initialize a block.
 */
static void mc_init_block(mc_block_t *blk, mc_point_t *p, int len)
{
   blk->points = p;
   blk->len = len;

   memset(&blk->min_corner, 0, sizeof(blk->min_corner));
   memset(&blk->max_corner, 0xff, sizeof(blk->max_corner));
}


/*! Find the dimension with the longest distance between its min and max value.
 * @param blk Pointer to block.
 * @return Returns the the index which is between i and NUM_DIMENSIONS - 1.
 */
static int mc_longest_side_index(const mc_block_t *blk)
{
   int diff, m = 0, maxi = 0;

   for (int i = 0; i < NUM_DIMENSIONS; i++)
   {
      diff = blk->max_corner.x[i] - blk->min_corner.x[i];
      if (diff > m)
      {
         m = diff;
         maxi = i;
      }
   }

   return maxi;
}


/*! Calculate the distance of the longest side of a block.
 * @param blk Pointer to the block.
 * @return Returns the distance of the longest side.
 */
static int mc_longest_side_length(const mc_block_t *blk)
{
   int i = mc_longest_side_index(blk);
   return blk->max_corner.x[i] - blk->min_corner.x[i];
}


static int mc_point_compare0(const mc_point_t *a, const mc_point_t *b, int ixptr)
{
   return a->x[ixptr] - b->x[ixptr];
}


static int mc_ixptr_;


static int mc_point_compare(const mc_point_t *a, const mc_point_t *b)
{
   return mc_point_compare0(a, b, mc_ixptr_);
}


static int mc_block_compare(const mc_block_t *a, const mc_block_t *b)
{
   return mc_longest_side_length(b) - mc_longest_side_length(a);
}


static mc_pdim_t mc_min(mc_pdim_t a, mc_pdim_t b)
{
   return a < b ? a : b;
}


static mc_pdim_t mc_max(mc_pdim_t a, mc_pdim_t b)
{
   return a > b ? a : b;
}


/*! Calculate the distance of two colors.
 */
static int mc_col_dist(const mc_point_t *a, const mc_point_t *b)
{
   int diff = 0;
   for (int i = 0; i < NUM_DIMENSIONS; i++)
      diff += abs(a->x[i] - b->x[i]);
   return diff;
}


/*! Find the color (index) which is nearest to the color of pt.
 * @param blk Pointer to the block list (of colors).
 * @param size Number of blocks in list.
 * @param pt Point to which the nearest color shall be found.
 * @returns Returns the index, 0 <= index < size.
 */
static int mc_nearest_block_index(const mc_block_t *blk, int size, const mc_point_t *pt)
{
   int diff, dist = 0x1000000, idx = 0;
   for (int i = 0; i < size; i++)
   {
      diff = mc_col_dist(&blk[i].avg, pt);
      if (diff < dist)
      {
         dist = diff;
         idx = i;
      }
   }
   return idx;
}


/*! Find the colors which have the lowest and highest color values in the list
 * of points (pixels).
 */
static void mc_shrink(mc_block_t *blk)
{
   int i, j;

   blk->min_corner = blk->max_corner = blk->points[0];
   for (i = 1; i < blk->len; i++)
      for (j = 0; j < NUM_DIMENSIONS; j++)
      {
         blk->min_corner.x[j] = mc_min(blk->min_corner.x[j], blk->points[i].x[j]);
         blk->max_corner.x[j] = mc_max(blk->max_corner.x[j], blk->points[i].x[j]);
      }
}


/*! Calculate the average color value of all pixels in the block.
 * @param blk Pointer to the block.
 */
static void mc_avg_block(mc_block_t *blk)
{
   long sum;

   for (int j = 0; j < NUM_DIMENSIONS; j++)
   {
      sum = 0;
      for (int i = 0; i < blk->len; i++)
         sum += blk->points[i].x[j];
      blk->avg.x[j] = sum / blk->len;
   }
}


/*! Allocate memory for the block list.
 * @param desired Number of elements.
 * @return This function returns a pointer to the block list or NULL in case of
 * error.
 */
static mc_block_t *mc_block_list(unsigned desired)
{
   return malloc(sizeof(mc_block_t) * desired);
}


/*! This function actually reduces the colors to a given number.
 * @param image Pointer to an array of all points of the image.
 * @param num Number of elements in array (total number of pixels).
 * @param desired Desired numbers of colors after color reduction.
 * @param blk Pointer to block array. This has to have as much elements as
 * defined by _desired_.
 * @return This function returnes the final number of colors (which may be less
 * than desired).
 */
static int mc_median_cut(mc_point_t *image, unsigned num, unsigned desired, mc_block_t *blk)
{
   int size;
   int len;

   log_debug("reducing...");
   mc_init_block(blk, image, num);
   mc_shrink(blk);
   mc_avg_block(blk);
   size = 1;

   while (size < (int) desired && blk->len > 1)
   {
      // qsort_r() could be used instead but this would cause compatibility
      // issues (GNU/BSD versions differ). Since this code is not
      // multi-threaded it is not an issue.
      // FIXME: This sorting is not necessary. A partial sort (such as
      // std::nth_element) would sufficient.
      mc_ixptr_ = mc_longest_side_index(blk);
      // sort points along one dimension (mc_ixptr_).
      qsort(blk->points, blk->len, sizeof(*blk->points),
            (int (*) (const void*, const void*)) mc_point_compare);

      len = (blk->len + 1) / 2;
      mc_init_block(&blk[size], blk->points + len, blk->len - len);
      blk->len = len;

      mc_shrink(blk);
      mc_shrink(&blk[size]);
      mc_avg_block(&blk[size]);
      size++;
      qsort(blk, size, sizeof(*blk), (int (*) (const void*, const void*)) mc_block_compare);
   }

   return size;
}


#ifdef HAVE_CAIRO
static uint32_t mc_point_to_cairo_color(const mc_point_t *pt)
{
   return 0xff000000 | 
      (uint32_t) pt->x[0] | 
      (uint32_t) (pt->x[1] << 8) |
      (uint32_t) (pt->x[2] << 16);
}


static void mc_cairo_color_to_point(const void *pix, mc_point_t *pt)
{
   uint32_t c = *((uint32_t*) pix);
   pt->x[0] = c & 0xff;
   pt->x[1] = (c >> 8) & 0xff;
   pt->x[2] = (c >> 16) & 0xff;
}

 
/*! This function reduces the colors in an Cairo image surface to a defined
 * number of colors.
 * @param src Pointer to the Cairo image surface.
 * @param ncol Maximum number of final colors.
 * @param Pointer to an array which will recieve the color values.
 * @return The function returns the final number of colors.
 */
int cairo_smr_image_surface_color_reduce(cairo_surface_t *src, int ncol, uint32_t *palette)
{
   unsigned char *pix;
   mc_block_t *blk;
   mc_point_t *pt, p;
   uint32_t *padr;
   int i;

   cairo_surface_flush(src);
   if ((pix = cairo_image_surface_get_data(src)) == NULL)
      return -1;

   if ((pt = malloc(sizeof(*pt) * (cairo_image_surface_get_width(src) * cairo_image_surface_get_height(src)))) == NULL)
   {
      log_errno(LOG_ERR, "malloc() failed");
      return -1;
   }

   log_debug("retrieving pixels");
   i = 0;
   for (int y = 0; y < cairo_image_surface_get_height(src); y++)
   {
      for (int x = 0; x < cairo_image_surface_get_width(src); x++, i++)
         mc_cairo_color_to_point(((uint32_t*) pix) + x, &pt[i]);
      pix += cairo_image_surface_get_stride(src);
   }

   log_debug("allocating block list");
   if ((blk = mc_block_list(ncol)) == NULL)
      return -1;

   ncol = mc_median_cut(pt, i, ncol, blk);

   if (palette != NULL)
      for (int i = 0; i < ncol; i++)
         palette[i] = mc_point_to_cairo_color(&blk[i].avg);

   log_debug("modifying pixels");
   pix = cairo_image_surface_get_data(src);
   for (int y = 0; y < cairo_image_surface_get_height(src); y++)
   {
      for (int x = 0; x < cairo_image_surface_get_width(src); x++)
      {
         padr = ((uint32_t*) pix) + x;
         mc_cairo_color_to_point(padr, &p);

         i = mc_nearest_block_index(blk, ncol, &p);

         *padr = mc_point_to_cairo_color(&blk[i].avg);
      }
      pix += cairo_image_surface_get_stride(src);
   }

   cairo_surface_mark_dirty(src);
   free(pt);
   free(blk);
   return ncol;
}
#endif


#ifdef TEST_MEDIAN_CUT

#include <stdio.h>


int main(int argc, char **argv)
{
   cairo_surface_t *sfc;
   int col = 127;
   uint32_t palette[col];
   
   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <PNG infile> <PNG outfile>\n", argv[0]);
      return 1;
   }

   sfc = cairo_image_surface_create_from_png(argv[1]);
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      fprintf(stderr, "failed: %s\n", cairo_status_to_string(cairo_surface_status(sfc)));
      return 1;
   }

   switch (cairo_image_surface_get_format(sfc))
   {
      case CAIRO_FORMAT_ARGB32:
      case CAIRO_FORMAT_RGB24:
         break;

      default:
         fprintf(stderr, "format not supported\n");
         return 1;
   }

   col = cairo_smr_image_surface_color_reduce(sfc, col, palette);
   cairo_surface_write_to_png(sfc, argv[2]);
   cairo_surface_destroy(sfc);

   return 0;
}


#endif

