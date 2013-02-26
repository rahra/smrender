#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "smrender.h"
#include "bstring.h"


#define NEXT_TOKEN(x) if (!next_token(x)) return 0


enum {CMP_NE, CMP_GE, CMP_LE, CMP_REGEX, CMP_EQ, CMP_GT, CMP_LT};
enum {MCSS_CANVAS = OSM_REL + 1, MCSS_AREA, MCSS_LINE, MCSS_ANY};


static const char *EMPTY_ = "";
static const char *CMP_[] = {"!=", ">=", "<=", "=~", "=", ">", "<", NULL};


static int line_count(char c)
{
   static int lineno = 1;

   if (c == '\n')
      lineno++;
   return lineno;
}


static int skip_bblank(bstring_t *b)
{
   for (; isspace((unsigned) *b->buf) && b->len; bs_advance(b))
      line_count(*b->buf);
   return b->len;
}


static int skip_c_comment(bstring_t *b)
{
   for (; b->len; bs_advance(b))
   {
      line_count(*b->buf);
      if (b->len >= 2 && !strncmp("*/", b->buf, 2))
      {
         bs_nadvance(b, 2);
         break;
      }
   }
   return b->len;
}


static int skip_cxx_comment(bstring_t *b)
{
   for (; b->len; bs_advance(b))
   {
      line_count(*b->buf);
      if (*b->buf == '\n')
      {
         bs_advance(b);
         break;
      }
   }
   return b->len;
}


static int skip_comment(bstring_t *b)
{
   if (b->len < 2)
      return b->len;

   if (!bs_ncmp(*b, "//", 2))
      return skip_cxx_comment(b);

   if (!bs_ncmp(*b, "/*", 2))
      return skip_c_comment(b);

   return b->len;
}


static int skip_to_char(bstring_t *b, char c)
{
   for (; b->len && *b->buf != c; bs_advance(b))
      skip_comment(b);
   return b->len;
}


static int next_token(bstring_t *b)
{
   int l;

   for (;;)
   {
      l = b->len;
      if (!skip_bblank(b))
         return 0;
      if (!skip_comment(b))
         return 0;
      if (l != b->len)
         continue;
      return b->len;
   }
}


static int isword(int c)
{
   return isalpha(c) || isdigit(c) || c == '\\' || c == '*' || c == '-' || c == '_' || c == '#';
}


static int read_word(bstring_t *src, bstring_t *dst)
{
   char delim;

   //skip_bblank(src);
   switch (*src->buf)
   {
      case '\'':
      case '"':
         delim = *src->buf;
         bs_advance(src);
         break;
      default:
         delim = 0;
   }
   for (dst->buf = src->buf, dst->len = 0; *src->buf && isword(*src->buf) && *src->buf != delim; src->buf++, dst->len++)
   {
      // check for escape character ('\\')
      if (*src->buf == '\\')
      {
         // '\\' must not be the last character
         if (src->len < 2)
            return -1;
         src->buf++;
         dst->len++;
      }
   }
   if (delim)
   {
      // start delimiter must be the same as end delimiter
      if (*src->buf != delim)
         return -1;
      bs_advance(src);
   }
   return dst->len;
}


static int read_cmp(bstring_t *src)
{
   int i;

   for (i = 0; CMP_[i] != NULL; i++)
   {
      if (!bs_ncmp(*src, CMP_[i], strlen(CMP_[i])))
      {
         bs_nadvance(src, strlen(CMP_[i]));
         return i;
      }
   }

   return -1;
}


// FIXME: return value questionable
static int read_css(bstring_t *src, bstring_t *k, bstring_t *v)
{
   //NEXT_TOKEN(src);
   read_word(src, k);
   NEXT_TOKEN(src);
   if (*src->buf != ':')
      return -1;
   bs_advance(src);
   NEXT_TOKEN(src);
   read_word(src, v);
   NEXT_TOKEN(src);
   return k->len + v->len;
}


// FIXME: return value questionable
static int read_tag(bstring_t *src, bstring_t *k, bstring_t *v)
{
   int cmp;

   if (*src->buf != '[')
      return -1;
   bs_advance(src);
   NEXT_TOKEN(src);
   read_word(src, k);
   NEXT_TOKEN(src);
   if (*src->buf == ']')
   {
      v->buf = (char*) EMPTY_;
      v->len = 0;
      bs_advance(src);
      return k->len;
   }
   if ((cmp = read_cmp(src)) == -1)
      return -1;
   NEXT_TOKEN(src);
   read_word(src, v);
   NEXT_TOKEN(src);
   if (*src->buf != ']')
      return -1;
   bs_advance(src);

   return k->len + v->len;
}


static int close_osm_node(int type)
{
   switch (type)
   {
      case OSM_NODE:
         return printf("</node>\n");
      case OSM_WAY:
         return printf("</way>\n");
      case OSM_REL:
         return printf("</relation>\n");
      default:
         return printf("<!-- no closing tag for type %d -->\n", type);
   }
}


static int open_osm_node(bstring_t b)
{
   if (!bs_cmp(b, "way"))
   {
      printf("<way>\n");
      return OSM_WAY;
   }
   if (!bs_cmp(b, "node"))
   {
      printf("<node>\n");
      return OSM_NODE;
   }
   if (!bs_cmp(b, "canvas"))
   {
      printf("<!-- canvas -->\n");
      return MCSS_CANVAS;
   }
   if (!bs_cmp(b, "line"))
   {
      printf("<!-- line -->\n");
      return MCSS_LINE;
   }
   if (!bs_cmp(b, "area"))
   {
      printf("<!-- area -->\n");
      return MCSS_AREA;
   }
   if (!bs_cmp(b, "*"))
   {
      printf("<!-- * -->\n");
      return MCSS_ANY;
   }
   return -1;
}


/*! This code is rendundant! It is found in smrender.c as well and should be
moved to libsmrender. */
static int bs_safe_put_xml(FILE *f, const bstring_t *b)
{
   int i, c;

   for (i = 0, c = 0; i < b->len; i++)
      switch (b->buf[i])
      {
         case '"':
            c += fputs("&quot;", f);
            break;
         case '<':
            c += fputs("&lt;", f);
            break;
         default:
            c += fputc(b->buf[i], f);
      }
   return c;
}


static int print_osm_tag(const bstring_t *k, const bstring_t *v)
{
   int l;

   l = printf("<tag k=\"");
   l += bs_safe_put_xml(stdout, k);
   l += printf("\" v=\"");
   l += bs_safe_put_xml(stdout, v);
   l += printf("\"/>\n");
   return l;
}


static int read_mcss_elem(bstring_t *src)
{
   bstring_t b, k, v;
   int e, t;

   NEXT_TOKEN(src);

   if ((e = read_word(src, &b)) <= 0)
      return e;

   if ((t = open_osm_node(b)) == -1)
      return -1;

   NEXT_TOKEN(src);

   if (t != MCSS_CANVAS)
   {
      for (; *src->buf == '[';)
      {
         if ((e = read_tag(src, &k, &v)) <= 0)
            return e;
         print_osm_tag(&k, &v);
         NEXT_TOKEN(src);
      }
   }

   if (*src->buf != '{')
      return -1;
   bs_advance(src);

   NEXT_TOKEN(src);

   for (;*src->buf != '}';)
   {
      if ((e = read_css(src, &k, &v)) <= 0)
         return e;

      if (!bs_cmp(k, "fill-color"))
      {
         printf("<tag k=\"_action_\" v=\"draw:color=%.*s\"/>\n", v.len, v.buf);
      }
      else
         printf("<!-- %.*s: %.*s -->\n", k.len, k.buf, v.len, v.buf);

      if (*src->buf == ';')
      {
         bs_advance(src);
         NEXT_TOKEN(src);
      }
   }

   if (*src->buf == '}')
   {
      bs_advance(src);
      NEXT_TOKEN(src);
   }

   close_osm_node(t);

   return src->len;
}


static int parse_mcss_buf(bstring_t buf)
{
   int e;

   while ((e = read_mcss_elem(&buf)) > 0);
   if (e == -1)
      fprintf(stderr, "*** syntax error in line %d\n", line_count(0));
   return 0;
}


static int init_read_buf(int fd, bstring_t *buf)
{
   struct stat st;

   if (fstat(fd, &st) == -1)
      return -1;

   buf->len = st.st_size;
   if ((buf->buf = mmap(NULL, buf->len, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
   {
      perror("mmap");
      return -1;
   }

   return buf->len;
}


int main(int argc, char **argv)
{
   bstring_t buf;

   if (init_read_buf(0, &buf) == -1)
      perror("init_read_buf"), exit(EXIT_FAILURE);

   parse_mcss_buf(buf);

   return 0;
}


