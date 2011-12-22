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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "bstring.h"
#include "libhpxml.h"


static size_t hpx_lineno_;


size_t hpx_lineno(void)
{
   return hpx_lineno_;
}


/*! 
 *  @param b Pointer to bstring.
 *  @return Number of remaining characters in b.
 */
int skip_bblank(bstring_t *b)
{
   for (; isblank(*b->buf) && b->len; bs_advance(b));
   return b->len;
}


void hpx_tm_free(hpx_tag_t *t)
{
   free(t);
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
   if (!IS_XML1CHAR(*b->buf))
      return 0;

   n->buf = b->buf;
   bs_advance(b);
   for (n->len = 1; IS_XMLCHAR(*b->buf) && b->len; bs_advance(b), n->len++);
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
 * @param in Must be set to 0 if element is literal text, otherwise 1 if it is
 * a tag.
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
   if (IS_XML1CHAR(*b.buf))
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
      if ((b.len >= 2) && !strncmp(b.buf, "--", 2))
      {
         b.buf += 2;
         b.len -= 2;
         p->tag.buf = b.buf;

         for (p->tag.len = 0; (b.len >= 3) && strncmp(b.buf, "-->", 3); bs_advance(&b), p->tag.len++);

         if (b.len < 3)
            return -1;

         p->type = HPX_COMMENT;
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


/*! Changes white spaces ([\t\n\r]) to space ([ ]).
 * @param c Pointer to character.
 * @return Returns 0 if character contains any of [ \t\r\n], otherwise 1.
 */
int cblank(char *c)
{
   switch (*c)
   {
      case '\n':
         hpx_lineno_++;
      case '\t':
      case '\r':
         *c = ' ';
      case ' ':
         return 0;
   }

   return 1;
}


/*! Returns length if tag.
 *  @param buf Pointer to buffer.
 *  @param len Length of buffer.
 *  @return Lendth of tag content including '<' and '>'. If return value > len,
 *  the tag is unclosed.
 */
int count_tag(bstring_t b)
{
   int i, c = 0;

   if ((b.len >= 7) && !strncmp(b.buf + 1, "!--", 3))
      c = 1;

   for (i = 0; i < b.len; i++, b.buf++)
   {
      if (*b.buf == '>')
      {
         if (!c)
            break;
         if ((i >= 7) && !strncmp(b.buf - 2, "--", 2))
            break;
         else
            continue;
      }
      (void) cblank(b.buf);
   }

   return i + 1;
}


/*! Returns length of literal.
 *  @param b Bstring_t of buffer to check.
 *  @param nbc Pointer to integer which counts non-blank characters.
 *  @return Length of literal. Return value == len if literal is unclosed.
 */
int count_literal(bstring_t b, int *nbc)
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

      *nbc += cblank(b.buf);
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
int hpx_proc_buf(hpx_ctrl_t *ctl, bstring_t *b, size_t *lno)
{
   int i, s, n;

   if (ctl->in_tag)
   {
      if (lno != NULL)
         *lno = hpx_lineno_;
      s = count_tag(*b);
      if (s > b->len)
         return -1;
      b->len = s;
   }
   else
   {
      // skip leading white spaces
      for (i = 0; i < b->len && !cblank(b->buf); i++)
         bs_advance(b);
      if (i == b->len)
         return -1;

      if (lno != NULL)
         *lno = hpx_lineno_;
      s = count_literal(*b, &n);
      // check if literal had no end tag (i.e. '<')
      if (s == b->len)
         return -1;

      // cut trailing white spaces
      for (b->len = s; b->len && (b->buf[b->len - 1] == ' '); b->len--);

      s += i;
   }

   return s;
}

/*
int hpx_buf_reader(int fd, char *buf, int buflen)
{
   return -1;
}
*/

/*!
 *  @param fd Input file descriptor.
 *  @param len Read buffer length.
 *  @param mattr Maximum number of attributes per tag.
 */
hpx_ctrl_t *hpx_init(int fd, int len)
{
   hpx_ctrl_t *ctl;

   if ((ctl = malloc(sizeof(*ctl) + len)) == NULL)
      return NULL;

   memset(ctl, 0, sizeof(*ctl));
   ctl->buf.buf = (char*) (ctl + 1);
   ctl->len = len;
   ctl->empty = 1;
   ctl->fd = fd;
 
   // init line counter
   hpx_lineno_ = 1;

   return ctl;
}


void hpx_free(hpx_ctrl_t *ctl)
{
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
int hpx_get_elem(hpx_ctrl_t *ctl, bstring_t *b, int *in_tag, size_t *lno)
{
   int s;

   for (;;)
   {
      if (ctl->empty)
      {
         // move remaining data to the beginning of the buffer
         ctl->buf.len -= ctl->pos;
         memmove(ctl->buf.buf, ctl->buf.buf + ctl->pos, ctl->buf.len);
         ctl->pos = 0;

         // read new data from file
         for (;;)
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
         ctl->empty = 0;
      }

      // no more data available
      if (!ctl->buf.len)
         // FIXME: return value questionable (-1 eq error)
         return -1;

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
