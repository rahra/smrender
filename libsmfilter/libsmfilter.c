#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <regex.h>

#include "smrender.h"
#include "seamark.h"


#define COL_CNT 8
#define COL_ABBR_CNT COL_CNT
#define ATYPE_CNT 4
#define TAG_CNT 7
#define SMFILTER_REV "$Rev$"


enum { SEAMARK_LIGHT_CHARACTER, SEAMARK_LIGHT_OBJECT, SEAMARK_LIGHT_RADIAL,
   SEAMARK_LIGHT_SECTOR_NR, SEAMARK_ARC_STYLE, SEAMARK_LIGHT_ARC_AL,
   SEAMARK_LIGHT_ARC};

struct compass_data
{
   double var;       //!< magnetic variation
   double r1;        //!< radius in mm
   double r2;
   int ticks;        //!< number of ticks on circle
};

struct vsec_data
{
   double arc_div;         // param 'd'
   double arc_max;         // param 'a'
   double sec_radius;      // param 'r'
   double dir_arc;         // param 'b'
   double radius_f;        // radius multiplier
};

struct pchar_data
{
   regex_t regex;
   int lang;
};

enum {LANG_EN, LANG_DE, LANG_HR, LANG_GR};
#define LANG_DEFAULT LANG_EN


static char *smstrdup(const char *);
static int get_sectors(const osm_obj_t*, struct sector *sec, int nmax);
static void node_calc(const struct osm_node *nd, double r, double a, double *lat, double *lon);
static int sector_calc3(const osm_node_t*, const struct sector *, const struct vsec_data*);
static void init_sector(struct sector *sec);
static int proc_sfrac(struct sector *sec, struct vsec_data*);
#ifdef COPY_TO_HEAP
static const char *color(int);
static const char *color_abbr(int);
#endif
static void sort_sectors(struct sector *, int);
static int parse_seamark_color(bstring_t);


//static double arc_div_ = ARC_DIV;         // param 'd'
//static double arc_max_ = ARC_MAX;         // param 'a'
//static double sec_radius_ = SEC_RADIUS;   // param 'r'
//static double dir_arc_ = DIR_ARC;         // param 'b'
static int untagged_circle_ = 0;

static const double altr_[] = {0.003, 0.0035, 0.009, 0.005};

static const char *col_[] = {"white", "red", "green", "yellow", "orange", "blue", "violet", "amber", NULL};
static const char *col_abbr_hr_[] = {"B", "C", "Z", "Ž", "Or", "Pl", "Lj", "Am", NULL};
static const char *col_abbr_de_[] = {"w", "r", "gn", "g", "or", "bl", "viol", "or", NULL};
static const char *col_abbr_gr_[] = {"Λ", "Ερ", "Πρ", "Κτ", "or", "bl", "viol", "or", NULL};
static const char *col_abbr_[] = {"W", "R", "G", "Y", "Or", "Bu", "Vi", "Am", NULL};
static const char *atype_[] = {"undef", "solid", "suppress", "dashed", NULL};
static const char *tag_[] = { "seamark:light_character",
   "seamark:light:object", "seamark:light_radial", "seamark:light:sector_nr",
   "seamark:arc_style", "seamark:light_arc_al", "seamark:light_arc", NULL};

#ifdef COPY_TO_HEAP
static char *col_heap_[COL_CNT];
static char *col_abbr_heap_[COL_ABBR_CNT];
static char *atype_heap_[ATYPE_CNT];
static char *tag_heap_[TAG_CNT];
#else
#define col_heap_ (char*)col_
#define col_abbr_heap_ (char*)col_abbr_
#define atype_heap_ (char*)atype_
#define tag_heap_ (char*)tag_
#endif


/*! Library constructor. String constants which are returned to load program
 * are copied to heap memory.
 */
void __attribute__ ((constructor)) init_libsmfilter(void)
{
#ifdef COPY_TO_HEAP
   int i;

   for (i = 0; i <= COL_CNT; i++)
   {
      col_heap_[i] = smstrdup(col_[i]);
      col_abbr_heap_[i] = smstrdup(col_abbr_[i]);
   }
   for (i = 0; i <= ATYPE_CNT; i++)
   {
      atype_heap_[i] = smstrdup(atype_[i]);
   }
   for (i = 0; i <= TAG_CNT; i++)
   {
      tag_heap_[i] = smstrdup(tag_[i]);
   }
#endif

   log_msg(LOG_INFO, "libsmfilter %s initialized", SMFILTER_REV);
}


void __attribute__ ((destructor)) fini_libsmfilter(void)
{
#ifdef COPY_TO_HEAP
   int i;

   for (i = 0; i < COL_CNT; i++)
   {
      free(col_heap_[i]);
      free(col_abbr_heap_[i]);
   }
   for (i = 0; i < ATYPE_CNT; i++)
   {
      free(atype_heap_[i]);
   }
   for (i = 0; i < TAG_CNT; i++)
   {
      free(tag_heap_[i]);
   }
#endif

   log_msg(LOG_INFO, "libsmfilter unloading");
}


int act_pchar_ini(smrule_t *r)
{
   struct pchar_data *pd;
   regex_t regex;
   char *s;
   int e;

   if ((e = regcomp(&regex, "seamark:light:([0-9]+:)?colour", REG_EXTENDED | REG_NOSUB)))
   {
      log_msg(LOG_ERR, "regcomp failed: %d", e);
      return -1;
   }

   if ((pd = malloc(sizeof(*pd))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memcpy(&pd->regex, &regex, sizeof(regex));
   pd->lang = LANG_DEFAULT;

   if ((s = get_param("lang", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(s, "hr"))
         pd->lang = LANG_HR;
      else if (!strcasecmp(s, "de"))
         pd->lang = LANG_DE;
      else if (!strcasecmp(s, "gr"))
         pd->lang = LANG_GR;
   }

   r->data = pd;
   return 0;
}


int act_pchar_fini(smrule_t *r)
{
   if (r->data == NULL)
      return 0;

   regfree(&((struct pchar_data*) r->data)->regex);
   free(r->data);
   r->data = NULL;
   return 0;
}



char *bs_dup(const bstring_t *b)
{
   char *s;

   if ((s = malloc(b->len + 1)) == NULL)
      return NULL;
   memcpy(s, b->buf, b->len);
   s[b->len] = '\0';
   return s;
}


/*! This function creates the new tag 'seamark:light_character' which is a
 * combined tag of several light attributes.
 * The function is intended to be called by a rule action.
 */
int act_pchar_main(smrule_t *r, osm_obj_t *o)
{
   struct pchar_data *pd = r->data;
   char lchar[8] = "", group[8] = "", period[8] = "", range[8] = "", col[32] = "", buf[256];
   int col_mask[COL_CNT];
   struct otag *ot;
   char *s;
   int i, n;

   if ((n = match_attr(o, "seamark:light:group", NULL)) != -1 || 
         (n = match_attr(o, "seamark:light:1:group", NULL)) != -1)
      snprintf(group, sizeof(group), "(%.*s)", o->otag[n].v.len, o->otag[n].v.buf);
   if ((n = match_attr(o, "seamark:light:period", NULL)) != -1 ||
         (n = match_attr(o, "seamark:light:1:period", NULL)) != -1)
   {
      if (((struct pchar_data*) r->data)->lang == LANG_GR)
         snprintf(period, sizeof(period), " %.*sδ", o->otag[n].v.len, o->otag[n].v.buf);
      else
         snprintf(period, sizeof(period), " %.*ss", o->otag[n].v.len, o->otag[n].v.buf);
   }
   if ((n = match_attr(o, "seamark:light:range", NULL)) != -1 || 
         (n = match_attr(o, "seamark:light:1:range", NULL)) != -1)
      snprintf(range, sizeof(range), " %.*sM", o->otag[n].v.len, o->otag[n].v.buf);
   if ((n = match_attr(o, "seamark:light:character", NULL)) != -1 ||
         (n = match_attr(o, "seamark:light:1:character", NULL)) != -1)
   {
      switch (((struct pchar_data*) r->data)->lang)
      {
         case LANG_GR:
            snprintf(lchar, sizeof(lchar), "%.*s ", o->otag[n].v.len, o->otag[n].v.buf);
            break;
         case LANG_HR:
            snprintf(lchar, sizeof(lchar), "%.*s", o->otag[n].v.len, o->otag[n].v.buf);
            break;
         default:
            snprintf(lchar, sizeof(lchar), "%.*s%s", o->otag[n].v.len, o->otag[n].v.buf, group[0] == '\0' ? "." : "");
      }
   }

   memset(&col_mask, 0, sizeof(col_mask));
   for (i = 0; i < o->tag_cnt; i++)
   {
      s = bs_dup(&o->otag[i].k);
      if (!regexec(&pd->regex, s, 0, NULL, 0))
      {
         if ((n = parse_seamark_color(o->otag[i].v)) != -1)
            col_mask[n]++;
      }
      free(s);
   }

   for (i = 0; i < COL_CNT; i++)
      if (col_mask[i])
      {
         switch (((struct pchar_data*) r->data)->lang)
         {
            case LANG_GR:
               snprintf(buf, sizeof(buf), "%s ", col_abbr_gr_[i]);
               break;
            case LANG_HR:
               snprintf(buf, sizeof(buf), "%s ", col_abbr_hr_[i]);
               break;
            case LANG_DE:
               snprintf(buf, sizeof(buf), "%s/", col_abbr_de_[i]);
               break;
            default:
               snprintf(buf, sizeof(buf), "%s", col_abbr_[i]);
         }
         //FIXME: strcat
         strcat(col, buf);
      }

   // remove trailing '/'
   if (((struct pchar_data*) r->data)->lang == LANG_DE && strlen(col))
      col[strlen(col) - 1] = '\0';

   switch (((struct pchar_data*) r->data)->lang)
   {
      case LANG_HR:
         if (!snprintf(buf, sizeof(buf), "%s%s%s%s%s", col, lchar, group, period, range))
            return 0;
         break;
      case LANG_GR:
         if (!snprintf(buf, sizeof(buf), "%s %s%s%s %s", lchar, group, col, period, range))
            return 0;
         break;
      default:
         if (!snprintf(buf, sizeof(buf), "%s%s%s.%s%s", lchar, group, col, period, range))
            return 0;
   }

   if ((ot = realloc(o->otag, sizeof(struct otag) * (o->tag_cnt + 1))) == NULL)
   {
      log_msg(LOG_ERR, "could not realloc new node: %s", strerror(errno));
      return -1;
   }

   o->otag = ot;

   // clear additional otag structure
   memset(&o->otag[o->tag_cnt], 0, sizeof(struct otag));
   // add key and value to new tag structure
   o->otag[o->tag_cnt].v.buf = smstrdup(buf);
   o->otag[o->tag_cnt].v.len = strlen(buf);
   o->otag[o->tag_cnt].k.buf = tag_heap_[SEAMARK_LIGHT_CHARACTER];
   o->otag[o->tag_cnt].k.len = strlen(tag_heap_[SEAMARK_LIGHT_CHARACTER]);
   o->tag_cnt++;

   return 0;
}


int act_vsector_ini(smrule_t *r)
{
   struct vsec_data *vd;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_WARN, "vsector may be applied to nodes only");
      return -1;
   }

   if ((vd = malloc(sizeof(*vd))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc vsec_data: %s", strerror(errno));
      return -1;
   }

   // init defaults
   vd->arc_max = ARC_MAX;
   vd->dir_arc = DIR_ARC;
   vd->arc_div = ARC_DIV;
   vd->sec_radius = SEC_RADIUS;
   vd->radius_f = 1;

   get_param("a", &vd->arc_max, r->act);
   get_param("b", &vd->dir_arc, r->act);
   get_param("d", &vd->arc_div, r->act);
   get_param("r", &vd->sec_radius, r->act);
   get_param("f", &vd->radius_f, r->act);

   r->data = vd;

   log_msg(LOG_INFO, "arc_max(a) = %.2f, dir_arc(b) = %.2f, arc_div(d) = %.2f, sec_radius(r) = %.2f, radius_f(f) = %.2f",
         vd->arc_max, vd->dir_arc, vd->arc_div, vd->sec_radius, vd->radius_f);
   return 0;
}


int act_vsector_fini(smrule_t *r)
{
   if (r->data != NULL)
   {
      free(r->data);
      r->data = NULL;
   }
   return 0;
}


/*! This function generates virtual nodes and ways for sectored lights.
 * The function was originally written for smfilter and was ported to this
 * library for smrender. The code was changed as less as possible.
 * The function is intended to be called by a rule action.
 */
int act_vsector_main(smrule_t *r, osm_obj_t *o)
{
   struct vsec_data *vd = r->data;
   int i, j, n, k;
   struct sector sec[MAX_SEC];

   for (i = 0; i < MAX_SEC; i++)
      init_sector(&sec[i]);

   if (!(i = get_sectors(o, sec, MAX_SEC)))
      return 0;

   for (i = 0, n = 0; i < MAX_SEC; i++)
   {
      // check all parsed sectors for its validity and remove
      // illegal sectors
      if (sec[i].used)
      {
         // Skip 0 degree sector if it is a directional
         // light. Such definitions are incorrect and have
         // been accidently imported with the LoL import.
         if (i && (sec[i].start == sec[i].end) && (sec[i].start == sec[0].dir))
         {
            log_msg(LOG_INFO, "deprecated feature: %d:sector_start == %d:sector_end == orientation (node %ld)", sec[i].nr, sec[i].nr, o->id);
            sec[i].used = 0;
            continue;
         }

         if ((!isnan(sec[i].dir) && (sec[i].cat != CAT_DIR)) ||
              (isnan(sec[i].dir) && (sec[i].cat == CAT_DIR)))
         {
            log_msg(LOG_WARNING, "sector %d has incomplete definition of directional light (node %ld)", sec[i].nr, o->id);
            sec[i].dir = NAN;
            sec[i].cat = 0;
            sec[i].used = 0;
            continue;
         }
         if (isnan(sec[i].start) && isnan(sec[i].end))
         {
            if (sec[i].cat == CAT_DIR)
            {
               sec[i].start = sec[i].end = sec[i].dir;
            }
            else if (untagged_circle_)
            {
               sec[i].start = 0.0;
               sec[i].end = 360.0;
            }
            else
            {
               log_msg(LOG_WARNING, "sector %d of node %ld seems to lack start/end angle", sec[i].nr, o->id);
               sec[i].used = 0;
               continue;
            }
         }
         else if (isnan(sec[i].start) || isnan(sec[i].end))
         {
            log_msg(LOG_WARNING, "sector %d of node %ld has either no start or no end angle!", sec[i].nr, o->id);
            sec[i].used = 0;
            continue;
         }

         if (sec[i].start > sec[i].end)
            sec[i].end += 360;

         // increase counter for valid sectors
         n++;
      } // if (sec[i].used)
   } // for (i = 0; i < MAX_SEC; i++)

   // remove all unused (or invalid) sectors
   //log_msg(LOG_DEBUG, "removing unused sectors");
   for (i = 0, j = 0; i < MAX_SEC && j < n; i++, j++)
   {
      if (sec[i].used)
      {
         sec[i].mean = (sec[i].start + sec[i].end) / 2;
         continue;
      }
      memmove(&sec[i], &sec[i + 1], sizeof(struct sector) * (MAX_SEC - i - 1));
      init_sector(&sec[MAX_SEC - 1]);
      i--;
      j--;
   }

   // sort sectors ascending on der mean angle
   sort_sectors(&sec[0], n);

   sec[n - 1].espace = sec[0].sspace = sec[0].start - sec[n - 1].end;
   for (i = 0; i < n - 1; i++)
      sec[i].espace = sec[i + 1].sspace = sec[i + 1].start - sec[i].end;

   // render sectors
   for (i = 0; i < n; i++)
   {
      if (sec[i].used)
      {
         if (proc_sfrac(&sec[i], vd) == -1)
         {
            log_msg(LOG_WARNING, "negative angle definition is just allowed in last segment! (sector %d node %ld)", sec[i].nr, o->id);
            continue;
         }
         //printf("   <!-- [%d]: start = %.2f, end = %.2f, col = %d, r = %.2f, nr = %d -->\n",
         //   i, sec[i].start, sec[i].end, sec[i].col, sec[i].r, sec[i].nr);
         if (sector_calc3((osm_node_t*) o, &sec[i], vd))
            log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));

         if (sec[i].col[1] != -1)
         {
            sec[i].sf[0].startr = sec[i].sf[sec[i].fused - 1].endr = 0;
            for (j = 0; j < 4; j++)
            {
               for (k = 0; k < sec[i].fused; k++)
                  sec[i].sf[k].r -= altr_[j];
               sec[i].al++;
               if (sector_calc3((osm_node_t*) o, &sec[i], vd))
                  log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));
            }
         }
      }
   } // for (i = 0; n && i < MAX_SEC; i++)

   return 0;
}


static int parse_seamark_color(bstring_t b)
{
   int i;

   for (i = 0; col_[i] != NULL; i++)
      if (!bs_cmp(b, col_[i]))
         return i;
   return -1;
}


static char *smstrdup(const char *s)
{
   char *dup;

   if (s == NULL)
      return NULL;

   if ((dup = strdup(s)) == NULL)
   {
      log_msg(LOG_EMERG, "cannot smstrdup(): %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   return dup;
}


static void sort_sectors(struct sector *sec, int n)
{
   struct sector ss;
   int i, j;

   for (j = 1; j < n - 1; j++)
      for (i = 0; i < n - j; i++)
      {
         if (sec[i].mean > sec[i + 1].mean)
         {
            memcpy(&ss, &sec[i], sizeof(struct sector));
            memcpy(&sec[i], &sec[i + 1], sizeof(struct sector));
            memcpy(&sec[i + 1], &ss, sizeof(struct sector));
         }
      }
}

 
#ifdef COPY_TO_HEAP
static const char *color_abbr(int n)
{
   if ((n < 0) || (n >= COL_CNT))
      return NULL;
   return col_abbr_[n];
}


static const char *color(int n)
{
   if ((n < 0) || (n >= COL_CNT))
      return NULL;
   return col_[n];
}
#endif


/*! Test if bstring is numeric. It tests for the following expression:
 *  /^[-]?[0-9]*[.]?[0-0].
 *  @param b Bstring to test.
 *  @return 1 if first part of bstring is numeric, otherwise 0.
 */
static int bs_isnum(bstring_t b)
{
   int i;

   if (*b.buf == '-')
      if (!bs_advance(&b))
         return 0;

   for (i = 0; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b), i++);

   if (!b.len)
      return i > 0;
   if (*b.buf != '.')
      return i > 0;
   if (!bs_advance(&b))
      return i > 0;

   for (i = 0; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b), i++);
   
   return i > 0;
}


static int parse_arc_type(const bstring_t *b)
{
   int i;

   for (i = 0; atype_[i] != NULL; i++)
      if (!bs_ncmp(*b, atype_[i], strlen(atype_[i])))
         return i;
   return -1;
}


/*! Find next separator which is either ':' or ';'.
 *  @return It returns 0 if separator is ':' in which case the bstring is
 *  advance at the next character behind the colon. 1 is returned if then
 *  length of bstring is 0 without finding any separator or if the separator is
 *  a semicolon. In the latter case the bstring points exactly to the
 *  semicolon.
 */
static int find_sep(bstring_t *c)
{
   // find next colon
   for (; c->len && (*c->buf != ':') && (*c->buf != ';'); bs_advance(c));
   if (!c->len)
      return 1;
   if (*c->buf == ';')
      return 1;
   if (!bs_advance(c))
      return 1;

   return 0;
}


/*! get_sectors() parses the tags of an OSM node and extracts
 *  sector data into struct sector data structures.
 *  @param sec Pointer to first element of struct sector array.
 *  @param nmax maximum number of elements in array sec.
 *  @return Number of elements touched in sec array.
 */
//int get_sectors(const hpx_tree_t *t, struct sector *sec, int nmax)
static int get_sectors(const osm_obj_t *o, struct sector *sec, int nmax)
{
   int i, l;      //!< loop variables
   int n = 0;        //!< sector counter
   int k;            //!< sector number
   bstring_t b, c;   //!< temporary bstrings

   for (i = 0; i < o->tag_cnt; i++)
   {
            k = 0;
            if (!bs_cmp(o->otag[i].k, "seamark:light:orientation"))
            {
                  (sec + k)->dir = bs_tod(o->otag[i].v);
                  if (!(sec + k)->used)
                  {
                     n++;
                     (sec + k)->used = 1;
                     (sec + k)->nr = k;
                  }
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:category"))
            {
                  if (!bs_cmp(o->otag[i].v, "directional"))
                  {
                     (sec + k)->cat = CAT_DIR;
                     if (!(sec + k)->used)
                     {
                        n++;
                        (sec + k)->used = 1;
                        (sec + k)->nr = k;
                     }
                  }
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:colour"))
            {
               k = 0;
                  for (l = 0; col_[l]; l++)
                  {
                     if (!bs_cmp(o->otag[i].v, col_[l]))
                     {
                        (sec + k)->col[0] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME
                     //log_msg("unknown color: %.*s", nd->otag[i].v.len, nd->otag[i].v.buf);
                     continue;
                  }
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:character"))
            {

               (sec + k)->lc.lc = o->otag[i].v;
               continue;
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:period"))
            {
                  (sec + k)->lc.period = bs_tol(o->otag[i].v);
               continue;
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:range"))
            {
                  (sec + k)->lc.range = bs_tol(o->otag[i].v);
               continue;
            }
            else if (!bs_cmp(o->otag[i].k, "seamark:light:group"))
            {
                  (sec + k)->lc.group = bs_tol(o->otag[i].v);
               continue;
            }
            else if ((o->otag[i].k.len > 14) && !strncmp(o->otag[i].k.buf, "seamark:light:", 14))
            {
               b = o->otag[i].k;
               b.len -= 14;
               b.buf += 14;

               if (!bs_isnum(b))
                  continue;

               // get sector number of tag
               k = bs_tol(b);

               // check if it is in range
               if ((k <= 0) || (k >= nmax))
               {
                  //FIXME: logging not possible here
                  //log_msg(LOG_WARN, "sector number out of range: %d", k);
                  continue;
               }

               // find tag section behind sector number
               for (; b.len && (*b.buf >= '0') && (*b.buf <= '9'); bs_advance(&b));

               if (*b.buf == ':')
                  if (!bs_advance(&b))
                     continue;

               if (!bs_cmp(b, "sector_start"))
               {
                  (sec + k)->start = bs_tod(o->otag[i].v);
               }
               else if (!bs_cmp(b, "sector_end"))
               {
                  (sec + k)->end = bs_tod(o->otag[i].v);
               }
               else if (!bs_cmp(b, "colour"))
               {
                  c = o->otag[i].v;
                  for (l = 0; col_[l]; l++)
                  {
                     if (!bs_ncmp(c, col_[l], strlen(col_[l])))
                     {
                        (sec + k)->col[0] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME: logging not possible here
                     //log_msg("unknown color: %.*s", c.len, c.buf);
                     continue;
                  }

                  for (; c.len && (*c.buf != ';'); bs_advance(&c));
                  if (!c.len) continue;
                  if (!bs_advance(&c)) continue;

                  for (l = 0; col_[l]; l++)
                  {

                     if (!bs_ncmp(c, col_[l], strlen(col_[l])))
                     {
                        (sec + k)->col[1] = l;
                        break;
                     }
                  }
                  // continue if color was not found
                  if (col_[l] == NULL)
                  {
                     //FIXME: logging not possible here
                     //log_msg("unknown color: %.*s", c.len, c.buf);
                     continue;
                  }
               }
               else if (!bs_cmp(b, "radius"))
               {
                  c = o->otag[i].v;

                  if (!c.len)
                     continue;

                  for (; c.len; (sec + k)->fused++)
                  {
                     // if it is not the first radius set, advance bstring
                     // behind next ';'
                     if ((sec + k)->fused)
                     {
                        for (; c.len && (*c.buf != ';'); bs_advance(&c));
                        if (!c.len)
                           break;
                        if (!bs_advance(&c))
                           break;
                     }

                     // if radius definition does not start with a colon, the
                     // first entry is a radius
                     if (*c.buf != ':')
                        (sec + k)->sf[(sec + k)->fused].r = bs_tod(c);

                     // find next colon
                     if (find_sep(&c))
                        continue;

                     // test if it is numeric.
                     if (bs_isnum(c))
                     {
                        // get value of <segment>
                        (sec + k)->sf[(sec + k)->fused].a = bs_tod(c);
                        // find next colon
                        if (find_sep(&c))
                           continue;
                        // get value of <type>
                        if ((l = parse_arc_type(&c)) != -1)
                           (sec + k)->sf[(sec + k)->fused].type = l;
                        else
                        {
                           //FIXME
                           //log_msg("arc_type unknown: %.*s", c.len, c.buf);
                           (sec + k)->sf[(sec + k)->fused].type = ARC_SUPPRESS;
                        }
                     }
                     else 
                     {
                        // get value of <type>
                        if ((l = parse_arc_type(&c)) != -1)
                           (sec + k)->sf[(sec + k)->fused].type = l;
                        else
                        {
                           //FIXME
                           //log_msg("arc_type unknown: %.*s", c.len, c.buf);
                           (sec + k)->sf[(sec + k)->fused].type = ARC_SUPPRESS;
                        }
                         // find next colon
                        if (find_sep(&c))
                           continue;
                        // get value of <segment>
                        if (bs_isnum(c))
                           (sec + k)->sf[(sec + k)->fused].a = bs_tod(c);
                     }
                  }
               }
               else if (!bs_cmp(b, "orientation"))
               {
                  (sec + k)->dir = bs_tod(o->otag[i].v);
               }
               else if (!bs_cmp(b, "category"))
               {
                  if (bs_cmp(o->otag[i].v, "directional"))
                     continue;
                  (sec + k)->cat = CAT_DIR;
               }
               // continue loop if none of above matches
               else
                  continue;

               if (!(sec + k)->used)
               {
                  n++;
                  (sec + k)->used = 1;
                  (sec + k)->nr = k;
               }
            }
         }
 
   //if (n) log_msg(LOG_DEBUG, "%d sectors found", n);

   return n;
}


static void node_calc(const osm_node_t *n, double r, double a, double *lat, double *lon)
{
   *lat = r * sin(a);
   *lon = r * cos(a) / cos(DEG2RAD(n->lat));
}


static int sector_calc3(const osm_node_t *n, const struct sector *sec, const struct vsec_data *vd)
{
   double lat[3], lon[3], d, s, e, w, la, lo;
   int64_t id[5], sn;
   int i, j, k;
   char buf[256];
   //struct onode *node;
   osm_node_t *node;
   osm_way_t *wy;
   bstring_t obj;

   // get tag seamark:type of object
   if ((i = match_attr((osm_obj_t*) n, "seamark:type", NULL)) == -1)
   {
      log_msg(LOG_WARNING, "sector_calc3 was called with object (%ld) w/o tag 'seamark:type'", n->obj.id);
      return -1;
   }
   obj = n->obj.otag[i].v;
 
   for (i = 0; i < sec->fused; i++)
   {
      s = M_PI - DEG2RAD(sec->sf[i].start) + M_PI_2;
      e = M_PI - DEG2RAD(sec->sf[i].end) + M_PI_2;

      // node and radial way of sector_start
      node_calc(n, sec->sf[i].r / 60.0, s, &lat[0], &lon[0]);
      node = malloc_node(0);
      id[0] = node->obj.id = unique_node_id();
      node->lat = lat[0] + n->lat;
      node->lon = lon[0] + n->lon;
      node->obj.tim = n->obj.tim;
      node->obj.ver = 1;
      put_object((osm_obj_t*) node);

      if (sec->sf[i].startr && !(sec->sf[i].start == 0.0 && sec->sf[i].end == 360.0))
      {
         wy = malloc_way(2, 2);
         wy->obj.id = unique_way_id();
         wy->obj.tim = n->obj.tim;
         wy->obj.ver = 1;
         wy->ref[0] = n->obj.id;
         wy->ref[1] = id[0];
         wy->obj.otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         wy->obj.otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         wy->obj.otag[0].v.buf = smstrdup(buf);
         wy->obj.otag[0].v.len = strlen(buf);
         wy->obj.otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         wy->obj.otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         wy->obj.otag[1].v = obj;
         put_object((osm_obj_t*) wy);
      }

      // if radii of two segments differ and they are not suppressed then draw a radial line
      // (id[1] still contains end node of previous segment)
      if (i && (sec->sf[i].r != sec->sf[i - 1].r) && (sec->sf[i].type != ARC_SUPPRESS) && (sec->sf[i - 1].type != ARC_SUPPRESS))
      {
         wy = malloc_way(2, 2);
         wy->obj.id = unique_way_id();
         wy->obj.tim = n->obj.tim;
         wy->obj.ver = 1;
         wy->ref[0] = id[1];
         wy->ref[1] = id[0];
         wy->obj.otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         wy->obj.otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         wy->obj.otag[0].v.buf = smstrdup(buf);
         wy->obj.otag[0].v.len = strlen(buf);
         wy->obj.otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         wy->obj.otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         wy->obj.otag[1].v = obj;
         put_object((osm_obj_t*) wy);
      }
           
      // node and radial way of sector_end
      node_calc(n, sec->sf[i].r / 60.0, e, &lat[1], &lon[1]);
      node = malloc_node(0);
      id[1] = node->obj.id = unique_node_id();
      node->lat = lat[1] + n->lat;
      node->lon = lon[1] + n->lon;
      node->obj.tim = n->obj.tim;
      node->obj.ver = 1;
      put_object((osm_obj_t*) node);

      if (sec->sf[i].endr && !(sec->sf[i].start == 0.0 && sec->sf[i].end == 360.0))
      {
         wy = malloc_way(2, 2);
         wy->obj.id = unique_way_id();
         wy->obj.tim = n->obj.tim;
         wy->obj.ver = 1;
         wy->ref[0] = n->obj.id;
         wy->ref[1] = id[1];
         wy->obj.otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         wy->obj.otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         wy->obj.otag[0].v.buf = smstrdup(buf);
         wy->obj.otag[0].v.len = strlen(buf);
         wy->obj.otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         wy->obj.otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         wy->obj.otag[1].v = obj;
         put_object((osm_obj_t*) wy);
      }

      // do not generate arc if radius is explicitly set to 0 or type of arc is
      // set to 'suppress'
      if ((sec->sf[i].type == ARC_SUPPRESS) || (sec->sf[i].r == 0.0))
         continue;

      // calculate distance of nodes on arc
      if ((vd->arc_max > 0.0) && ((sec->sf[i].r / vd->arc_div) > vd->arc_max))
         d = vd->arc_max;
      else
         d = sec->sf[i].r / vd->arc_div;
      d = 2.0 * asin((d / 60.0) / (2.0 * (sec->sf[i].r / 60.0)));

      // if end angle is greater than start, wrap around 360 degrees
      if (e > s)
         e -= 2.0 * M_PI;

      // make nodes of arc
      for (w = s - d, sn = 0, j = 0; w > e; w -= d, j++)
      {
         node_calc(n, sec->sf[i].r / 60.0, w, &la, &lo);
         node = malloc_node(0);
         node->obj.id = unique_node_id();
         if (!sn) sn = node->obj.id;
         node->lat = la + n->lat;
         node->lon = lo + n->lon;
         node->obj.tim = n->obj.tim;
         node->obj.ver = 1;
         put_object((osm_obj_t*) node);
      }

      // connect nodes of arc to a way
      wy = malloc_way(4, j + 2);
      id[3] = unique_way_id();
      wy->obj.id = id[3];
      wy->obj.tim = n->obj.tim;
      wy->obj.ver = 1;
      wy->obj.otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_SECTOR_NR];
      wy->obj.otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_SECTOR_NR]);
      snprintf(buf, sizeof(buf), "%d", sec->nr);
      wy->obj.otag[0].v.buf = smstrdup(buf);
      wy->obj.otag[0].v.len = strlen(buf);
      wy->obj.otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
      wy->obj.otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
      wy->obj.otag[1].v = obj;

      wy->obj.otag[2].k.buf = tag_heap_[SEAMARK_ARC_STYLE];
      wy->obj.otag[2].k.len = strlen(tag_heap_[SEAMARK_ARC_STYLE]);
      wy->obj.otag[2].v.buf = atype_heap_[sec->sf[i].type];
      wy->obj.otag[2].v.len = strlen(atype_heap_[sec->sf[i].type]);

      if (sec->al)
      {
         snprintf(buf, sizeof(buf), "%s%d", tag_heap_[SEAMARK_LIGHT_ARC_AL], sec->al);
         wy->obj.otag[3].k.buf = smstrdup(buf);
         wy->obj.otag[3].k.len = strlen(buf);
         wy->obj.otag[3].v.buf = col_heap_[sec->col[1]];
         wy->obj.otag[3].v.len = strlen(col_heap_[sec->col[1]]);
      }
      else
      {
         wy->obj.otag[3].k.buf = tag_heap_[SEAMARK_LIGHT_ARC];
         wy->obj.otag[3].k.len = strlen(tag_heap_[SEAMARK_LIGHT_ARC]);
         wy->obj.otag[3].v.buf = col_heap_[sec->col[0]];
         wy->obj.otag[3].v.len = strlen(col_heap_[sec->col[0]]);
      }

      wy->ref[0] = id[0];
      wy->ref[wy->ref_cnt - 1] = id[1];
      for (k = 0; k < j; sn--, k++)
         wy->ref[k + 1] = sn;
      put_object((osm_obj_t*) wy);
   }

   return 0;
}


static void init_sector(struct sector *sec)
{
   int i;

   memset(sec, 0, sizeof(*sec));
   sec->start = sec->end = sec->r = sec->dir = NAN;
   sec->col[1] = -1;

   for (i = 0; i < MAX_SFRAC; i++)
      sec->sf[i].r = sec->sf[i].a = NAN;
}


/* wooly thoughts...
 * assumption:
 * sector_start = 100
 * sector_end = 200
 * radius = :10;:dashed;:solid:-10
 *    -> 100-110:solid;110-190:dashed;190-200:solid
 * radius = :-10:dashed
 *    -> 100-190:solid;190-200:dashed
 *
 *
 *
 *  @return 0 if all segments could be generated. If a negative angle was
 *  defined in another than the last segment, -1 is returned. If generation of
 *  tapering segments would exceed MAX_SFRAC (array overflow), -2 is returned.
 */
static int proc_sfrac(struct sector *sec, struct vsec_data *vd)
{
   int i, j;

   if (isnan(sec->sf[0].r))
      sec->sf[0].r = isnan(sec->r) ? vd->sec_radius : sec->r;
   if (sec->sf[0].r < 0)
      sec->sf[0].r = vd->sec_radius;
   sec->sf[0].r *= vd->radius_f;

   if (!sec->fused && isnan(sec->dir))
   {
      sec->sf[0].r = vd->sec_radius * vd->radius_f;
      sec->sf[0].start = sec->start;
      sec->sf[0].end = sec->end;
      sec->sf[0].col = sec->col[0];
      sec->sf[0].type = ARC_SOLID;
      if (sec->end - sec->start < 360)
         sec->sf[0].startr = sec->sf[0].endr = 1;
      sec->fused++;
      return 0;
   }

   // handle directional light
   if (!isnan(sec->dir))
   {
      if (sec->sspace < 0) sec->sf[0].start = sec->dir - vd->dir_arc;
      else if ((sec->sspace / 2) < vd->dir_arc) sec->sf[0].start = sec->dir - sec->sspace / 2;
      else sec->sf[0].start = sec->dir - vd->dir_arc;
      //sec->sf[0].start = sec->dir - (dir_arc_ < sec->sspace ? dir_arc_ : sec->sspace / 2);
      sec->sf[0].end = sec->dir;
      sec->sf[0].col = sec->col[0];
      sec->sf[0].type = ARC_SOLID;
      sec->sf[0].endr = 1;
      sec->sf[1].r = sec->sf[0].r;
      sec->sf[1].start = sec->dir;
      if (sec->espace < 0) sec->sf[1].end = sec->dir + vd->dir_arc;
      else if ((sec->espace / 2) < vd->dir_arc) sec->sf[1].end = sec->dir + sec->espace / 2;
      else sec->sf[1].end = sec->dir + vd->dir_arc;
      //sec->sf[1].end = sec->dir + (dir_arc_ < sec->espace ? dir_arc_ : sec->espace / 2);
      sec->sf[1].col = sec->col[0];
      sec->sf[1].type = ARC_SOLID;
      sec->fused = 2;

      return 0;
   }

   if (isnan(sec->sf[0].a))
   {
      sec->sf[0].a = sec->end - sec->start;
   }
   else if (sec->sf[0].a < 0)
   {
      // negative angle is allowed only in last segment
      if (sec->fused > 1)
         return -1;

      if (sec->sf[0].a < sec->start - sec->end)
         sec->sf[0].a = sec->start - sec->end;

      sec->sf[1].type = sec->sf[0].type;
      sec->sf[1].a = sec->sf[0].a;
      sec->sf[0].a = sec->sf[0].a + sec->end - sec->start;
      sec->sf[0].type = ARC_SOLID;

      sec->fused++;
   }

   if (sec->sf[0].a > sec->end - sec->start)
      sec->sf[0].a = sec->end - sec->start;

   sec->sf[0].start = sec->start;
   sec->sf[0].end = sec->start + sec->sf[0].a;
   sec->sf[0].col = sec->col[0];
   sec->sf[0].startr = 1;
   if (sec->sf[0].type == ARC_UNDEF)
      sec->sf[0].type = ARC_SOLID;

   for (i = 1; i < sec->fused; i++)
   {
      if (isnan(sec->sf[i].r))
         sec->sf[i].r = sec->sf[i - 1].r;
      if (sec->sf[i].type == ARC_UNDEF)
         sec->sf[i].type = sec->sf[i - 1].type;
      sec->sf[i].col = sec->sf[i - 1].col;

      if (isnan(sec->sf[i].a))
      {
         sec->sf[i].start = sec->sf[i - 1].end;
         sec->sf[i].end = sec->end;
         sec->sf[i].a = sec->sf[i].end - sec->sf[i].start;
      }
      else if (sec->sf[i].a < 0)
      {
         // negative angle is allowed only in last segment
         if (sec->fused > i + 1)
            return -1;

         if (sec->sf[i].a < sec->start - sec->end)
            sec->sf[i].a = sec->start - sec->end;

         sec->sf[i - 1].end = sec->end + sec->sf[i].a;
         sec->sf[i].start = sec->end + sec->sf[i].a;
         sec->sf[i].end = sec->end;
         sec->sf[i].a = -sec->sf[i].a;
      }
      else
      {
         if (sec->sf[i].a + sec->sf[i - 1].end > sec->end)
            sec->sf[i].a = sec->end - sec->sf[i - 1].end;

         sec->sf[i].start = sec->sf[i - 1].end;
         sec->sf[i].end = sec->sf[i].start + sec->sf[i].a;
      }
   }

   // creating tapering segments
   for (i = 0; i < sec->fused; i++)
   {
      if ((sec->sf[i].type != ARC_TAPER_UP) && (sec->sf[i].type != ARC_TAPER_DOWN))
         continue;
      // check array overflow
      if (sec->fused > MAX_SFRAC - TAPER_SEGS + 1)
         return -2;

      memmove(&sec->sf[i + TAPER_SEGS], &sec->sf[i + 1], sizeof(struct sector_frac) * (TAPER_SEGS - 1));
      sec->sf[i].a /= TAPER_SEGS;
      sec->sf[i].end = sec->sf[i].start + sec->sf[i].a;

      for (j = 1; j < TAPER_SEGS; j++)
      {
         memcpy(&sec->sf[i + j], &sec->sf[i], sizeof(struct sector_frac));
         sec->sf[i + j].start = sec->sf[i + j - 1].end;
         sec->sf[i + j].end = sec->sf[i + j].start + sec->sf[i + j].a;
         sec->sf[i + j].type = sec->sf[i].type == ARC_TAPER_UP ? ARC_TAPER_1 + j : ARC_TAPER_7 - j;
         if (sec->sf[i + j].startr)
            sec->sf[i + j].startr = 0;
      }
      sec->sf[i].type = sec->sf[i].type == ARC_TAPER_UP ? ARC_TAPER_1 : ARC_TAPER_7;
      sec->fused += TAPER_SEGS - 1;
   }

   i = sec->fused - 1;
   // extend last section to end of sector
   if (sec->sf[i].end < sec->end)
      sec->sf[i].end = sec->end;
   // add radial to last section
   sec->sf[i].endr = 1;

   return 0;
}


int act_sounding_main(smrule_t * UNUSED(rl), osm_obj_t *o)
{
   osm_node_t *n;
   osm_way_t *w;
   int i, cnt = 30;
   double r = 0.1;

   if (o->type != OSM_NODE)
      return -1;

   w = malloc_way(o->tag_cnt, cnt + 1);
   w->obj.id = unique_way_id();
   memcpy(w->obj.otag, o->otag, o->tag_cnt * sizeof(struct otag));
 
   for (i = 0; i < cnt; i++)
   {
      n = malloc_node(0);
      n->obj.id = unique_node_id();
      w->ref[i] = n->obj.id;
      node_calc((osm_node_t*) o, r / 60.0, i * 2.0 * M_PI / cnt, &n->lat, &n->lon);
      n->lat += ((osm_node_t*) o)->lat;
      n->lon += ((osm_node_t*) o)->lon;
      put_object((osm_obj_t*) n);
   }
   w->ref[i] = w->ref[0];
   put_object((osm_obj_t*) w);

   return 0;
}


int act_compass_ini(smrule_t *r)
{
   struct compass_data *cd;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_ERR, "compass() is only applicable to nodes");
      return 1;
   }

   if ((cd = calloc(1, sizeof(*cd))) == NULL)
   {
      log_msg(LOG_ERR, "calloc() failed: %s", strerror(errno));
      return -1;
   }

   if (get_parami("ticks", &cd->ticks, r->act) == NULL)
      cd->ticks = 360;

   (void) get_param("variation", &cd->var, r->act);

   if (get_param("radius", &cd->r1, r->act) == NULL)
   {
      return 1;
   }

   cd->r2 = cd->r1 * 0.9;
   cd->var = DEG2RAD(cd->var);
   r->data = cd;

   log_debug("var = %.2f, r1 = %f, ticks = %d", cd->var * 180.0 / M_PI, cd->r1, cd->ticks);
   return 0;
}


static int64_t circle_node(const osm_node_t *cn, double radius, double angle, const char *ndesc)
{
   char buf[8], *s;
   osm_node_t *n;
   int tcnt;

   tcnt = ndesc != NULL ? 3 : 2;
   n = malloc_node(tcnt);
   osm_node_default(n);
   n->lat = cn->lat + radius * sin(angle);
   n->lon = cn->lon + radius * cos(angle) / cos(DEG2RAD(n->lat));

   // add bearing
   snprintf(buf, sizeof(buf), "%.2f", RAD2DEG(M_PI_2 - angle));
   if ((s = strdup(buf)) == NULL)
   {
      log_errno(LOG_ERR, "strdup() failed");
      s = ""; //FIXME: error handling?
   }
   set_const_tag(&n->obj.otag[1], "smrender:compass", s);

   if (ndesc != NULL)
      set_const_tag(&n->obj.otag[2], "smrender:compass:description", (char*) ndesc); //FIXME: typecasting removes const
   put_object(&n->obj);
   return n->obj.id;
}


static int circle_line(const osm_node_t *cn, double angle, double r1, double r2, double phase, const char *ndesc)
{
   char buf[8], *s;

   osm_way_t *w = malloc_way(2, 2);
   osm_way_default(w);
   snprintf(buf, sizeof(buf), "%.2f", RAD2DEG(angle));
   if ((s = strdup(buf)) == NULL)
   {
      log_errno(LOG_ERR, "strdup() failed");
      return 1;
   }
   set_const_tag(&w->obj.otag[1], "smrender:compass", s);

   w->ref[0] = circle_node(cn, r1, M_PI_2 - angle, ndesc);
   w->ref[1] = circle_node(cn, r2, M_PI_2 - angle + phase, NULL);
   put_object(&w->obj);

   return 0;
}


//FIXME: function not finished yet
int act_compass_main(smrule_t *r, osm_obj_t *o)
{
   struct compass_data *cd = r->data;
   double angle_step, angle, ri, ro;
   char buf[8], *s;
   int i;

   // safety check
   if (o->type != OSM_NODE)
      return 1;

   angle_step = 2 * M_PI / cd->ticks;
   for (i = 0; i < cd->ticks; i++)
   {
      s = NULL;
      angle = angle_step * i /*+ cd->var*/;
      if (!((int) round(RAD2DEG(angle)) % 10))
      {
         ro = cd->r1 * 1.02;
         ri = cd->r2 * 0.9;
         snprintf(buf, sizeof(buf), "%03d", (int) round(RAD2DEG(angle)));
         if ((s = strdup(buf)) == NULL)
            log_errno(LOG_ERR, "strdup() failed");
      }
      else if (!((int) round(RAD2DEG(angle)) % 5))
      {
         ro = cd->r1;
         ri = cd->r2 * 0.95;
      }
      else
      {
         ro = cd->r1;
         ri = cd->r2;
      }
 
      circle_line((osm_node_t*) o, angle, ro, ri, 0, s);
   }

   // N - S axis
   circle_line((osm_node_t*) o, cd->var, cd->r1 / 0.9, cd->r1 / 0.9, M_PI, NULL);
   // E - W axis
   circle_line((osm_node_t*) o, cd->var + M_PI_2, cd->r1 / 0.9, cd->r1 / 0.9, M_PI, NULL);

   return 0;
}


int act_compass_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}

