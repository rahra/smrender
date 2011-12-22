#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

#include "smrender.h"
#include "seamark.h"


#define COL_CNT 8
#define COL_ABBR_CNT COL_CNT
#define ATYPE_CNT 4
#define TAG_CNT 7


enum { SEAMARK_LIGHT_CHARACTER, SEAMARK_LIGHT_OBJECT, SEAMARK_LIGHT_RADIAL,
   SEAMARK_LIGHT_SECTOR_NR, SEAMARK_ARC_STYLE, SEAMARK_LIGHT_ARC_AL,
   SEAMARK_LIGHT_ARC};


static char *smstrdup(const char *);
static int get_sectors(const struct onode *, struct sector *sec, int nmax);
static void node_calc(const struct osm_node *nd, double r, double a, double *lat, double *lon);
static int sector_calc3(const struct onode *, const struct sector *, bstring_t);
static void init_sector(struct sector *sec);
static int proc_sfrac(struct sector *sec);
static const char *color(int);
static const char *color_abbr(int);
static void sort_sectors(struct sector *, int);
static int parse_color(bstring_t);


static double arc_div_ = ARC_DIV;
static double arc_max_ = ARC_MAX;
static double sec_radius_ = SEC_RADIUS;
static double dir_arc_ = DIR_ARC;
static int untagged_circle_ = 0;

static const double altr_[] = {0.005, 0.005, 0.01, 0.005};

static const char *col_[] = {"white", "red", "green", "yellow", "orange", "blue", "violet", "amber", NULL};
static const char *col_abbr_[] = {"W", "R", "G", "Y", "Or", "Bu", "Vi", "Am", NULL};
static const char *atype_[] = {"undef", "solid", "suppress", "dashed", NULL};
static const char *tag_[] = { "seamark:light_character",
   "seamark:light:object", "seamark:light_radial", "seamark:light:sector_nr",
   "seamark:arc_style", "seamark:light_arc_al", "seamark:light_arc", NULL};

static char *col_heap_[COL_CNT];
static char *col_abbr_heap_[COL_ABBR_CNT];
static char *atype_heap_[ATYPE_CNT];
static char *tag_heap_[TAG_CNT];


/*! Library constructor. String constants which are returned to load program
 * are copied to heap memory.
 */
void __attribute__ ((constructor)) init_libsmfilter(void)
{
   int i;

   log_msg(LOG_INFO, "initializing libsmfilter");

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
}


void __attribute__ ((destructor)) fini_libsmfilter(void)
{
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
   log_msg(LOG_INFO, "libsmfilter unloading");
}


/*! This function creates the new tag 'seamark:light_character' which is a
 * combined tag of several light attributes.
 * The function is intended to be called by a rule action.
 */
int pchar(struct onode *nd)
{
   char lchar[8] = "", group[8] = "", period[8] = "", range[8] = "", col[8] = "", buf[256];
   int n;
   struct onode *node;

   if ((n = match_attr(nd, "seamark:light:group", NULL)) != -1)
      snprintf(group, sizeof(group), "(%.*s)", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:period", NULL)) != -1)
      snprintf(period, sizeof(period), " %.*ss", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:range", NULL)) != -1)
      snprintf(range, sizeof(range), " %.*sM", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:character", NULL)) != -1)
      snprintf(lchar, sizeof(lchar), "%.*s%s", nd->otag[n].v.len, nd->otag[n].v.buf, group[0] == '\0' ? "." : "");
   if ((n = match_attr(nd, "seamark:light:colour", NULL)) != -1)
   {
      if ((n = parse_color(nd->otag[n].v)) != -1)
         snprintf(col, sizeof(col), "%s.",  col_abbr_[n]);
   }

   if (!snprintf(buf, sizeof(buf), "%s%s%s%s%s", lchar, group, col, period, range))
      return 0;
    
   if ((node = realloc(nd, sizeof(struct onode) + sizeof(struct otag) * (nd->tag_cnt + 1) + sizeof(int64_t) * nd->ref_cnt)) == NULL)
   {
      log_msg(LOG_ERR, "could not realloc new node: %s", strerror(errno));
      return -1;
   }

   // check of realloc changed address
   if (node != nd)
   {
      nd = node;
      // if realloc changed address re-put it into tree of nodes
      (void) put_object(nd);
   }

   // clear additional otag structure
   memset(&nd->otag[nd->tag_cnt], 0, sizeof(struct otag));
   // add key and value to new tag structure
   nd->otag[nd->tag_cnt].v.buf = smstrdup(buf);
   nd->otag[nd->tag_cnt].v.len = strlen(buf);
   nd->otag[nd->tag_cnt].k.buf = tag_heap_[SEAMARK_LIGHT_CHARACTER];
   nd->otag[nd->tag_cnt].k.len = strlen(tag_heap_[SEAMARK_LIGHT_CHARACTER]);
   nd->tag_cnt++;

   return 0;
}


/*! This function generates virtual nodes and ways for sectored lights.
 * The function was originally written for smfilter and was ported to this
 * library for smrender. The code was changed as less as possible.
 * The function is intended to be called by a rule action.
 */
int vsector(struct onode *ond)
{
   int i, j, n, k;
   struct sector sec[MAX_SEC];
   struct osm_node *nd = &ond->nd;
   bstring_t b = {0, ""};

   for (i = 0; i < MAX_SEC; i++)
      init_sector(&sec[i]);

   if (!(i = get_sectors(ond, sec, MAX_SEC)))
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
            log_msg(LOG_INFO, "deprecated feature: %d:sector_start == %d:sector_end == orientation (node %ld)", sec[i].nr, sec[i].nr, nd->id);
            sec[i].used = 0;
            continue;
         }

         if ((!isnan(sec[i].dir) && (sec[i].cat != CAT_DIR)) ||
              (isnan(sec[i].dir) && (sec[i].cat == CAT_DIR)))
         {
            log_msg(LOG_WARNING, "sector %d has incomplete definition of directional light (node %ld)", sec[i].nr, nd->id);
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
               log_msg(LOG_WARNING, "sector %d of node %ld seems to lack start/end angle", sec[i].nr, nd->id);
               sec[i].used = 0;
               continue;
            }
         }
         else if (isnan(sec[i].start) || isnan(sec[i].end))
         {
            log_msg(LOG_WARNING, "sector %d of node %ld has either no start or no end angle!", sec[i].nr, nd->id);
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
      memcpy(&sec[i], &sec[i + 1], sizeof(struct sector) * (MAX_SEC - i - 1));
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
         if (proc_sfrac(&sec[i]) == -1)
         {
            log_msg(LOG_WARNING, "negative angle definition is just allowed in last segment! (sector %d node %ld)", sec[i].nr, nd->id);
            continue;
         }
         //printf("   <!-- [%d]: start = %.2f, end = %.2f, col = %d, r = %.2f, nr = %d -->\n",
         //   i, sec[i].start, sec[i].end, sec[i].col, sec[i].r, sec[i].nr);
         if (sector_calc3(ond, &sec[i], b))
            log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));

         if (sec[i].col[1] != -1)
         {
            sec[i].sf[0].startr = sec[i].sf[sec[i].fused - 1].endr = 0;
            for (j = 0; j < 4; j++)
            {
               for (k = 0; k < sec[i].fused; k++)
                  sec[i].sf[k].r -= altr_[j];
               sec[i].al++;
               if (sector_calc3(ond, &sec[i], b))
                  log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));
            }
         }
      }
   } // for (i = 0; n && i < MAX_SEC; i++)
}


static int parse_color(bstring_t b)
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


/*! get_sectors() parses the tags of an OSM nodes and extracts
 *  sector data into struct sector data structures.
 *  @param sec Pointer to first element of struct sector array.
 *  @param nmax maximum number of elements in array sec.
 *  @return Number of elements touched in sec array.
 */
//int get_sectors(const hpx_tree_t *t, struct sector *sec, int nmax)
static int get_sectors(const struct onode *nd, struct sector *sec, int nmax)
{
   int i, j, l;      //!< loop variables
   int n = 0;        //!< sector counter
   int k;            //!< sector number
   bstring_t b, c;   //!< temporary bstrings

   for (i = 0; i < nd->tag_cnt; i++)
   {
            k = 0;
            if (!bs_cmp(nd->otag[i].k, "seamark:light:orientation"))
            {
                  (sec + k)->dir = bs_tod(nd->otag[i].v);
                  if (!(sec + k)->used)
                  {
                     n++;
                     (sec + k)->used = 1;
                     (sec + k)->nr = k;
                  }
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:category"))
            {
                  if (!bs_cmp(nd->otag[i].v, "directional"))
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
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:colour"))
            {
               k = 0;
                  for (l = 0; col_[l]; l++)
                  {
                     if (!bs_cmp(nd->otag[i].v, col_[l]))
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
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:character"))
            {

               (sec + k)->lc.lc = nd->otag[i].v;
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:period"))
            {
                  (sec + k)->lc.period = bs_tol(nd->otag[i].v);
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:range"))
            {
                  (sec + k)->lc.range = bs_tol(nd->otag[i].v);
               continue;
            }
            else if (!bs_cmp(nd->otag[i].k, "seamark:light:group"))
            {
                  (sec + k)->lc.group = bs_tol(nd->otag[i].v);
               continue;
            }
            else if ((nd->otag[i].k.len > 14) && !strncmp(nd->otag[i].k.buf, "seamark:light:", 14))
            {
               b = nd->otag[i].k;
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
                  (sec + k)->start = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "sector_end"))
               {
                  (sec + k)->end = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "colour"))
               {
                  c = nd->otag[i].v;
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
                  c = nd->otag[i].v;

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
                  (sec + k)->dir = bs_tod(nd->otag[i].v);
               }
               else if (!bs_cmp(b, "category"))
               {
                  if (bs_cmp(nd->otag[i].v, "directional"))
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


static void node_calc(const struct osm_node *nd, double r, double a, double *lat, double *lon)
{
   *lat = r * sin(a);
   *lon = r * cos(a) / cos(DEG2RAD(nd->lat));
}


static int sector_calc3(const struct onode *nd, const struct sector *sec, bstring_t st)
{
   double lat[3], lon[3], d, s, e, w, la, lo;
   int64_t id[5], sn;
   int i, j, k, n;
   char buf[256];
   struct onode *node;
   bstring_t obj;

   // get tag seamark:type of object
   if ((n = match_attr(nd, "seamark:type", NULL)) == -1)
   {
      log_msg(LOG_WARNING, "sector_calc3 was called with object (%ld) w/o tag 'seamark:type'", nd->nd.id);
      return -1;
   }
   obj = nd->otag[n].v;
 
   for (i = 0; i < sec->fused; i++)
   {
      s = M_PI - DEG2RAD(sec->sf[i].start) + M_PI_2;
      e = M_PI - DEG2RAD(sec->sf[i].end) + M_PI_2;

      // node and radial way of sector_start
      node_calc(&nd->nd, sec->sf[i].r / 60.0, s, &lat[0], &lon[0]);
      if ((node = malloc_object(0, 0)) == NULL) return -1;
      id[0] = node->nd.id = unique_node_id();
      node->nd.type = OSM_NODE;
      node->nd.lat = lat[0] + nd->nd.lat;
      node->nd.lon = lon[0] + nd->nd.lon;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      put_object(node);

      if (sec->sf[i].startr)
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id();
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = nd->nd.id;
         node->ref[1] = id[0];
         node->otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         node->otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         node->otag[0].v.buf = smstrdup(buf);
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         node->otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         node->otag[1].v = obj;
         put_object(node);
      }

      // if radii of two segments differ and they are not suppressed then draw a radial line
      // (id[1] still contains end node of previous segment)
      if (i && (sec->sf[i].r != sec->sf[i - 1].r) && (sec->sf[i].type != ARC_SUPPRESS) && (sec->sf[i - 1].type != ARC_SUPPRESS))
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id();
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = id[1];
         node->ref[1] = id[0];
         node->otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         node->otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         node->otag[0].v.buf = smstrdup(buf);
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         node->otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         node->otag[1].v = obj;
         put_object(node);
      }
           
      // node and radial way of sector_end
      node_calc(&nd->nd, sec->sf[i].r / 60.0, e, &lat[1], &lon[1]);
      if ((node = malloc_object(0, 0)) == NULL) return -1;
      id[1] = node->nd.id = unique_node_id();
      node->nd.type = OSM_NODE;
      node->nd.lat = lat[1] + nd->nd.lat;
      node->nd.lon = lon[1] + nd->nd.lon;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      put_object(node);

      if (sec->sf[i].endr)
      {
         if ((node = malloc_object(2, 2)) == NULL) return -1;
         node->nd.id = unique_way_id();
         node->nd.type = OSM_WAY;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         node->ref[0] = nd->nd.id;
         node->ref[1] = id[1];
         node->otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_RADIAL];
         node->otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_RADIAL]);
         snprintf(buf, sizeof(buf), "%d", sec->nr);
         node->otag[0].v.buf = smstrdup(buf);
         node->otag[0].v.len = strlen(buf);
         node->otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
         node->otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
         node->otag[1].v = obj;
         put_object(node);
      }

      // do not generate arc if radius is explicitly set to 0 or type of arc is
      // set to 'suppress'
      if ((sec->sf[i].type == ARC_SUPPRESS) || (sec->sf[i].r == 0.0))
         continue;

      // calculate distance of nodes on arc
      if ((arc_max_ > 0.0) && ((sec->sf[i].r / arc_div_) > arc_max_))
         d = arc_max_;
      else
         d = sec->sf[i].r / arc_div_;
      d = 2.0 * asin((d / 60.0) / (2.0 * (sec->sf[i].r / 60.0)));

      // if end angle is greater than start, wrap around 360 degrees
      if (e > s)
         e -= 2.0 * M_PI;

      // make nodes of arc
      for (w = s - d, sn = 0, j = 0; w > e; w -= d, j++)
      {
         node_calc(&nd->nd, sec->sf[i].r / 60.0, w, &la, &lo);
         if ((node = malloc_object(0, 0)) == NULL) return -1;
         node->nd.id = unique_node_id();
         if (!sn) sn = node->nd.id;
         node->nd.type = OSM_NODE;
         node->nd.lat = la + nd->nd.lat;
         node->nd.lon = lo + nd->nd.lon;
         node->nd.tim = nd->nd.tim;
         node->nd.ver = 1;
         put_object(node);
      }

      // connect nodes of arc to a way
      if ((node = malloc_object(4, j + 2)) == NULL) return -1;
      id[3] = unique_way_id();
      node->nd.id = id[3];
      node->nd.type = OSM_WAY;
      node->nd.tim = nd->nd.tim;
      node->nd.ver = 1;
      node->otag[0].k.buf = tag_heap_[SEAMARK_LIGHT_SECTOR_NR];
      node->otag[0].k.len = strlen(tag_heap_[SEAMARK_LIGHT_SECTOR_NR]);
      snprintf(buf, sizeof(buf), "%d", sec->nr);
      node->otag[0].v.buf = smstrdup(buf);
      node->otag[0].v.len = strlen(buf);
      node->otag[1].k.buf = tag_heap_[SEAMARK_LIGHT_OBJECT];
      node->otag[1].k.len = strlen(tag_heap_[SEAMARK_LIGHT_OBJECT]);
      node->otag[1].v = obj;

      node->otag[2].k.buf = tag_heap_[SEAMARK_ARC_STYLE];
      node->otag[2].k.len = strlen(tag_heap_[SEAMARK_ARC_STYLE]);
      node->otag[2].v.buf = atype_heap_[sec->sf[i].type];
      node->otag[2].v.len = strlen(atype_heap_[sec->sf[i].type]);

      if (sec->al)
      {
         snprintf(buf, sizeof(buf), "%s%d", tag_heap_[SEAMARK_LIGHT_ARC_AL], sec->al);
         node->otag[3].k.buf = smstrdup(buf);
         node->otag[3].k.len = strlen(buf);
         node->otag[3].v.buf = col_heap_[sec->col[1]];
         node->otag[3].v.len = strlen(col_heap_[sec->col[1]]);
      }
      else
      {
         node->otag[3].k.buf = tag_heap_[SEAMARK_LIGHT_ARC];
         node->otag[3].k.len = strlen(tag_heap_[SEAMARK_LIGHT_ARC]);
         node->otag[3].v.buf = col_heap_[sec->col[0]];
         node->otag[3].v.len = strlen(col_heap_[sec->col[0]]);
      }

      node->ref[0] = id[0];
      node->ref[node->ref_cnt - 1] = id[1];
      for (k = 0; k < j; sn--, k++)
         node->ref[k + 1] = sn;
      put_object(node);
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
static int proc_sfrac(struct sector *sec)
{
   int i, j;

   if (isnan(sec->sf[0].r))
      sec->sf[0].r = isnan(sec->r) ? sec_radius_ : sec->r;
   if (sec->sf[0].r < 0)
      sec->sf[0].r = sec_radius_;

   if (!sec->fused && isnan(sec->dir))
   {
      sec->sf[0].r = sec_radius_;
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
      if (sec->sspace < 0) sec->sf[0].start = sec->dir - dir_arc_;
      else if ((sec->sspace / 2) < dir_arc_) sec->sf[0].start = sec->dir - sec->sspace / 2;
      else sec->sf[0].start = sec->dir - dir_arc_;
      //sec->sf[0].start = sec->dir - (dir_arc_ < sec->sspace ? dir_arc_ : sec->sspace / 2);
      sec->sf[0].end = sec->dir;
      sec->sf[0].col = sec->col[0];
      sec->sf[0].type = ARC_SOLID;
      sec->sf[0].endr = 1;
      sec->sf[1].r = sec->sf[0].r;
      sec->sf[1].start = sec->dir;
      if (sec->espace < 0) sec->sf[1].end = sec->dir + dir_arc_;
      else if ((sec->espace / 2) < dir_arc_) sec->sf[1].end = sec->dir + sec->espace / 2;
      else sec->sf[1].end = sec->dir + dir_arc_;
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

