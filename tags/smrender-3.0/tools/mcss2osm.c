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


#define SYNTAX_RELAXED
#define NEXT_TOKEN(x) if (!next_token(x)) return 0


typedef struct mcss_tag
{
   bstring_t k;
   bstring_t v;
   int cmp;
} mcss_tag_t;

typedef struct mcss_obj
{
   int type;
   int subtype;
   int tag_cnt;
   int zs, ze;
   mcss_tag_t *tag;
} mcss_obj_t;


enum {CMP_NE, CMP_GE, CMP_LE, CMP_REGEX, CMP_EQ, CMP_GT, CMP_LT};
enum {MCSS_CANVAS = OSM_REL + 1, MCSS_AREA, MCSS_LINE, MCSS_ANY};


static const char *EMPTY_ = "";
static const char *CMP_[] = {"!=", ">=", "<=", "=~", "=", ">", "<", NULL};
static mcss_obj_t *stack_ = NULL;
static int cnt_ = 0;


static int mcss_obj_add_tag(mcss_obj_t *obj, const mcss_tag_t *tag)
{
   if (obj == NULL)
      return -1;
   if (tag == NULL)
      return obj->tag_cnt;
   if ((obj->tag = realloc(obj->tag, sizeof(*obj->tag) * (obj->tag_cnt + 1))) == NULL)
      perror("realloc"), exit(EXIT_FAILURE);
   obj->tag[obj->tag_cnt] = *tag;
   return ++obj->tag_cnt;
}


static int mcss_push(const mcss_obj_t *obj)
{
   if (obj == NULL)
      return cnt_;

   if ((stack_ = realloc(stack_, sizeof(*stack_) * (cnt_ + 1))) == NULL)
      perror("realloc"), exit(EXIT_FAILURE);

   stack_[cnt_] = *obj;
   return ++cnt_;
}


static int mcss_pop(mcss_obj_t *obj)
{
   if (obj == NULL)
      return cnt_;

   if (cnt_ <= 0)
      return -1;

   cnt_--;
   *obj = stack_[cnt_];
   return cnt_;
}


static void mcss_free(void)
{
   free(stack_);
   stack_ = NULL;
}


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
   {
      line_count(*b->buf);
      skip_comment(b);
   }
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
   // FIXME: is inclusion of ',' dangerous?
   return isalpha(c) || isdigit(c) || c == '\\' || c == '*' || c == '-' || c == '_' || c == '#' || c == '.' || c == '/' || c== ',';
}


static int read_number(bstring_t *src, bstring_t *dst)
{
   for (dst->buf = src->buf, dst->len = 0; src->len && isdigit(*src->buf); bs_advance(src), dst->len++);
   return src->len;
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
   // FIXME: src->len does not decrease?
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
   return src->len;
}


static int read_tag(bstring_t *src, mcss_tag_t *tag)
{
   if (*src->buf != '[')
      return -1;
   bs_advance(src);
   NEXT_TOKEN(src);
   read_word(src, &tag->k);
   NEXT_TOKEN(src);
   if (*src->buf == ']')
   {
      tag->v.buf = (char*) EMPTY_;
      tag->v.len = 0;
      tag->cmp = CMP_EQ;
      bs_advance(src);
      return tag->cmp;
   }
   if ((tag->cmp = read_cmp(src)) == -1)
      return -1;
   NEXT_TOKEN(src);
   read_word(src, &tag->v);
   NEXT_TOKEN(src);
   if (*src->buf != ']')
      return -1;
   bs_advance(src);

   return tag->cmp;
}


static int osm_xml_tag(int type, int open)
{
   char *s = open ? (char*) EMPTY_ : "/";

   switch (type)
   {
      case OSM_NODE:
         return printf("<%snode>\n", s);
      case OSM_WAY:
         return printf("<%sway>\n", s);
      case OSM_REL:
         return printf("<%srelation>\n", s);
      default:
         return printf("<!-- no OSM/XML tag for type %d -->\n", type);
   }
}


static int close_osm_node(int type)
{
   return osm_xml_tag(type, 0);
}


static int open_osm_node(int type)
{
   return osm_xml_tag(type, 1);
}


static int parse_osm_node(bstring_t b)
{
   if (!bs_cmp(b, "way"))
   {
      //printf("<way>\n");
      return OSM_WAY;
   }
   if (!bs_cmp(b, "node"))
   {
      //printf("<node>\n");
      return OSM_NODE;
   }
   if (!bs_cmp(b, "canvas"))
   {
      //printf("<!-- canvas -->\n");
      return MCSS_CANVAS;
   }
   if (!bs_cmp(b, "line"))
   {
      //printf("<!-- line -->\n");
      return MCSS_LINE;
   }
   if (!bs_cmp(b, "area"))
   {
      //printf("<!-- area -->\n");
      return MCSS_AREA;
   }
   if (!bs_cmp(b, "*"))
   {
      //printf("<!-- * -->\n");
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


//static int print_osm_tag(const bstring_t *k, const bstring_t *v, int cmp)
static int print_osm_tag(const mcss_tag_t *tag)
{
   char *sc, *ec;
   int l;

   switch (tag->cmp)
   {
      case CMP_REGEX:
         sc = ec = "/";
         break;
      case CMP_LT:
      case CMP_LE:
         sc = "[";
         ec = "]";
         break;
      case CMP_GT:
      case CMP_GE:
         sc = "]";
         ec = "[";
         break;
      case CMP_NE:
         sc = ec = "~";
         break;
      default:
         sc = ec = (char*) EMPTY_;
   }


   l = printf("   <tag k=\"");
   l += bs_safe_put_xml(stdout, &tag->k);
   l += printf("\" v=\"%s", sc);
   l += bs_safe_put_xml(stdout, &tag->v);
   l += printf("%s\"/>\n", ec);
   return l;
}


static int print_action(mcss_tag_t *tag, int cnt)
{
   int l = 0;

   l += printf("   <tag k=\"_action_\" v=\"mapcss:");
   for (; cnt; tag++, cnt--)
   {
      l += printf("%.*s=%.*s;", tag->k.len, tag->k.buf, tag->v.len, tag->v.buf);
#if 0
      if (!bs_cmp(tag->k, "fill-color"))
      {
         l += printf("   <tag k=\"_action_\" v=\"draw:color=%.*s\"/>\n", tag->v.len, tag->v.buf);
      }
      else
         l += printf("   <!-- %.*s: %.*s -->\n", tag->k.len, tag->k.buf, tag->v.len, tag->v.buf);
#endif
   }
   l += printf("\"/>\n");

   return l;
}


static int print_zoom(int zs, int ze)
{
   return printf("   <tag k=\"zoom:start\" v=\"%d\"/>\n   <tag k=\"zoom:end\" v=\"%d\"/>\n", zs, ze);
}


static int read_mcss_obj(bstring_t *src)
{
   mcss_obj_t obj;
   mcss_tag_t tag;
   bstring_t b;
   int e;

   memset(&obj, 0, sizeof(obj));
   NEXT_TOKEN(src);
   if ((e = read_word(src, &b)) <= 0)
      return e;

   if ((obj.type = parse_osm_node(b)) == -1)
      return -1;

   // ignore zoom levels
   if (*src->buf == '|')
   {
      bs_advance(src);
      NEXT_TOKEN(src);
      if (*src->buf != 'z')
         return -1;
      bs_advance(src);
      if (*src->buf != '-')
      {
         if (read_number(src, &b) <= 0)
            return -1;
         obj.zs = bs_tol(b);
         NEXT_TOKEN(src);
      }
      if (*src->buf != '-')
      {
         obj.ze = obj.zs;
      }
      else
      {
         bs_advance(src);
         NEXT_TOKEN(src);
         if (read_number(src, &b) <= 0)
            return -1;
         obj.ze = bs_tol(b);
         NEXT_TOKEN(src);
      }
   }

   NEXT_TOKEN(src);

   if (obj.type != MCSS_CANVAS)
   {
      for (; *src->buf == '[';)
      {
         if (read_tag(src, &tag) < 0)
            return tag.cmp;
         //print_osm_tag(&k, &v, e);
         mcss_obj_add_tag(&obj, &tag);
         NEXT_TOKEN(src);
      }
   }

   // ignore area
   if (*src->buf == ':')
   {
      bs_advance(src);
      NEXT_TOKEN(src);
      if (bs_ncmp(*src, "area", 4))
      {
         fprintf(stderr, "*** unknown token \"%.*s\" in line %d\n", 4, src->buf, line_count(0));
         return -1;
      }
      obj.subtype = MCSS_AREA;
      bs_nadvance(src, 4);
      NEXT_TOKEN(src);
   }

   mcss_push(&obj);
   return src->len;
}

 
static int read_mcss_elem(bstring_t *src)
{
   mcss_obj_t obj, css;
   mcss_tag_t tag;
   int e, i;

   if ((e = read_mcss_obj(src)) <= 0)
      return e;

   while (*src->buf == ',')
   {
      bs_advance(src);

#ifdef SYNTAX_RELAXED
      // handle special case where no object follows after the ','
      i = line_count(0);
      NEXT_TOKEN(src);
      if (*src->buf == '{')
      {
         printf("<!-- syntax violation in line %d: spurious comma -->\n", i);
         break;
      }
#endif

      if ((e = read_mcss_obj(src)) <= 0)
         return e;
   }

   if (*src->buf != '{')
      return -1;
   bs_advance(src);

   NEXT_TOKEN(src);
   memset(&css, 0, sizeof(css));

   for (;*src->buf != '}';)
   {
      if ((e = read_css(src, &tag.k, &tag.v)) <= 0)
         return e;

      mcss_obj_add_tag(&css, &tag);
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

   while (mcss_pop(&obj) >= 0)
   {
      open_osm_node(obj.type);
      for (i = 0; i < obj.tag_cnt; i++)
         print_osm_tag(&obj.tag[i]);
      print_zoom(obj.zs, obj.ze);
      print_action(css.tag, css.tag_cnt);
      close_osm_node(obj.type);
      free(obj.tag);
   }
   free(css.tag);
   mcss_free();

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

   if (munmap(buf.buf, buf.len) == -1)
      perror("munmap"), exit(EXIT_FAILURE);

   return 0;
}


