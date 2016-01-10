/*! This file contains two functions for reading and writing JPEG files from
 * and to Cairo image surfaces. It uses the functions from the libjpeg.
 * Most of the code is directly derived from the online example at
 * http://libjpeg-turbo.virtualgl.org/Documentation/Documentation
 *
 * All prototypes are defined in cairo_jpg.h All functions and their parameters
 * and return values are described below directly at the functions. You may
 * also have a look at the preprocessor macros defined below.
 *
 * To compile this code you need to have installed the packages libcairo2-dev
 * and libjpeg-dev. Compile with the following to create an object file to link
 * with your code:
 * gcc -Wall -c `pkg-config cairo libjpeg --cflags --libs` cairo_jpg.c
 * Use the following command to include the main() function and create an
 * executable for testing of this code:
 * gcc -Wall -o cairo_jpg -DCAIRO_JPEG_MAIN `pkg-config cairo libjpeg --cflags --libs` cairo_jpg.c
 *
 * @author Bernhard R. Fischer, 2048R/5C5FFD47 bf@abenteuerland.at
 * @version 2015/12/31
 * @license This code is free software. Do whatever you like to do with it.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_LIBJPEG
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cairo.h>
#include <jpeglib.h>

#include "cairo_jpg.h"

/*! Macro to activate main() function. This is only used for testing. Comment
 * it out (#undef) if you link this file to your own program.
 */
//#define CAIRO_JPEG_MAIN
//
/*! Define this to use an alternate implementation of
 * cairo_image_surface_create_from_jpeg() which fstat(3)s the file before
 * reading (see below). For huge files this /may/ be slightly faster.
 */
#undef CAIRO_JPEG_USE_FSTAT

/*! This is the read block size for the stream reader
 * cairo_image_surface_create_from_jpeg_stream().
 */
#define CAIRO_JPEG_IO_BLOCK_SIZE 4096

/*! Define this to test jpeg creation with non-image surfaces. This is only for
 * testing and is to be used together with CAIRO_JPEG_MAIN.  
 */
#undef CAIRO_JPEG_TEST_SIMILAR
#if defined(CAIRO_JPEG_TEST_SIMILAR) && defined(CAIRO_JPEG_MAIN)
#include <cairo-pdf.h>
#endif


/*! This function creates a JPEG file in memory from a Cairo image surface.
 * @param sfc Pointer to a Cairo surface. It should be an image surface of
 * either CAIRO_FORMAT_ARGB32 or CAIRO_FORMAT_RGB24. Other formats are
 * converted to CAIRO_FORMAT_RGB24 before compression.
 * Please note that this may give unexpected results because JPEG does not
 * support transparency. Thus, default background color is used to replace
 * transparent regions. The default background color is black if not specified
 * explicitly. Thus converting e.g. PDF surfaces without having any specific
 * background color set will apear with black background and not white as you
 * might expect. In such cases it is suggested to manually convert the surface
 * to RGB24 before calling this function.
 * @param data Pointer to a memory pointer. This parameter receives a pointer
 * to the memory area where the final JPEG data is found in memory. This
 * function reservers the memory properly and it has to be freed by the caller
 * with free(3).
 * @param len Pointer to a variable of type size_t which will receive the final
 * lenght of the memory buffer.
 * @param quality Compression quality, 0-100.
 * @return On success the function returns CAIRO_STATUS_SUCCESS. In case of
 * error CAIRO_STATUS_INVALID_FORMAT is returned.
 */
cairo_status_t cairo_image_surface_write_to_jpeg_mem(cairo_surface_t *sfc, unsigned char **data, size_t *len, int quality)
{
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
   JSAMPROW row_pointer[1];
   cairo_surface_t *other = NULL;

   // check valid input format (must be IMAGE_SURFACE && (ARGB32 || RGB24))
   if (cairo_surface_get_type(sfc) != CAIRO_SURFACE_TYPE_IMAGE ||
         (cairo_image_surface_get_format(sfc) != CAIRO_FORMAT_ARGB32 &&
         cairo_image_surface_get_format(sfc) != CAIRO_FORMAT_RGB24))
   {
      // create a similar surface with a proper format if supplied input format
      // does not fulfill the requirements
      double x1, y1, x2, y2;
      other = sfc;
      cairo_t *ctx = cairo_create(other);
      // get extents of original surface
      cairo_clip_extents(ctx, &x1, &y1, &x2, &y2);
      cairo_destroy(ctx);

      // create new image surface
      sfc = cairo_surface_create_similar_image(other, CAIRO_FORMAT_RGB24, x2 - x1, y2 - y1);
      if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
         return CAIRO_STATUS_INVALID_FORMAT;

      // paint original surface to new surface
      ctx = cairo_create(sfc);
      cairo_set_source_surface(ctx, other, 0, 0);
      cairo_paint(ctx);
      cairo_destroy(ctx);
   }

   // finish queued drawing operations
   cairo_surface_flush(sfc);

   // init jpeg compression structures
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_compress(&cinfo);

   // set compression parameters
   jpeg_mem_dest(&cinfo, data, len);
   cinfo.image_width = cairo_image_surface_get_width(sfc);
   cinfo.image_height = cairo_image_surface_get_height(sfc);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   cinfo.in_color_space = JCS_EXT_BGRX;
#else
   cinfo.in_color_space = JCS_EXT_XRGB;
#endif
   cinfo.input_components = 4;
   jpeg_set_defaults(&cinfo);
   jpeg_set_quality(&cinfo, quality, TRUE);

   // start compressor
   jpeg_start_compress(&cinfo, TRUE);

   // loop over all lines and compress
   while (cinfo.next_scanline < cinfo.image_height)
   {
      row_pointer[0] = cairo_image_surface_get_data(sfc) + (cinfo.next_scanline
            * cairo_image_surface_get_stride(sfc));
      (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
   }

   // finalize and close everything
   jpeg_finish_compress(&cinfo);
   jpeg_destroy_compress(&cinfo);

   // destroy temporary image surface (if available)
   if (other != NULL)
      cairo_surface_destroy(sfc);

   return CAIRO_STATUS_SUCCESS;
}


/*! This is the internal write function which is called by
 * cairo_image_surface_write_to_jpeg(). It is not exported.
 */
static cairo_status_t cj_write(void *closure, const unsigned char *data, unsigned int length)
{
   return write((long) closure, data, length) < length ?
      CAIRO_STATUS_WRITE_ERROR : CAIRO_STATUS_SUCCESS;
}


/*! This function writes JPEG file data from a Cairo image surface by using the
 * user-supplied stream writer function write_func().
 * @param sfc Pointer to a Cairo *image* surface. Its format must either be
 * CAIRO_FORMAT_ARGB32 or CAIRO_FORMAT_RGB24. Other formats are not supported
 * by this function, yet.
 * @param write_func Function pointer to a function which is actually writing
 * the data.
 * @param closure Pointer to user-supplied variable which is directly passed to
 * write_func().
 * @param quality Compression quality, 0-100.
 * @return This function calles cairo_image_surface_write_to_jpeg_mem() and
 * returns its return value.
 */
cairo_status_t cairo_image_surface_write_to_jpeg_stream(cairo_surface_t *sfc, cairo_write_func_t write_func, void *closure, int quality)
{
   cairo_status_t e;
   unsigned char *data = NULL;
   size_t len = 0;

   // create JPEG data in memory from surface
   if ((e = cairo_image_surface_write_to_jpeg_mem(sfc, &data, &len, quality)) != CAIRO_STATUS_SUCCESS)
      return e;

   // write whole memory block with stream function
   e = write_func(closure, data, len);

   // free JPEG memory again and return the return value
   free(data);
   return e;

}


/*! This function creates a JPEG file from a Cairo image surface.
 * @param sfc Pointer to a Cairo *image* surface. Its format must either be
 * CAIRO_FORMAT_ARGB32 or CAIRO_FORMAT_RGB24. Other formats are not supported
 * by this function, yet.
 * @param filename Pointer to the filename.
 * @param quality Compression quality, 0-100.
 * @return In case of success CAIRO_STATUS_SUCCESS is returned. If an error
 * occured while opening/creating the file CAIRO_STATUS_DEVICE_ERROR is
 * returned. The error can be tracked down by inspecting errno(3). The function
 * internally calles cairo_image_surface_write_to_jpeg_stream() and returnes
 * its return value respectively (see there).
 * returns its return value.
 */
cairo_status_t cairo_image_surface_write_to_jpeg(cairo_surface_t *sfc, const char *filename, int quality)
{
   cairo_status_t e;
   int outfile;

   // Open/create new file
   if ((outfile = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
      return CAIRO_STATUS_DEVICE_ERROR;

   // write surface to file
   e = cairo_image_surface_write_to_jpeg_stream(sfc, cj_write, (void*) (long) outfile, quality);

   // close file again and return
   close(outfile);
   return e;
}


/*! This function decompresses a JPEG image from a memory buffer and creates a
 * Cairo image surface.
 * @param data Pointer to JPEG data (i.e. the full contents of a JPEG file read
 * into this buffer).
 * @param len Length of buffer in bytes.
 * @return Returns a pointer to a cairo_surface_t structure. It should be
 * checked with cairo_surface_status() for errors.
 */
cairo_surface_t *cairo_image_surface_create_from_jpeg_mem(void *data, size_t len)
{
   struct jpeg_decompress_struct cinfo;
   struct jpeg_error_mgr jerr;
   JSAMPROW row_pointer[1];
   cairo_surface_t *sfc;
 
   // initialize jpeg decompression structures
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_decompress(&cinfo);
   jpeg_mem_src(&cinfo, data, len);
   (void) jpeg_read_header(&cinfo, TRUE);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   cinfo.out_color_space = JCS_EXT_BGRA;
#else
   cinfo.out_color_space = JCS_EXT_ARGB;
#endif

   // start decompressor
   (void) jpeg_start_decompress(&cinfo);

   // create Cairo image surface
   sfc = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cinfo.output_width, cinfo.output_height);
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      jpeg_destroy_decompress(&cinfo);
      return sfc;
   }

   // loop over all scanlines and fill Cairo image surface
   while (cinfo.output_scanline < cinfo.output_height)
   {
      row_pointer[0] = cairo_image_surface_get_data(sfc) +
         (cinfo.output_scanline * cairo_image_surface_get_stride(sfc));
      (void) jpeg_read_scanlines(&cinfo, row_pointer, 1);
   }

   // finish and close everything
   cairo_surface_mark_dirty(sfc);
   (void) jpeg_finish_decompress(&cinfo);
   jpeg_destroy_decompress(&cinfo);

   // set jpeg mime data
   cairo_surface_set_mime_data(sfc, CAIRO_MIME_TYPE_JPEG, data, len, free, data);

   return sfc;
}


/*! This function reads an JPEG image from a stream and creates a Cairo image
 * surface.
 * @param read_func Pointer to function which reads data.
 * @param closure Pointer which is passed to read_func().
 * @return Returns a pointer to a cairo_surface_t structure. It should be
 * checked with cairo_surface_status() for errors.
 * @note If the surface returned is invalid you can use errno(3) to determine
 * further reasons. Errno is set according to realloc(3). If you
 * intend to check errno you shall set it to 0 before calling this function
 * because it modifies errno only in case of an error.
 */
cairo_surface_t *cairo_image_surface_create_from_jpeg_stream(cairo_read_func_len_t read_func, void *closure)
{
   void *data, *tmp;
   ssize_t len, rlen;
   int eof = 0;

   // read all data into memory buffer in blocks of CAIRO_JPEG_IO_BLOCK_SIZE 
   for (len = 0, data = NULL; !eof; len += rlen)
   {
      // grow memory buffer and check for error
      if ((tmp = realloc(data, len + CAIRO_JPEG_IO_BLOCK_SIZE)) == NULL)
         break;
      data = tmp;

      // read byte into buffer and check for error
      if ((rlen = read_func(closure, data + len, CAIRO_JPEG_IO_BLOCK_SIZE)) == -1)
         break;

      // check if EOF occured
      if (rlen < CAIRO_JPEG_IO_BLOCK_SIZE)
         eof++;
   }

   // check for error in read loop
   if (!eof)
   {
      free(data);
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);
   }

   // call jpeg decompression and return surface
   return cairo_image_surface_create_from_jpeg_mem(data, len);
}


#ifdef CAIRO_JPEG_USE_FSTAT
/*! This function reads an JPEG image from a file an creates a Cairo image
 * surface. Internally the filesize is determined with fstat(2) and then the
 * whole data is read at once.
 * @param filename Pointer to filename of JPEG file.
 * @return Returns a pointer to a cairo_surface_t structure. It should be
 * checked with cairo_surface_status() for errors.
 * @note If the returned surface is invalid you can use errno to determine
 * further reasons. Errno is set according to fopen(3) and malloc(3). If you
 * intend to check errno you shall set it to 0 before calling this function
 * because it does not modify errno itself.
 */
cairo_surface_t *cairo_image_surface_create_from_jpeg(const char *filename)
{
   void *data;
   int infile;
   struct stat stat;

   // open input file
   if ((infile = open(filename, O_RDONLY)) == -1)
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);

   // get stat structure for file size
   if (fstat(infile, &stat) == -1)
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);

   // allocate memory
   if ((data = malloc(stat.st_size)) == NULL)
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);

   // read data
   if (read(infile, data, stat.st_size) < stat.st_size)
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);

   close(infile);

   return cairo_image_surface_create_from_jpeg_mem(data, stat.st_size);
}

#else


/*! This is the read function which is called by
 * cairo_image_surface_create_from_jpeg_stream() (non-fstat-version below). It
 * is not exported.
 */
static ssize_t cj_read(void *closure, unsigned char *data, unsigned int length)
{
   return read((long) closure, data, length);
}


/*! This function reads an JPEG image from a file an creates a Cairo image
 * surface. Internally the function calls
 * cairo_image_surface_create_from_jpeg_stream() to actually read the data.
 * @param filename Pointer to filename of JPEG file.
 * @return Returns a pointer to a cairo_surface_t structure. It should be
 * checked with cairo_surface_status() for errors.
 * @note If the returned surface is invalid you can use errno to determine
 * further reasons. Errno is set according to fopen(3) and malloc(3). If you
 * intend to check errno you shall set it to 0 before calling this function
 * because it does not modify errno itself.
 */
cairo_surface_t *cairo_image_surface_create_from_jpeg(const char *filename)
{
   cairo_surface_t *sfc;
   int infile;

   // open input file
   if ((infile = open(filename, O_RDONLY)) == -1)
      return cairo_image_surface_create(CAIRO_FORMAT_INVALID, 0, 0);

   // call stream loading function
   sfc = cairo_image_surface_create_from_jpeg_stream(cj_read, (void*)(long) infile);
   close(infile);

   return sfc;
}

#endif


#ifdef CAIRO_JPEG_MAIN
/*! Main routine, only for testing. #undef CAIRO_JPEG_MAIN or simply delete
 * this part if you link this file to your own program.
 */
int main(int argc, char **argv)
{
   cairo_surface_t *sfc;

#ifndef CAIRO_JPEG_TEST_SIMILAR
   if (argc < 4)
   {
      fprintf(stderr, "usage: %s <'r' | 'w'> <infile> <outfile>\n", argv[0]);
      return 1;
   }

   // check for write mode
   if (*argv[1] == 'w')
   {
      // read PNG file
      sfc = cairo_image_surface_create_from_png(argv[2]);
      if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
      {
         fprintf(stderr, "error loading image: %s", cairo_status_to_string(cairo_surface_status(sfc)));
         return 2;
      }
      // write JPEG file
      cairo_image_surface_write_to_jpeg(sfc, argv[3], 90);
      cairo_surface_destroy(sfc);
   }
   // check for read mode
   else if (*argv[1] == 'r')
   {
      // read JPEG file
      sfc = cairo_image_surface_create_from_jpeg(argv[2]);
      if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
      {
         fprintf(stderr, "error loading image: %s", cairo_status_to_string(cairo_surface_status(sfc)));
         return 3;
      }
      // write PNG file
      cairo_surface_write_to_png(sfc, argv[3]);
      cairo_surface_destroy(sfc);
   }
   else
   {
      fprintf(stderr, "1st argument must be one of 'r' or 'w'.\n");
      return 1;
   }
#else
   sfc = cairo_pdf_surface_create("xyz.pdf", 595.276, 841.890);

   cairo_t *ctx = cairo_create(sfc);
   cairo_set_source_rgb(ctx, 1, 1, 1);
   cairo_paint(ctx);
   cairo_move_to(ctx, 100, 100);
   cairo_set_source_rgb(ctx, 1, 0, 0);
   cairo_set_line_width(ctx, 3);
   cairo_line_to(ctx, 400, 400);
   cairo_stroke(ctx);
   cairo_destroy(ctx);

   cairo_image_surface_write_to_jpeg(sfc, "xyz.jpg", 90);
   cairo_surface_destroy(sfc);
#endif

   return 0;
}

#endif

#endif //HAVE_LIBJPEG

