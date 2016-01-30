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

/*! This file contains the qrcode of the Smrender homepage. 
 *
 *  @author Bernhard R. Fischer
 */


#include "smrender_dev.h"

#ifdef HAVE_GD
#include <gd.h>
#endif


#define SMQR_BLACK 0x10000000
#define SMQR_TRANS 0x7fffffff


const struct
{
   int dim;
   char data[];
} smqr_ =
{
   29,
   {
      1,1,1,1,1,1,1,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0,1,1,1,1,1,1,1,
      1,0,0,0,0,0,1,0,1,1,0,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,0,1,
      1,0,1,1,1,0,1,0,0,1,1,1,0,0,1,0,0,0,1,0,0,0,1,0,1,1,1,0,1,
      1,0,1,1,1,0,1,0,1,1,0,1,0,0,0,1,1,1,0,0,0,0,1,0,1,1,1,0,1,
      1,0,1,1,1,0,1,0,0,0,1,0,1,1,0,0,1,0,1,1,1,0,1,0,1,1,1,0,1,
      1,0,0,0,0,0,1,0,1,0,1,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,1,
      1,1,1,1,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,
      0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,1,0,1,0,0,0,0,0,0,0,0,
      1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,
      1,1,1,0,1,0,0,0,0,1,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,1,
      1,0,0,0,1,0,1,0,0,1,0,1,1,0,0,1,0,1,0,0,0,1,0,1,1,0,0,0,0,
      0,0,0,1,0,1,0,1,1,1,1,1,0,0,1,1,1,0,1,1,0,1,1,0,1,0,0,1,0,
      0,0,1,0,1,0,1,1,0,1,0,1,0,0,0,1,0,1,0,0,1,0,0,0,0,1,1,0,0,
      1,0,1,1,1,0,0,0,0,1,1,0,1,1,1,0,1,0,1,1,1,1,1,1,1,0,1,0,1,
      0,1,0,0,1,0,1,0,1,1,0,0,0,1,1,1,0,0,1,0,1,0,0,0,0,0,1,0,0,
      1,1,0,0,0,1,0,0,0,0,1,0,1,1,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,
      1,1,1,0,0,0,1,1,1,1,1,0,1,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,
      1,1,1,0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,0,1,1,1,1,0,1,1,1,0,1,
      1,0,1,0,0,1,1,0,0,0,0,1,1,0,0,1,1,0,0,0,1,0,1,1,0,1,1,0,0,
      1,0,0,1,0,1,0,0,0,0,0,1,0,0,1,1,1,0,1,0,1,1,1,1,1,0,0,1,0,
      1,0,1,0,1,0,1,0,0,1,1,1,0,0,0,1,0,1,1,0,1,1,1,1,1,0,1,1,1,
      0,0,0,0,0,0,0,0,1,0,1,0,1,1,1,0,1,1,1,1,1,0,0,0,1,1,1,1,1,
      1,1,1,1,1,1,1,0,1,1,1,0,0,1,1,1,0,1,0,1,1,0,1,0,1,1,1,0,0,
      1,0,0,0,0,0,1,0,0,1,1,0,1,1,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,
      1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,
      1,0,1,1,1,0,1,0,1,1,0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,1,0,1,1,
      1,0,1,1,1,0,1,0,1,0,1,1,1,0,0,1,1,1,1,1,0,0,1,1,1,1,1,1,0,
      1,0,0,0,0,0,1,0,1,1,1,1,0,0,1,1,1,0,0,0,1,0,0,1,0,1,0,1,0,
      1,1,1,1,1,1,1,0,1,0,1,1,0,0,0,0,0,1,0,1,0,1,0,1,1,1,1,0,0
   }
};



image_t *smqr_image(void)
{
#ifdef HAVE_GD

   static gdImage *img = NULL;
   int i;

   if (img != NULL)
      return img;

   if ((img = gdImageCreateTrueColor(smqr_.dim, smqr_.dim)) == NULL)
      return NULL;

   gdImageAlphaBlending(img, 0);
   gdImageSaveAlpha(img, 1);

   for (i = 0; i < smqr_.dim * smqr_.dim; i++)
      gdImageSetPixel(img, i % smqr_.dim, i / smqr_.dim, smqr_.data[i] ? SMQR_BLACK : SMQR_TRANS);

   return img;

#else

   return NULL;

#endif
}


#ifdef TEST_SMQR

int main(int argc, char **argv)
{
   gdImage *img;

   img = smqr_image();
   gdImagePng(img, stdout);
   gdImageDestroy(img);

   return 0;
}

#endif

