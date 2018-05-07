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

/*! \file smrparse.c
 * This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
 *
 *  @author Bernhard R. Fischer
 */
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <ctype.h>
#include <inttypes.h>

#include "smrender_dev.h"


#include "colors.c"


static char *skipb(const char *s)
{
   for (; isspace((unsigned) *s); s++);
   if (*s == '\0')
      return NULL;
   return (char*) s;
}


/*! Parse a literal match condition into a struct specialTag.
 *  @param b Pointer to bstring_t with match definition. The contents of b are
 *  modified.
 *  @param t Pointer to struct specialTag.
 *  @return The function returns 0 if everything is ok. If a condition could
 *  not be properly parsed, a negative value is returned and it will be
 *  interpreted as simple string compare. Thus, it could still be used as
 *  conditon. -1 means that the regex failed to compile and -2 means that the
 *  value of a GT or LT condition could not be interpreted.
 */
static int parse_matchtype(bstring_t *b, struct specialTag *t)
{
   t->type = 0;

   if (b->len > 2)
   {
      if ((b->buf[0] == '!') && (b->buf[b->len - 1] == '!'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_INVERT;
      }
      else if ((b->buf[0] == '~') && (b->buf[b->len - 1] == '~'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_NOT;
      }
   }

   if (b->len > 2)
   {
      if ((b->buf[0] == '/') && (b->buf[b->len - 1] == '/'))
      {
         log_debug("seems to be regex: '%.*s' (%d, %c)", b->len, b->buf, b->len, b->buf[b->len - 1]);
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;

         if (regcomp(&t->re, b->buf, REG_EXTENDED | REG_NOSUB))
         {
            log_msg(LOG_ERR, "failed to compile regex '%s'", b->buf);
            return -1;
         }
         else
            t->type |= SPECIAL_REGEX;
      }
      else if ((b->buf[0] == ']') && (b->buf[b->len - 1] == '['))
      {
         log_debug("parsing GT rule");
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         errno = 0;
         t->val = strtod(b->buf, NULL);
         if (errno)
         {
            log_msg(LOG_ERR, "failed to convert value of GT rule: %s", strerror(errno));
            return -2;
         }
         else
            t->type |= SPECIAL_GT;
      }
      else if ((b->buf[0] == '[') && (b->buf[b->len - 1] == ']'))
      {
         log_debug("parsing LT rule");
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         errno = 0;
         t->val = strtod(b->buf, NULL);
         if (errno)
         {
            log_msg(LOG_ERR, "failed to convert value of LT rule: %s", strerror(errno));
            return -2;
         }
         else
            t->type |= SPECIAL_LT;
      }
   }

   return 0;
}


/*! This function parses the match tags in ot and fills the special tag
 * structure st accordingly. The bstrings in ot are modified. This function
 * actually calls parse_matchtype().
 * @param ot Pointer to a struct otag.
 * @param st Pointer to a struct stag.
 * @return On success, the function returns 0. On failure a negative value is
 * returned (see parse_matchtype()).
 */
int parse_matchtag(struct otag *ot, struct stag *st)
{
   int e;

   if ((e = parse_matchtype(&ot->k, &st->stk)) < 0)
      return e;
   if ((e = parse_matchtype(&ot->v, &st->stv)) < 0)
      return e;
   return 0;
}
 

short ppos(const char *s)
{
   char c[] = "nsmewc";
   int p[] = {POS_N, POS_S, POS_M, POS_E, POS_W, POS_C};
   int i;
   short pos = 0;

   for (i = 0; i < (int) strlen(c); i++)
      if (strchr(s, c[i]) != NULL)
         pos |= p[i];

   return pos;
}


int get_color(int n)
{
   if (n < 0 || n >= MAXCOLOR)
      return -1;
   return color_def_[n].col;
}


int set_color(const char *s, int col)
{
   int i, c = -1;

   for (i = 0; color_def_[i].name != NULL; i++)
      if (!strcasecmp(s, color_def_[i].name))
      {
         c = color_def_[i].col;
         color_def_[i].col = col;
         break;
      }
   return c;
}


int parse_color(const char *s)
{
   long c;
   int i, l;

   // safety check
   if (s == NULL)
      return -1;

   if (*s == '#')
   {
      s++;
      l = strlen(s);
      if ((l != 6) && (l != 8))
      {
         log_msg(LOG_WARN, "format error in HTML color '#%s'", s);
         return 0;
      }

      errno = 0;
      c = strtoll(s, NULL, 16);
      if (errno)
      {
         log_msg(LOG_WARN, "cannot convert HTML color '#%s': %s", s, strerror(errno));
         return 0;
      }

      return c;
   }

   for (i = 0; color_def_[i].name != NULL; i++)
      if (!strcmp(s, color_def_[i].name))
         return color_def_[i].col;

   log_msg(LOG_WARN, "unknown color %s, defaulting to black", s);
   return 0;
}


int parse_style(const char *s)
{
   if (s == NULL)
      return DRAW_SOLID;

   if (!strcmp(s, "solid")) return DRAW_SOLID;
   if (!strcmp(s, "dashed")) return DRAW_DASHED;
   if (!strcmp(s, "dotted")) return DRAW_DOTTED;
   if (!strcmp(s, "transparent")) return DRAW_TRANSPARENT;
   if (!strcmp(s, "pipe")) return DRAW_PIPE;
   if (!strcmp(s, "rounddot")) return DRAW_ROUNDDOT;

   return DRAW_SOLID;
}


int get_structor(void *lhandle, void **stor, const char *sym, const char *trail)
{
   char buf[strlen(sym) + strlen(trail) + 5];

   snprintf(buf, sizeof(buf), "act_%s%s", sym, trail);
   // Clear any existing error
   dlerror();
   *stor = dlsym(lhandle, buf);
   // Check for errors (BSD returns const char*, thus type is converted)
   if (dlerror() == NULL)
      return 0;

   log_msg(LOG_INFO, "no symbol '%s'", buf);
   return -1;
}


static smrule_t *alloc_rule(osm_obj_t *o)
{
   smrule_t *rl;

   if ((rl = malloc(sizeof(smrule_t) + sizeof(action_t) + sizeof(struct stag) * o->tag_cnt)) == NULL)
   {
      log_msg(LOG_ERR, "alloc_rule failed: %s", strerror(errno));
      return NULL; 
   }

   rl->act = (action_t*) (rl + 1);
   return rl;
}


void check_way_type(smrule_t *r)
{
   if (r->oo->type != OSM_WAY)
      return;
   if (!((osm_way_t*) r->oo)->ref_cnt)
      return;
   if (((osm_way_t*) r->oo)->ref[0] == ((osm_way_t*) r->oo)->ref[((osm_way_t*) r->oo)->ref_cnt - 1])
      r->act->way_type = ACTION_CLOSED_WAY;
   else
      r->act->way_type = ACTION_OPEN_WAY;
   log_debug("way_type = %s", r->act->way_type == ACTION_CLOSED_WAY ? "ACTION_CLOSED_WAY" : "ACTION_OPEN_WAY");
}


/*! This function parses a rule defined within the object o into a smrule_t
 * structure r. The memory is reserved by a call to alloc_rule() and must be
 * freed again with free(). If the _action_ tag was parsed properly, it is
 * removed from that object's list of tags.
 * @param o Pointer to object.
 * @param r Pointer to rule pointer. It will receive the pointer to the newly
 * allocated memory or NULL in case of error.
 * @return 0 if everything is ok. In case of a fatal error a negative value is
 * returned and *r is set to NULL. In case of a minor error a positive number
 * is returned and *r is set to a valid memory.
 */
int init_rule(osm_obj_t *o, smrule_t **r)
{
   char *s, *t, *func, buf[1024];
   smrule_t *rl;
   int e, i;

   log_debug("initializing rule %"PRId64" (0x%016"PRIx64", %"PRId64")", o->id, o->id, o->id & 0x000000ffffffffff);

   if ((*r = alloc_rule(o)) == NULL)
      return -1;

   rl = *r;
   rl->oo = o;
   rl->data = NULL;
   memset(rl->act, 0, sizeof(*rl->act));
   check_way_type(rl);

   rl->act->tag_cnt = o->tag_cnt;
   for (i = 0; i < o->tag_cnt; i++)
   {
      if (parse_matchtag(&o->otag[i], &rl->act->stag[i]) < 0)
         return 0;
#if 0
      if (parse_matchtype(&o->otag[i].k, &rl->act->stag[i].stk) < 0)
         return 0;
      if (parse_matchtype(&o->otag[i].v, &rl->act->stag[i].stv) < 0)
         return 0;
#endif
   }

   if ((i = match_attr(o, "_action_", NULL)) == -1)
   {
      // FIXME: don't understand next fixme
      // FIXME need to be added to btree
      log_msg(LOG_DEBUG, "rule %"PRId64" has no action, it may be used as template", o->id);
      return 0;
   }

   // FIXME: init_rule() should work without the following \0-termination
   o->otag[i].v.buf[o->otag[i].v.len] = '\0';
   log_msg(LOG_DEBUG, "parsing '%s'", o->otag[i].v.buf);

   if ((s = skipb(o->otag[i].v.buf)) == NULL)
   {
      log_msg(LOG_WARN, "empty _action_ value");
      return 1;
   }

   rl->act->func_name = s;
   if ((t = strpbrk(s, "@:")) != NULL)
   {
      s = t + 1;
      if (*t == '@')
      {
         if ((t = strchr(s, ':')) != NULL)
         {
            *t = '\0';
            rl->act->parm = t + 1;
         }

         // Open shared library
         if ((rl->act->libhandle = dlopen(s, RTLD_LAZY)) == NULL)
         {
            log_msg(LOG_ERR, "could not open library: %s", dlerror());
            return 1;
         }
      }
      else
      {
         *t = '\0';
         rl->act->parm = t + 1;
      }
   }

   if (rl->act->libhandle != NULL)
   {
      strncpy(buf, rl->act->func_name, sizeof(buf));
      buf[sizeof(buf) - 1] = '\0';
      if ((func = strtok(buf, "@")) == NULL)
      {
         log_msg(LOG_CRIT, "strtok() returned NULL");
         return -1;
      }
   }
   else
      func = rl->act->func_name;

   (void) get_structor(rl->act->libhandle, &rl->act->main.sym, func, "_main");
   (void) get_structor(rl->act->libhandle, &rl->act->ini.sym, func, "_ini");
   (void) get_structor(rl->act->libhandle, &rl->act->fini.sym, func, "_fini");

   if (rl->act->parm != NULL)
      rl->act->fp = parse_fparam(rl->act->parm);

   // finally call initialization function
   if (rl->act->ini.func != NULL)
   {
      log_msg(LOG_DEBUG, "calling %s_ini()", rl->act->func_name);
      e = rl->act->ini.func(rl);
      if (e < 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Exiting.", rl->act->func_name, e);
         return e;
      }
      if (e > 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Rule will be ignored.", rl->act->func_name, e);
         rl->act->main.func = NULL;
         rl->act->fini.func = NULL;
         return 0;
      }
   }

   // remove _action_ tag from tag list, i.e. move last element
   // to position of _action_ tag (order doesn't matter).
   if (i < rl->oo->tag_cnt - 1)
   {
      memmove(&rl->oo->otag[i], &rl->oo->otag[rl->oo->tag_cnt - 1], sizeof(struct otag));
      memmove(&rl->act->stag[i], &rl->act->stag[rl->oo->tag_cnt - 1], sizeof(struct stag));
   }
   rl->oo->tag_cnt--;
   rl->act->tag_cnt--;

   return 0;
}
 

int init_rules(osm_obj_t *o, void *p)
{
   bx_node_t *bn;
   smrule_t *rl;
   int e;

   if ((e = init_rule(o, &rl)) < 0)
      return e;

   if (rl == NULL)
   {
      log_msg(LOG_EMERG, "init_rule() fatally failed");
      return -1;
   }

   if ((bn = bx_get_node(p, o->id)) == NULL)
      log_msg(LOG_EMERG, "bx_get_node() returned NULL in rule_alloc()"),
         exit(EXIT_FAILURE);

   bn->next[o->type - 1] = rl;
   return 0;
}


void free_fparam(fparam_t **fp)
{
   fparam_t **fp0 = fp;

   // safety check
   if (fp == NULL)
      return;

   // free fparam_t's
   for (; *fp != NULL; fp++)
      free(*fp);

   // free pointer list
   free(fp0);
}


static void strtrunc(char *first, char *last)
{
   for (; first < last && isspace(*last); last--)
      *last = '\0';
}


/*! Parse a string. The string may either be delimited by '\'' or '"' or by any
 * character in the string delim. It returns a pointer to the tokenized string
 * and the character which actually delimited it. If the string is not enclosed
 * in one of "'\"" leading and trailing spaces are removed.
 * @param src Pointer to a string pointer. This string will be parsed and the
 * pointer thereby is increased. After the call to this function it points to
 * the first parsable character of the next token.
 * @param delim This string may contain additional delimiter characters. It may
 * be set to NULL if unused.
 * @param nextchar This pointer whill receive the character that actually
 * delimited the string.
 * @return The function returns a pointer to the tokenized string or NULL if
 * there are no more tokens.
 */
static char *parse_string(char **src, const char *delim, char *nextchar)
{
   char *sep, *s;

   // safety check.
   if (delim == NULL)
      delim = "";

   // skip leading spaces and return if string is empty
   if ((*src = skipb(*src)) == NULL)
      return NULL;

   // check if string starts with separator
   if ((sep = strchr("'\"", **src)) == NULL)
      sep = "";
   else
      (*src)++;

   for (s = *src; **src && strchr(delim, **src) == NULL && *sep != **src; (*src)++)
      // unescape characters if necessary
      if (**src == '\\')
      {
         memmove(*src, *src + 1, strlen(*src));
         switch (**src)
         {
            case 'n':
               **src = '\n';
               break;
         }
      }

   if (!**src)
   {
      strtrunc(s, *src - 1);
      *nextchar = '\0';
   }
   else if (strchr(delim, **src) != NULL)
   {
      *nextchar = **src;
      **src = 0;
      strtrunc(s, *src - 1);
      (*src)++;
   }
   else if (*sep == **src)
   {
      *nextchar = **src;
      **src = 0;
      (*src)++;
   }
   else
   {
      log_msg(LOG_EMERG, "fatal error in parse_string(), this should never happen");
      return NULL;
   }

   return s;
}


/*! This function parses a string of the format "key1=val1;key2=val2;..." into
 *  a fparam_t* list. The string within the list (keyX, valX) may be
 *  additionally delimited by '\'' or '"'. Special characters such as one of
 *  those both delimiters may be escaped with a backslash.
 *  @param parm A pointer to the original string. Please note that the string
 *  is tokenized similar to strtok_r(), thus '\0' characters are inserted. If
 *  the original string is needed for something else it should be strdup()'ed
 *  before.
 *  @return A pointer to a fparam_t* list or NULL in case of error. The
 *  fparam_t* list always contains one additional element which points to NULL.
 */
fparam_t **parse_fparam(char *parm)
{
   fparam_t **fp;
   char *s, c;
   int n;

   for (fp = NULL, n = 0; (s = parse_string(&parm, "=;", &c)) != NULL; n++)
   {
      if ((fp = realloc(fp, sizeof(*fp) * (n + 2))) == NULL)
      {
         log_msg(LOG_ERR, "realloc() failed in parse_fparam(): %s", strerror(errno));
         return NULL;
      }
      if ((fp[n] = malloc(sizeof(**fp))) == NULL)
      {
         log_msg(LOG_ERR, "malloc() failed in parse_fparam(): %s", strerror(errno));
         return NULL;
      }

      fp[n + 1] = NULL;
      fp[n]->attr = s;
      fp[n]->conv_error = 0;

      switch (c)
      {
         case ';':
            fp[n]->val = NULL;
            fp[n]->dval = 0;
            break;

         case '=':
            if ((fp[n]->val = parse_string(&parm, ";", &c)) != NULL)
            {
               char *endptr;
               errno = 0;
               fp[n]->dval = strtod(fp[n]->val, &endptr);
               if (endptr == fp[n]->val)
                  fp[n]->conv_error = EDOM;
               else
                  fp[n]->conv_error = errno;
            }
            else
               fp[n]->dval = 0;
            break;
      }
   }

   return fp;
}


int parse_alignment_str(const char *s)
{
   int pos = 0;

   if (!strcasecmp(s, "east"))
      pos |= POS_E;
   else if (!strcasecmp(s, "west"))
      pos |= POS_W;
   else if (!strcasecmp(s, "north"))
      pos |= POS_N;
   else if (!strcasecmp(s, "south"))
      pos |= POS_S;
   else if (!strcasecmp(s, "northeast"))
      pos |= POS_E | POS_N;
   else if (!strcasecmp(s, "northwest"))
      pos |= POS_W | POS_N;
   else if (!strcasecmp(s, "southeast"))
      pos |= POS_E | POS_S;
   else if (!strcasecmp(s, "southwest"))
      pos |= POS_W | POS_S;
   else
   {
      log_msg(LOG_WARN, "unknown alignment '%s'", s);
      errno = EINVAL;
   }

   return pos;
}
 

int parse_alignment(const action_t *act)
{
   int pos = 0;
   char *s;

   // 'align' has priority over 'halign'/'valign'
   if ((s = get_param("align", NULL, act)) != NULL)
      return parse_alignment_str(s);
 
   if ((s = get_param("halign", NULL, act)) != NULL)
      pos |= parse_alignment_str(s) & (POS_E | POS_W);

   if ((s = get_param("valign", NULL, act)) != NULL)
      pos |= parse_alignment_str(s) & (POS_N | POS_S);

   return pos;
}


unit_t parse_unit(const char *uptr)
{
   if (uptr == NULL)
      return U_1;
   if (*uptr == '\0' || *uptr == ':')
      return U_1;
   if (!strcasecmp(uptr, "nm") || !strcasecmp(uptr, "sm"))
      return U_NM;
   if (!strcasecmp(uptr, "kbl"))
      return U_KBL;
   if (!strcasecmp(uptr, "ft"))
      return U_FT;
   if (!strcasecmp(uptr, "mm"))
      return U_MM;
   if (!strcasecmp(uptr, "°") || !strcasecmp(uptr, "deg") || !strcasecmp(uptr, "degrees"))
      return U_DEG;
   if (!strcasecmp(uptr, "'") || !strcasecmp(uptr, "min"))
      return U_MIN;
   if (!strcasecmp(uptr, "m"))
      return U_M;
   if (!strcasecmp(uptr, "km"))
      return U_KM;
   if (!strcasecmp(uptr, "in") || !strcasecmp(uptr, "\""))
      return U_IN;
   if (!strcasecmp(uptr, "cm"))
      return U_CM;
   if (!strcasecmp(uptr, "px"))
      return U_PX;
   if (!strcasecmp(uptr, "pt"))
      return U_PT;

   log_msg(LOG_WARN, "unknown unit '%s', defaulting to '1'", uptr);
   return U_1;
}


int parse_length(const char *s, value_t *v)
{
   char *eptr;

   errno = 0;
   v->val = strtod(s, &eptr);

   if (eptr == s)
      errno = EINVAL;

   if (errno)
      return -1;

   // skip leading spaces
   for (; isspace(*eptr); eptr++);

   v->u = parse_unit(eptr);
   return 0;
}


int parse_length_def(const char *s, value_t *v, unit_t u)
{
   int e;
   if ((e = parse_length(s, v)))
      return e;

   if (v->u == U_1)
      v->u = u;
   return 0;
}


/*! Parses a string of the form "<ddd.ddd>[<unit>]:<eee.eee>[<unit>]:..." into
 * an array. If no unit exists, mm is used by default.
 * @param s Pointer to the string.
 * @param val Pointer to the array which will receive the values.
 * @param len Number of elements of the array.
 * @return Returns the number of elements found in the array or a negative
 * value according to parse_length_def() in case of error.
 */
int parse_length_mm_array(const char *s, double *val, int len)
{
   int cnt, e;
   value_t v;

   if (val == NULL)
      return 0;

   for (cnt = 0; s != NULL && len; cnt++, val++, len--)
   {
      if ((e = parse_length_def(s, &v, U_MM)) != 0)
      {
         log_debug("parse_length_def() returned %d", e);
         return e;
      }
      *val = rdata_unit(&v, U_MM);
      if ((s = strchr(s, ':')) != NULL)
         s++;
   }

   return cnt;
}

