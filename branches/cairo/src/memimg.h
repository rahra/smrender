#ifndef MEMIMG_H
#define MEMIMG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_GD
#include <gd.h>
#endif

#include <stdint.h>


#define CVLI_COMP(x, y, z) (((x) >> (y)) & (z))
#define CVL_COMP(x, y, z) ((double) CVLI_COMP(x, y, z) / (double) (z))
#define COL_COMP(x, y) CVL_COMP(x, y, 255)
#define TRN_COMP(x) CVL_COMP(x, 24, 127)
#define OPQ_COMP(x) (1.0 - TRN_COMP(x))
#define RED_COMP(x) COL_COMP(x, 16)
#define GRN_COMP(x) COL_COMP(x, 8)
#define BLU_COMP(x) COL_COMP(x, 0)
#define SQR(x) ((x) * (x))
#define CMUL 255


typedef struct mem_img mem_img_t;
typedef uint32_t pixel_t;

struct mem_img
{
   int w, h;
   pixel_t *p;
};

struct diff_vec
{
   double dv_diff;   // 0.0 no difference, 1.0 total difference
   int dv_x, dv_y;
   double dv_angle;  // angle, 0.0 - 2 * M_PI
   int dv_index;
};

int inline mi_getpixel(const mem_img_t *mi, int x, int y);
mem_img_t *mi_create(int w, int h);
void mi_free(mem_img_t *mi);
mem_img_t *rectify_circle(image_t *img, int cx, int cy, int R);
image_t *mi_to_gdimage(mem_img_t *mi);
mem_img_t *mi_from_gdimage(image_t *img);
void mi_diff_vector(const mem_img_t *dst, const mem_img_t *src, struct diff_vec *dv);
mem_img_t *mi_from_diff_vec(const struct diff_vec *dv, int len, int xvar);
int cmp_dv(const struct diff_vec *src, const struct diff_vec *dst);

int diff_vec_count_eq(const struct diff_vec *dv, int len);
void weight_diff_vec(struct diff_vec *dv, int len, double phase, double weight);
void index_diff_vec(struct diff_vec * dv, int len);
int get_diff_vec(image_t *dst, image_t *src, int x, int y, int xvar, int res, struct diff_vec **dv);
int get_best_rotation(image_t *dst, image_t *src, int x, int y, int xvar, int, struct diff_vec *res);


#endif

