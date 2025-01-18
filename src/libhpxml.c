/* Copyright 2011-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file libhpxml.c
 * This file contains the complete source for the parser.
 * \author Bernhard R. Fischer, <bf@abenteuerland.at>
 * \date 2025/01/18
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef WITH_MMAP
#include <sys/mman.h>
#ifndef MAP_NORESERVE
// MAP_NORESERVE is not defined an all systems is not necessary
#define MAP_NORESERVE 0
#endif
#endif

#ifndef MADV_WILLNEED
#ifdef POSIX_MADV_WILLNEED
#define MADV_WILLNEED POSIX_MADV_WILLNEED
#else
#warning "cannot define MADV_WILLNEED, will undef madvise()"
#undef HAVE_MADVISE
#undef HAVE_POSIX_MADVISE
#endif
#endif
#ifndef MADV_DONTNEED
#ifdef POSIX_MADV_DONTNEED
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#else
#warning "cannot define MADV_MADV_DONTNEED, will undef madvise()"
#undef HAVE_MADVISE
#undef HAVE_POSIX_MADVISE
#endif
#endif

#include "smrender.h"
#include "bstring.h"
#include "libhpxml.h"


/*! This function skips white spaces at the beginning of a bstring, i.e. it
 * removes them by increasing the base pointer.
 *  @param b Pointer to bstring.
 *  @return Number of remaining characters in b.
 */
int skip_bblank(bstring_t *b)
{
   for (; isspace((unsigned) *b->buf) && b->len; bs_advance(b));
   return b->len;
}


void hpx_tm_free(hpx_tag_t *t)
{
   free(t);
}


/*! This function recursively frees a tree with all its subtrees and tags.
 *  @param tlist Pointer to tree which should be freed.
 */
void hpx_tm_free_tree(hpx_tree_t *tlist)
{
   int i;

   // break recursion
   if (tlist == NULL)
      return;

   // recursively free all subtrees
   for (i = 0; i < tlist->msub; i++)
      hpx_tm_free_tree(tlist->subtag[i]);

   // free tag element of tree
   hpx_tm_free(tlist->tag);
   // free tree itself
   free(tlist);
}


hpx_tag_t *hpx_tm_create(int n)
{
   hpx_tag_t *t;
   if ((t = malloc(sizeof(hpx_tag_t) + n * sizeof(hpx_attr_t))) == NULL)
      return NULL;
   t->mattr = n;
   return t;
}


/*!
 *  @param b Pointer to bstring buffer which should be parsed.
 *  @param n Destination bstring.
 *  @return number of valid characters found.
 */
int hpx_parse_name(bstring_t *b, bstring_t *n)
{
   if (!IS_XML1CHAR((unsigned) *b->buf))
      return 0;

   n->buf = b->buf;
   bs_advance(b);
   for (n->len = 1; IS_XMLCHAR((unsigned) *b->buf) && b->len; bs_advance(b), n->len++);
   return n->len;
}


int hpx_parse_attr_list(bstring_t *b, hpx_tag_t *t)
{
   for (t->nattr = 0; t->nattr < t->mattr; t->nattr++)
   {
      if (!skip_bblank(b))
         break;

      if (!hpx_parse_name(b, &t->attr[t->nattr].name))
         break;

      if (!skip_bblank(b))
         break;

      if (*b->buf != '=')
      {
         t->attr[t->nattr].value.buf = NULL;
         t->attr[t->nattr].value.len = 0;
         break;
      }

      if (!bs_advance(b))
         break;

      if (!skip_bblank(b))
         break;

      t->attr[t->nattr].delim = *b->buf;

      if ((t->attr[t->nattr].delim != '"') && (t->attr[t->nattr].delim != '\''))
         break;

      if (!bs_advance(b))
         break;

      t->attr[t->nattr].value.buf = b->buf;
      for (t->attr[t->nattr].value.len = 0; b->len && (*b->buf != t->attr[t->nattr].delim); bs_advance(b), t->attr[t->nattr].value.len++);

      if (!b->len)
         break;

      bs_advance(b);
   }

   return t->nattr;
}

 
/*! Parses bstring into hpx_tag_t structure. The bstring buffer must contain a
 * single XML element with correct boundaries. This is either a tag (<....>) or
 * just text.
 * @param b Bstring containing pointer to an element.
 * @param p Pointer to valid hpx_tag_t structure. The structure will we filled
 * out.
 * @return Returns 0 if the bstring could be successfully parsed, otherwise -1.
 */
int hpx_process_elem(bstring_t b, hpx_tag_t *p)
{
   if (b.len && (*b.buf != '<'))
   {
      p->type = HPX_LITERAL;
      p->tag = b;
      return 0;
   }

   p->type = HPX_ILL;

   if (!bs_advance(&b))
      return -1;

   if (!skip_bblank(&b))
      return -1;

   //if (isalpha(*b.buf) || (*b.buf == '_') || (*b.buf == ':'))
   if (IS_XML1CHAR((unsigned) *b.buf))
   {
      hpx_parse_name(&b, &p->tag);
      hpx_parse_attr_list(&b, p);

      if (!skip_bblank(&b))
         return -1;

      if (*b.buf == '>')
      {
         p->type = HPX_OPEN;
         // call tag processor
         return 0;
      }

      if (*b.buf != '/')
         return -1;

      if (!bs_advance(&b))
         return -1;

      if (!skip_bblank(&b))
         return -1;

      if (*b.buf != '>')
         return -1;

      p->type = HPX_SINGLE;
      //call tag processor
      return 0;
   }

   if (*b.buf == '/')
   {
      if (!bs_advance(&b))
         return -1;

      if (!skip_bblank(&b))
         return -1;

      hpx_parse_name(&b, &p->tag);

      if (!skip_bblank(&b))
         return -1;

      if (*b.buf != '>')
         return -1;

      p->type = HPX_CLOSE;
      //call tag processor
      return 0;
   }

   if (*b.buf == '!')
   {
      bs_advance(&b);

      // check for comment
      if ((b.len >= 2) && !strncmp(b.buf, "--", 2))
      {
         b.buf += 2;
         b.len -= 2;
         p->tag.buf = b.buf;

         // find end marker
         for (p->tag.len = 0; (b.len >= 3) && strncmp(b.buf, "-->", 3); bs_advance(&b), p->tag.len++);

         if (b.len < 3)
            return -1;

         p->type = HPX_COMMENT;
         //call tag processor
         return 0;
      }

      // check for CDATA
      if ((b.len >= 7) && !strncmp(b.buf, "[CDATA[", 7))
      {
         b.buf += 7;
         b.len -= 7;
         p->tag.buf = b.buf;

         // find end marker
         for (p->tag.len = 0; (b.len >= 3) && strncmp(b.buf, "]]>", 3); bs_advance(&b), p->tag.len++);

         if (b.len < 3)
            return -1;

         p->type = HPX_CDATA;
         //call tag processor
         return 0;
      }

      if (b.len)
         b.len--;
      p->tag = b;
      p->type = HPX_ATT;
      //call tag processor
      return 0;
   }

   if (*b.buf == '?')
   {
      bs_advance(&b);
      hpx_parse_name(&b, &p->tag);
      hpx_parse_attr_list(&b, p);

      if (!skip_bblank(&b))
         return -1;

      if ((b.len >= 2) && !strncmp(b.buf, "?>", 2))
      {
         p->type = HPX_INSTR;
         // call tag processor
         return 0;
      }
      return -1;
   }

   // FIXME: return value correct?
   return -1;
}


/*! Checks for XML white spaces ([\t\n\r]) and increases the line number counter.
 * @param c Pointer to character.
 * @return Returns 0 if character contains any of [ \t\r\n], otherwise 1.
 */
int cblank(const char *c, long *lno)
{
   switch (*c)
   {
      case '\n':
         if (lno != NULL)
            (*lno)++;
         /* fall through */
      case '\t':
      case '\r':
      case ' ':
         return 0;
   }

   return 1;
}


/*! Returns length of tag.
 *  @param buf Pointer to buffer.
 *  @param len Length of buffer.
 *  @param lno Pointer to line number counter. May be NULL.
 *  @return Lendth of tag content including '<' and '>'. If return value > len,
 *  the tag is unclosed.
 */
int count_tag(bstringl_t b, long *lno)
{
#define HPX_DOCTYPE 0x100
   int i, c = HPX_ILL, sqcnt = 0;

   // manage comments
   if ((b.len >= 7) && !strncmp(b.buf + 1, "!--", 3))
      c = HPX_COMMENT;
   // manage CDATA
   if ((b.len >= 12) && !strncmp(b.buf + 1, "![CDATA[", 8))
      c = HPX_CDATA;
   // manage DOCTYPE sub entities
   if ((b.len >= 10) && !strncasecmp(b.buf + 1 , "!DOCTYPE", 8) && (isspace(b.buf[9]) || (b.buf[9] == '>')))
      c = HPX_DOCTYPE;

   for (i = 0; i < b.len; i++, b.buf++)
   {
      if (*b.buf == '>')
      {
         if (c == HPX_ILL)
            break;
         if ((c == HPX_COMMENT) && (i >= 7) && !strncmp(b.buf - 2, "--", 2))
            break;
         if ((c == HPX_CDATA) && (i >= 12) && !strncmp(b.buf - 2, "]]", 2))
            break;
         if ((c == HPX_DOCTYPE) && !sqcnt)
            break;
         else
            continue;
      }
      // count square brackets in DOCTYPE
      else if (c == HPX_DOCTYPE)
         switch (*b.buf)
         {
            case '[':
               sqcnt++;
               break;

            case ']':
               sqcnt--;
               break;
         }

      (void) cblank(b.buf, lno);
   }

   return i + 1;
}


/*! Returns length of literal.
 *  @param b Bstring_t of buffer to check.
 *  @param nbc Pointer to integer which counts non-blank characters.
 *  @param lno Pointer to line number counter. May be NULL.
 *  @return Length of literal. Return value == len if literal is unclosed.
 */
int count_literal(bstringl_t b, int *nbc, long *lno)
{
   int i, t;

   if (nbc != NULL)
      *nbc = 0;
   else
      nbc = &t;

   for (i = 0; i < b.len; i++, b.buf++)
   {
      if (*b.buf == '<')
         break;

      *nbc += cblank(b.buf, lno);
   }

   return i;
}


/*! Parse XML element into bstring.
 *  @param ctl Hpx control structure.
 *  @param b Pointer to bstring.
 *  @param lno Pointer to integer which will receive the line number of the
 *  element. lno may be NULL.
 *  @return Length of element or -1 if element is unclosed.
 */
int hpx_proc_buf(hpx_ctrl_t *ctl, bstringl_t *b, long *lno)
{
   int i, s, n;

   if (ctl->in_tag)
   {
      if (lno != NULL)
         *lno = ctl->lineno;
      s = count_tag(*b, &ctl->lineno);
      if (s > b->len)
         return -1;
      b->len = s;

      // check if it is a regular opening tag and preserve its name
      if (IS_XML1CHAR(b->buf[1]))
      {
         ctl->last_open.buf = b->buf + 1;
         for (ctl->last_open.len = 1; IS_XMLCHAR((unsigned) ctl->last_open.buf[ctl->last_open.len]) && (ctl->last_open.len < b->len - 2); ctl->last_open.len++);
      }
      else
         ctl->last_open.len = 0;
   }
   else
   {
      if (lno != NULL)
         *lno = ctl->lineno;

      s = count_literal(*b, &n, &ctl->lineno);
      // check if literal had no end tag (i.e. '<')
      if (s == b->len)
         return -1;

      // !(check if we are within an opening tag and there's the corresponding closing tag)
      if (!ctl->last_open.len || (b->len - s <= ctl->last_open.len + 2) || (b->buf[s + 1] != '/') || strncmp(&b->buf[s + 2], ctl->last_open.buf, ctl->last_open.len))
      {
         // reset line number
         if (lno != NULL)
            ctl->lineno = *lno;
         // skip leading white spaces
         for (i = 0; i < b->len && !cblank(b->buf, &ctl->lineno); i++)
            bs_advancel(b);
         if (i == b->len)
            return -1;

         if (lno != NULL)
            *lno = ctl->lineno;

         // cut trailing white spaces
         for (b->len = s - i; b->len && isspace((unsigned) b->buf[b->len - 1]); b->len--);
      }
      else
         b->len = s;

      ctl->last_open.len = 0;
   }

   return s;
}


/*! This function is wrapper for either madvise() or posix_madvise().
 */
static int hpx_madvise(void *addr, size_t length, int advice)
{
#ifdef HAVE_MADVISE
   return madvise(addr, length, advice);
#elif HAVE_POSIX_MADVISE
   return posix_madvise(addr, length, advice);
#else
   return 0;
#endif
}


/*! This function initializes the control structure which is the foundation for
 * all other functions. Please note that memory mapping is the recommended
 * method (see parameter len).
 *  @param fd Input file descriptor.
 *  @param len Read buffer length. If len is negative, the file is memory
 *  mapped with mmap(). This works only if it was compiled with WITH_MMAP.
 *  @return Pointer to allocated hpx_ctrl_t structure. On error NULL is
 *  returned and errno is set. If compiled without WITH_MMAP and hpx_init() is
 *  called with negative len parameter, NULL is returned and errno is set to
 *  EINVAL.
 */
hpx_ctrl_t *hpx_init(int fd, long len)
{
   hpx_ctrl_t *ctl;

   if ((ctl = malloc(sizeof(*ctl) + (len < 0 ? 0 : len))) == NULL)
      return NULL;

   memset(ctl, 0, sizeof(*ctl));
   ctl->fd = fd;
   // init line counter
   ctl->lineno = 1;

   if (len < 0)
   {
#ifdef WITH_MMAP
      ctl->len = ctl->buf.len = -len;
      if ((ctl->buf.buf = mmap(NULL, ctl->len, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, fd, 0)) == MAP_FAILED)
      {
         free(ctl);
         return NULL;
      }
      ctl->mmap = 1;
      ctl->madv_ptr = ctl->buf.buf;
      if ((ctl->pg_siz = sysconf(_SC_PAGESIZE)) == -1)
      {
         log_msg(LOG_ERR, "sysconf() failed: %s", strerror(errno));
         ctl->pg_siz = 0;
      }
      ctl->pg_blk_siz = ctl->pg_siz * MMAP_PAGES;
      log_msg(LOG_INFO, "system pagesize = %ld kB (read ahead %ld kB)",
            ctl->pg_siz / 1024, ctl->pg_blk_siz / 1024);

      // advise 1st block
      if (hpx_madvise(ctl->madv_ptr,
               ctl->pg_blk_siz <= ctl->len ? ctl->pg_blk_siz : ctl->len,
                     MADV_WILLNEED) == -1)
         log_msg(LOG_ERR, "madvise(%p, %ld, MADV_WILLNEED) failed: %s",
               ctl->madv_ptr, ctl->pg_blk_siz, strerror(errno));

      return ctl;
#else
      errno = EINVAL;
      free(ctl);
      return NULL;
#endif
   }

   ctl->buf.buf = (char*) (ctl + 1);
   ctl->len = len;
   ctl->empty = 1;

   return ctl;
}


/*! This function initializes a hpx_ctrl_t structure to be used for input from
 * a memory buffer (instead of a file).
 * @param ctl Pointer to hpx_ctrl_t structure which will be initialized
 *    properly.
 * @param buf Pointer to memory buffer.
 * @param len Number of bytes within memory buffer.
 */
void hpx_init_membuf(hpx_ctrl_t *ctl, void *buf, int len)
{
   memset(ctl, 0, sizeof(*ctl));
   ctl->buf.len = len;
   ctl->buf.buf = buf;
   ctl->fd = -1;
   ctl->len = len;
   ctl->lineno = 1;
}


void hpx_free(hpx_ctrl_t *ctl)
{
#ifdef WITH_MMAP
   if (ctl->mmap)
      // FIXME returned code should be checked
      (void) munmap(ctl->buf.buf, ctl->len);
#endif
   free(ctl);
}


/*!
 *  @param ctl Pointer to valid hpx_ctrl_t structure.
 *  @param b Pointer to bstring_t. This structure will be filled out by this
 *  function.
 *  @param in_tag is set to 1 or 0, either it is a tag or not. It is optional
 *  and may be NULL.
 *  @param lno Pointer to integer which will contain the starting line number
 *  of b. lno may be NULL.
 *  @return Length of element (always >= 1) if everything is ok. b will contain
 *  a valid bstring to the element. -1 is returned in case of error. On eof, 0
 *  is returned.
 */
long hpx_get_eleml(hpx_ctrl_t *ctl, bstringl_t *b, int *in_tag, long *lno)
{
   long s;

   for (;;)
   {
#ifdef WITH_MMAP
      if (ctl->mmap)
      {
         if ((ctl->buf.buf + ctl->pos) >= ctl->madv_ptr)
         {
            // pull in next block if it is available
            if (ctl->buf.buf + ctl->len > ctl->madv_ptr + ctl->pg_blk_siz)
            {
               s = ctl->len - ctl->pos - ctl->pg_blk_siz >= ctl->pg_blk_siz ?
                     ctl->pg_blk_siz : ctl->len - ctl->pos - ctl->pg_blk_siz;
               if (hpx_madvise(ctl->madv_ptr + ctl->pg_blk_siz, s, MADV_WILLNEED) == -1)
                  log_msg(LOG_ERR, "madvise(%p, %ld, MADV_WILLNEED) failed: %s",
                        ctl->madv_ptr + ctl->pg_blk_siz, s, strerror(errno));
            }
            // mark previous block as unneeded
            if (ctl->madv_ptr - ctl->pg_blk_siz >= ctl->buf.buf)
            {
               if (hpx_madvise(ctl->madv_ptr - ctl->pg_blk_siz, ctl->pg_blk_siz, MADV_DONTNEED) == -1)
                  log_msg(LOG_ERR, "madvise(%p, %ld, MADV_DONTNEED) failed: %s",
                        ctl->madv_ptr - ctl->pg_blk_siz, ctl->pg_blk_siz, strerror(errno));
            }
            ctl->madv_ptr += ctl->pg_blk_siz;
         }
     }
#endif

      if (ctl->empty)
      {
         if (ctl->mmap)
         {
            ctl->eof = 1;
            log_msg(LOG_DEBUG, "end of memory mapped area, buf.len = %ld, len = %ld, pos = %ld",
                  ctl->buf.len, ctl->len, ctl->pos);
         }
         else
         {
            // move remaining data to the beginning of the buffer
            ctl->buf.len -= ctl->pos;
            memmove(ctl->buf.buf, ctl->buf.buf + ctl->pos, ctl->buf.len);
            ctl->pos = 0;

            // read new data from file (but not the mem buffer, i.e. fd == -1)
            for (s = 0; ctl->fd != -1;)
            {
               if ((s = read(ctl->fd, ctl->buf.buf + ctl->buf.len, ctl->len - ctl->buf.len)) != -1)
                  break;

               if (errno != EINTR)
                  return -1;
            }

            if (!s)
               ctl->eof = 1;

            // adjust position pointers
            ctl->buf.len += s;
         }
         ctl->empty = 0;
      }

      // no more data available
      if (!ctl->buf.len)
         return 0;

      b->buf = ctl->buf.buf + ctl->pos;
      b->len = ctl->buf.len - ctl->pos;

      if ((s = hpx_proc_buf(ctl, b, lno)) >= 0)
      {
         if (in_tag != NULL)
            *in_tag = ctl->in_tag;

         ctl->in_tag ^= 1;
         ctl->pos += s;

         // test if empty element (literal)
         if (!b->len)
            continue;

         return b->len;
      }

      if (ctl->eof)
         return 0;

      ctl->empty = 1;
   }
}


int hpx_get_elem(hpx_ctrl_t *ctl, bstring_t *b, int *in_tag, long *lno)
{
   long e;
   bstringl_t bl;

   if ((e = hpx_get_eleml(ctl, &bl, in_tag, lno)) <= 0)
      return e;

   if (bl.len > INT_MAX)
   {
      errno = ERANGE;
      return -1;
   }

   b->len = bl.len;
   b->buf = bl.buf;
   return e;
}


int hpx_fprintf_attr(FILE *f, const hpx_attr_t *a, const char *lead)
{
   //FIXME: escaping of ['"] missing
   return fprintf(f, "%s%.*s=%c%.*s%c", lead == NULL ? "" : lead, a->name.len,
         a->name.buf, a->delim, a->value.len, a->value.buf, a->delim);
}

int hpx_fprintf_tag(FILE *f, const hpx_tag_t *p)
{
   int i, n;
   char *s = "";

   switch (p->type)
   {
      case HPX_CLOSE:
         return fprintf(f, "</%.*s>\n", p->tag.len, p->tag.buf);

      case HPX_SINGLE:
         s = "/";
         /* fall through */
      case HPX_OPEN:
         n = fprintf(f, "<%.*s", p->tag.len, p->tag.buf);
         for (i = 0; i < p->nattr; i++)
            n += hpx_fprintf_attr(f, &p->attr[i], " ");
         return n + fprintf(f, "%s>\n", s);

      case HPX_INSTR:
         n = fprintf(f, "<?%.*s", p->tag.len, p->tag.buf);
         for (i = 0; i < p->nattr; i++)
            n += hpx_fprintf_attr(f, &p->attr[i], " ");
         return n + fprintf(f, "?>\n");
 
   }
   return 0;
}


/*! Resize tag tree.
 *  @param n Number of sub tags to add to tree.
 */
int hpx_tree_resize(hpx_tree_t **tl, int n)
{
   int m;
   hpx_tree_t *t;

   m = *tl == NULL ? 0 : (*tl)->msub;

   if ((t = realloc(*tl, sizeof(hpx_tree_t) + (m + n)  * sizeof(hpx_tag_t*))) == NULL)
      return -1;

  t->msub = n + m;
   *tl = t;

   for (; m < t->msub; m++)
      (*tl)->subtag[m] = NULL;
 
   return t->msub;
}

