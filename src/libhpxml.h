/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of libhpxml.
 *
 * Libhpxml is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Libhpxml is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libhpxml. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file libhpxml.h
 * This file contains all structures and prototypes for the high performance
 * XML parser library libhpxml.
 * 
 * @author Bernhard R. Fischer
 */

#ifndef HPXML_H
#define HPXML_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "bstring.h"


#define IS_XML1CHAR(x) (isalpha(x) || (x == '_') || (x == ':'))
#define IS_XMLCHAR(x) (isalpha(x) || isdigit(x) || (x == '.') || (x == '-') || (x == '_') || (x == ':'))

#define hpx_init_simple() hpx_init(0, 10*1024*1024)

#define MMAP_PAGES (1L << 15)


typedef struct hpx_ctrl
{
   //! data buffer containing pointer and number of bytes in buffer
   //bstring_t buf;
   struct bstringl buf;
   //! file descriptor of input file
   int fd;
   //! flag set if eof
   short eof;
   //! total length of buffer
   long len;
   //! current working position
   long pos;
   //! flag to deter if next element is in or out of tag
   int in_tag;
   //! flag set if data should be read from file
   short empty;
   //! flag set if data is memory mapped
   short mmap;
   //! pointer to madvise()'d region (MADV_WILLNEED)
   char *madv_ptr;
   //! system page size
   long pg_siz;
   //! length of advised region (multiple of sysconf(_SC_PAGESIZE))
   long pg_blk_siz;
} hpx_ctrl_t;

typedef struct hpx_attr
{
   bstring_t name;   //! name of attribute
   bstring_t value;  //! value of attribute
   char delim;       //! delimiter character of attribute value
} hpx_attr_t;

typedef struct hpx_tag
{
   bstring_t tag;
   int type;
   long line;
   int nattr;
   int mattr;
   hpx_attr_t attr[];
} hpx_tag_t;

typedef struct hpx_tree
{
   hpx_tag_t *tag;
   int nsub;
   int msub;
   struct hpx_tree *subtag[];
} hpx_tree_t;

enum
{
   HPX_ILL, HPX_OPEN, HPX_SINGLE, HPX_CLOSE, HPX_LITERAL, HPX_ATT, HPX_INSTR, HPX_COMMENT
};


long hpx_lineno(void);
void hpx_tm_free(hpx_tag_t *t);
void hpx_tm_free_tree(hpx_tree_t *);
hpx_tag_t *hpx_tm_create(int n);
int hpx_process_elem(bstring_t b, hpx_tag_t *p);
hpx_ctrl_t *hpx_init(int fd, long len);
void hpx_init_membuf(hpx_ctrl_t *ctl, void *buf, int len);
void hpx_free(hpx_ctrl_t *ctl);
int hpx_get_elem(hpx_ctrl_t *ctl, bstring_t *b, int *in_tag, long *lno);
long hpx_get_eleml(hpx_ctrl_t *ctl, bstringl_t *b, int *in_tag, long *lno);
int hpx_fprintf_tag(FILE *f, const hpx_tag_t *p);
int hpx_tree_resize(hpx_tree_t **tl, int n);

#endif

