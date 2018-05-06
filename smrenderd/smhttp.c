/* Copyright 2014 Bernhard R. Fischer.
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <time.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <arpa/inet.h>

#include "smrender.h"
#include "smhttp.h"
#include "rdata.h"
#include "smcache.h"
#include "libhpxml.h"
#include "websocket.h"
#include "smloadosm.h"
#include "smcore.h"
#include "smdfunc.h"


extern bx_node_t *index_;


/* the following prototypes are found in smrender_dev.h which is unsuitable to
 * be included here. */
int print_onode(FILE*, const osm_obj_t*);
size_t save_osm0(FILE *, bx_node_t *, const struct bbox *, const char *);
int init_rule(osm_obj_t *, smrule_t **);


struct smhttpd
{
   int fd;
   int max_conns;
   http_thread_t *htth;
};


#ifdef OSM_BIN
/* binary encoding of tags:
 * always: key + value
 * 1.byte: if <= 127: length of bytes following
 * if >= 128: 1.byte & 0x7f << 8 + 2.byte -> max length: 32767
 * if key + value length == 0 -> end of list
 *
 * if node, it is followd by int32_t lat, int32_lon;
 * if way, it is followed by int32_t ref_cnt and then int64_t ref[]
 */
typedef struct sd_osm_obj
{
   int8_t type, vis;
   int64_t id;
   int32_t ver, cs, uid;
   int64_t tim;
//   int16_t tag_cnt;
} __attribute__((packed)) sd_osm_obj_t;
#endif


/*! create httpd acces log to stdout
 *  @param saddr Pointer to sockaddr_in structure containing
 *               the address of the remote end.
 *  @param req Request line.
 *  @param stat Response status.
 *  @param siz Number of bytes (of HTTP body) returned.
 */
void log_access(const struct sockaddr_in * UNUSED(saddr), const char *req, int stat, int siz)
{
#ifdef ACCESS_LOG
   char addr[100], tms[100];
   time_t t;
   struct tm tm;

   if (inet_ntop(AF_INET, &saddr->sin_addr.s_addr, addr, 100) == NULL)
      perror("inet_ntop"), exit(EXIT_FAILURE);
   t = time(NULL);
   (void) localtime_r(&t, &tm);
   (void) strftime(tms, 100, "%d/%b/%Y:%H:%M:%S %z", &tm);
   printf("%s - - [%s] \"%s\" %d %d \"-\" \"-\"\n", addr, tms, req, stat, siz);
#endif
   log_msg(LOG_INFO, "\"%s\" %d %d", req, stat, siz);
}


/*! Read '\n'-delimited line from file descriptor into buffer buf but not more
 * than size bytes. The buffer *  will NOT be \0-terminated.
 * @param fd Valid file descriptor.
 * @param buf Pointer to buffer.
 * @param size Size of buffer.
 * @return Returns the number of bytes read. In case of EOF, 0 is returned, in
 * case of error -1 is returned and errno is set appropriately (according to
 * read(2)). Please note that an error may occur after a set of bytes have
 * already been read. In this case the number of bytes that have been read
 * before the error occured are returned but errno is set. To detect this,
 * errno should be set to 0 before the call to read_line(). If it is not
 * handled in that way, read_line() should fail with -1 upon the next call.
 */
size_t read_line(int fd, char *buf, int size)
{
   int ret, len;

   for (len = 0; size > 0; size--, buf++, len++)
   {
      ret = read(fd, buf, 1);

      // check if read() failed
      if (ret == -1)
      {
         // check if data was already read before
         if (len)
            return len;
         return -1;
      }
      // check for EOF
      else if (!ret)
         return len;

      // check for EOL
      if (*buf =='\n')
         return len + 1;
   }
   return len;
}


int set_nonblock(int fd)
{
   long flags;
   int err = 0;

   log_debug("setting fd %d to O_NONBLOCK", fd);
   if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
   {
      log_msg(LOG_WARN, "could not get socket flags for %d: \"%s\"", fd, strerror(errno));
      flags = 0;
   }
   if ((fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1)
   {
      log_msg(LOG_ERR, "could not set O_NONBLOCK for %d: \"%s\"", fd, strerror(errno));
      err = -1;
   }

   return err;
}


static int http_flush_input_headers(int fd)
{
   int buf[2048];
   int s, len;

   set_nonblock(fd);
   for (len = 0; ; len += s)
   {
      if ((s = read(fd, buf, sizeof(buf))) == -1)
         return -1;
      if (s < (int) sizeof(buf))
         break;
   }
   return len;
}


/*! Close file descriptor and exit on error.
 *  @param fd File descriptor to be closed.
 */
void eclose(int fd)
{
   if (close(fd) == -1)
      perror("close"), exit(EXIT_FAILURE);
}


static int http_header(FILE *f, time_t t)
{
   struct tm tm;
   char buf[256];
   int len = 0;

   if (!t)
      t = time(NULL);
   localtime_r(&t, &tm);
   strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z", &tm);
   len += fprintf(f, "%sServer: Smrenderd\r\nDate: %s\r\nContent-Type: text/xml; charset=utf-8\r\n\r\n", STATUS_200, buf);
   return len;
}


static int s2d(const char *s, double *d)
{
   errno = 0;
   *d = strtod(s, NULL);
   if (errno)
   {
      int e = errno;
      log_msg(LOG_ERR, "strtod('%s') failed: %s", s, strerror(e));
      return -e;
   }
   return 0;
}


static int parse_bbi(const char *u, struct bboxi *bbi)
{
   char buf[256];
   char *uri = buf;
   double dc;
   char *s, *p;
   int i;

   // safety check
   if (u == NULL)
      return -500;

   if (strlen(u) >= sizeof(buf))
      return -500;

   strcpy(uri, u);

   if (strncmp(uri, "bbox=", 5))
      return -404;
   uri += 5;

   if ((s = strtok_r(uri, ",", &p)) == NULL)
      return -404;

   for (i = 0; i < 4 && s != NULL; i++, s = strtok_r(NULL, ",", &p))
   {
      if (s2d(s, &dc))
         return -404;
      bbi->coord[i] = dc * 1000.0;
   }
   if (i < 4)
   {
      log_msg(LOG_ERR, "not enough parameters");
      return -404;
   }
   bbi->coord[2]++;
   bbi->coord[3]++;

   log_debug("map query: %f,%f,%f,%f",
         bbi->coord[0] / 1000.0, bbi->coord[1] / 1000.0,
         bbi->coord[2] / 1000.0, bbi->coord[3] / 1000.0);
   return 0;
}


static void bbi2bb(const struct bboxi *bbi, struct bbox *bb)
{
   bb->ll.lon = bbi->coord[0] / 1000.0;
   bb->ll.lat = bbi->coord[1] / 1000.0;
   bb->ru.lon = bbi->coord[2] / 1000.0;
   bb->ru.lat = bbi->coord[3] / 1000.0;
}


static qcache_t *qc_get_bbi(const struct bboxi *bbi)
{
   bx_node_t *tree;
   struct bbox bb;
   qcache_t *qc;

   bbi2bb(bbi, &bb);

   if ((qc = qc_lookup(bbi)) == NULL)
   {
      log_debug("no cache entry, creating query");
      if ((tree = get_obj_bb(index_, &bb)) == NULL)
      {
         log_msg(LOG_ERR, "query failed");
         return NULL;
      }

      log_debug("adding query to cache");
      while ((qc = qc_put(bbi, tree)) == NULL)
      {
         log_msg(LOG_INFO, "cache full, cleaning up");
         qc_cleanup();
      }
   }

   return qc;
}


size_t http_map_bbox(int fd, const char *u)
{
   struct bboxi bbi;
   struct bbox bb;
   int i;
   FILE *f;
   size_t len;
   qcache_t *qc;

   if ((i = parse_bbi(u, &bbi)) < 0)
      return i;

   if ((qc = qc_get_bbi(&bbi)) == NULL)
      return -500;

   if ((f = fdopen(fd, "w")) == NULL)
   {
      log_msg(LOG_ERR, "failed to fdopen(%d): %s", fd, strerror(errno));
      return -500;
   }

   http_header(f, 0);
   bbi2bb(&bbi, &bb);
   len = save_osm0(f, qc->tree, &bb, NULL);
   fclose(f);

   qc_release(qc);

   return len;
}


int http_proc_api06(int fd, const char *uri)
{
   int len, type;
   osm_obj_t *o;
   int64_t id;
   FILE *f;

   log_debug("checking type: '%s'", uri);
   if (!strncmp("node/", uri, 5))
   {
      uri += 5;
      type = OSM_NODE;
   }
   else if (!strncmp("way/", uri, 4))
   {
      uri += 4;
      type = OSM_WAY;
   }
   else if (!strncmp("relation/", uri, 9))
   {
      uri += 9;
      type = OSM_REL;
   }
   else
   {
      log_msg(LOG_WARN, "ill object type");
      return -404;
   }

   errno = 0;
   id = strtoll(uri, NULL, 0);
   if (errno)
   {
      log_msg(LOG_WARN, "ill object id");
      return -404;
   }

   if ((o = get_object(type, id)) == NULL)
   {
      log_debug("object %"PRId64" of type %d does not exist", id, type);
      return -404;
   }

   if ((f = fdopen(fd, "w")) == NULL)
   {
      log_msg(LOG_ERR, "failed to fdopen(%d): %s", fd, strerror(errno));
      return -500;
   }

   len = 0;
   http_header(f, o->tim);
   len += fprintf(f, "<osm>\n");
   len += print_onode(f, o);
   len += fprintf(f, "</osm>\n");

   fclose(f);

   return len;
}


int http_changesets(int fd, const char * UNUSED(uri))
{
   FILE *f;
   int len;

   if ((f = fdopen(fd, "w")) == NULL)
   {
      log_msg(LOG_ERR, "failed to fdopen(%d): %s", fd, strerror(errno));
      return -500;
   }

   len = 0;
   http_header(f, 0);
   len += fprintf(f, "<osm>\n");
   //len += print_onode(f, o);
   len += fprintf(f, "</osm>\n");

   fclose(f);

   return len;
}


int http_capabilities(int fd, const char * UNUSED(uri))
{
   FILE *f;
   int len;

   if ((f = fdopen(fd, "w")) == NULL)
   {
      log_msg(LOG_ERR, "failed to fdopen(%d): %s", fd, strerror(errno));
      return -500;
   }

   len = 0;
   http_header(f, 0);
   len += fprintf(f,
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<osm version=\"0.6\" generator=\"Smrenderd\">\n"
         "<api>\n"
         "<version minimum=\"0.6\" maximum=\"0.6\"/>\n"
//         "<area maximum=\"0.25\"/>\n"
//         "<tracepoints per_page=\"5000\"/>\n"
//         "<waynodes maximum=\"2000\"/>\n"
//         "<changesets maximum_elements=\"50000\"/>\n"
//         "<timeout seconds=\"300\"/>\n"
         "<status database=\"online\" api=\"readonly\" gpx=\"offline\"/>\n"
         "</api>\n"
         "</osm>\n");

   fclose(f);

   return len;
}


int http_proc_get(int fd, const char *uri)
{
   log_debug("processing request '%s'", uri);
   if (!strncmp(API06_URI, uri, strlen(API06_URI)))
   {
      uri += strlen(API06_URI);
      log_debug("checking uri '%s'", uri);
      if (!strncmp(uri, "map?", 4))
         return http_map_bbox(fd, uri + 4);
      else if (!strncmp(uri, "changesets", 10))
         return http_changesets(fd, uri + 10);
      else if (!strncmp(uri, "capabilities", 12))
         return http_capabilities(fd, uri + 12);
      else
         return http_proc_api06(fd, uri);
   }
   else if (!strncmp("/api/", uri, 5))
   {
      uri += 5;
      if (!strncmp(uri, "capabilities", 12))
         return http_capabilities(fd, uri + 12);
      else
         return -404;
   }

   return -404;
}


#ifdef OSM_BIN
int bs_compress(const bstring_t *b, char *buf, int size)
{
   // boundary and safety checks
   if (b->len <= 127 && size < b->len + 1)
      return -1;
   if (b->len > 32767)
      return -2;
   if (size < b->len + 2)
      return -1;

   if (b->len <= 127)
   {
      *buf = b->len;
      memcpy(buf + 1, b->buf, b->len);
      return b->len + 1;
   }

   buf[0] = 0x80 | (b->len >> 8);
   buf[1] = b->len;
   memcpy(buf + 2, b->buf, b->len);
   return b->len + 2;
}


int osm_to_bin(osm_obj_t *o, char *buf, int size)
{
   sd_osm_obj_t *so;
   char *tagbuf;
   int32_t *int32;
   int64_t *int64;
   int len;

   so = (sd_osm_obj_t*) buf;
   tagbuf = (char*) (so + 1);

   so->type = o->type;
   so->vis = o->vis;
   so->id = o->id;
   so->ver = o->ver;
   so->cs = o->cs;
   so->uid = o->uid;
   so->tim = o->tim;

   size -= sizeof(*so);

   for (int i = 0; i < o->tag_cnt; i++)
   {
      if ((len = bs_compress(&o->otag[i].k, tagbuf, size)) < 0)
         return -1;
      tagbuf += len;
      if ((len = bs_compress(&o->otag[i].v, tagbuf, size)) < 0)
         return -1;
      tagbuf += len;
   }

   *tagbuf++ = 0;
   *tagbuf++ = 0;

#define COORD_FACTOR 11930464.7
   int32 = (int32_t*) tagbuf;

   switch (o->type)
   {
      case OSM_NODE:
         *int32++ = ((osm_node_t*) o)->lat * COORD_FACTOR;
         *int32++ = ((osm_node_t*) o)->lon * COORD_FACTOR;
         buf = (char*) int32;
         break;

      case OSM_WAY:
         *int32++ = ((osm_way_t*) o)->ref_cnt;
         int64 = (int64_t*) int32;
         for (int i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            *int64++ = ((osm_way_t*) o)->ref[i]; 
         buf = (char*) int64;
         break;

      case OSM_REL:
         *int32++ = 0;
         buf = (char*) int32;
         break;

      default:
         return -1;
   }

   return buf - (char*) so;
}
#endif


/* Smrenderd Websocket messages start with an initial header line. The header
 * line is a text line in 7 bit ascii. The line is delimited by '\n' from the
 * following data. The '\n' may be omitted if there is no following data. The
 * total length of the line is limited to 1024 bytes, including the '\n'. The
 * line consists of at least three parameters which are delimited by one ore
 * more space characters (0x20). The first parameter contains the protocol
 * version which is "SMWS/1.0". Future versions may be released. The 2nd
 * argument contains the message type. The following message types are defined:
 * "cmd", "object", and "error". The third parameter depends on the second and
 * qualifies more specifically the type of message.
 * Commands: "next" (get next object of query)
 *           "disconn" (disconnect websocket)
 * Errors: "unexp" (last message was unexpected and thus ignored)
 *         "notsup" (not supported, i.e. msg understood but not supported in that way)
 *         "protonotsup" (protocol version not supported)
 *         "badmsg" (last message was not understood)
 *         "nodata" (no more data is available according to the latest command)
 *         "illdata" (something with the data in the message (not the header) was wrong)
 *         "again" (system temporary unavailable, e.g. if an internal error occured)
 *         "ack" (simple acknoledgement that last command was understood. This is just necessary if the command does not
 *         generate any visible result.)
 * Objects: "node", "way", "relation", "osm".
 */


 
int http_init_ws(int fd, const char *u, qcache_t **qc)
{
   struct bboxi bbi;
   int i;
   size_t len;
   //qcache_t *qc;

   if (u == NULL)
      return -500;

   if (*u != '?')
      return -404;

   u++;

   if ((i = parse_bbi(u, &bbi)) < 0)
      return i;

   if ((*qc = qc_get_bbi(&bbi)) == NULL)
      return -500;

#define str_write(x, y) write(x, y, strlen(y))

   len = str_write(fd,
         "HTTP/1.1 101 Switching Protocols\r\n"
         "Server: Smrenderd\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "\r\n");
   return len;
}


enum {WS_MSGT_OBJ, WS_MSGT_CMD, WS_MSGT_ERROR};
enum {WS_CMDT_NEXT, WS_CMDT_DISCONN};
enum {WS_ERRT_ACK, WS_ERRT_UNEXP, WS_ERRT_NTOSUP, WS_ERRT_PROTONOTSUP,
   WS_ERRT_BADMSG, WS_ERRT_NODATA, WS_ERRT_ILLDATA, WS_ERRT_AGAIN};
enum {WS_OBJT_OSM, WS_OBJT_NODE, WS_OBJT_WAY, WS_OBJT_REL};

static const char *msgt_[] = {"object", "cmd", "status", NULL};
static const char *objt_[] = {"osm", "node", "way", "relation", NULL};
static const char *cmdt_[] = {"next", "disconn", NULL};
static const char *errt_[] = {"0,ack", "128,unexp", "129,notsup", "130,protonotsup", "131,badmsg", "132,nodata", "133,illdata", "134,again", NULL};
static const char **argt_[] = {objt_, cmdt_, errt_, NULL};


static int http_ws_parse_str(bstring_t *b, const char **token)
{
   // safety check
   if (token == NULL || b == NULL)
      return -1;

   for (int i = 0; *token != NULL; i++, token++)
      if (!bs_ncmp(*b, *token, strlen(*token)))
      {
         bs_nadvance(b, strlen(*token));
         return i;
      }
   return -1;
}


static int bs_skip_char(bstring_t *b, char c)
{
   for (; b->len && *b->buf == c; bs_advance(b));
   return b->len;
}


int smws_parse_header(bstring_t *b)
{
   int msg;
   int sn;

   if (bs_ncmp(*b, "SMWS/1.0 ", 9))
      return -WS_ERRT_PROTONOTSUP;

   bs_nadvance(b, 9);
   if (!bs_skip_char(b, ' '))
      return -WS_ERRT_BADMSG;

   if ((msg = http_ws_parse_str(b, msgt_)) < 0)
      return -WS_ERRT_BADMSG;

   if (*b->buf != ' ')
      return -WS_ERRT_BADMSG;

   if (!bs_skip_char(b, ' '))
      return -WS_ERRT_BADMSG;

   if ((sn = http_ws_parse_str(b, argt_[msg])) < 0)
      return -WS_ERRT_BADMSG;

   if (b->len)
   {
      if (*b->buf == ' ')
         (void) bs_skip_char(b, ' ');
      else if (*b->buf != '\n' && *b->buf != '\r')
         return -WS_ERRT_BADMSG;
   }

   return msg | (sn << 16);
}


int smws_send_error(websocket_t *ws, int e)
{
   char buf[200];
   int i, n;

   // safety check
   if (e < 0)
      return -1;

   // Find error string in list. This loop prevents from accessing the array
   // out of bounds.
   for (i = 0; errt_[i] != NULL && i != e; i++);

   // safety check
   if (errt_[i] == NULL)
      return -1;

   n = snprintf(buf, sizeof(buf), "SMWS/1.0 %s %s\n", msgt_[WS_MSGT_ERROR], errt_[i]);
   return ws_write(ws, buf, n);
}


static int smws_skip_to_data(bstring_t *b)
{
   // safety check
   if (b == NULL)
      return 0;

   for (; b->len && *b->buf != '\n'; bs_advance(b));

   if (!b->len)
      return 0;

   bs_advance(b);

   return b->len;
}


int smws_proc_objt(bstring_t *b, hpx_tree_t *tlist, osm_obj_t **o)
{
   hpx_ctrl_t ctl;

   if (*o != NULL)
   {
      log_debug("overriding previous rule");
      free_obj(*o);
      *o = NULL;
   }

   if (!smws_skip_to_data(b))
   {
      log_debug("creating empty rule");
      *o = (osm_obj_t*) malloc_node(0);
      return 0;
   }

   hpx_init_membuf(&ctl, b->buf, b->len);
   if ((read_osm_obj(&ctl, &tlist, o)) < 0)
   {
      // FIXME: Is errno set appripriately by read_osm_obj()?
      log_msg(LOG_ERR, "read_osm_obj() failed: %s", strerror(errno));
      return -1;
   }

   return 0;
}
 

int smws_print_onode(websocket_t *ws, const osm_obj_t *o)
{
   FILE *f;
   char *buf = NULL;
   size_t len;

   log_debug("opening memory stream");
   if ((f = open_memstream(&buf, &len)) == NULL)
   {
      log_errno(LOG_ERR, "open_memstream() failed");
      return -1;
   }
   print_onode(f, o);
   print_onode(stdout, o);
   fclose(f);
   log_debug("writing %ld bytes to websocket", len);
   ws_write(ws, buf, len);
   free(buf);
   return 0;
}


int http_ws_com(int fd, qcache_t *qc)
{
   websocket_t ws;
   char buf[8000];
   bstring_t b;
   int msgt, n;
   osm_obj_t *o = NULL, *nobj;
   hpx_tree_t *tlist = NULL;
   int disconn = 0, err = 0;
   static char str_action[] = "_action_", str_ws_traverse[] = "ws_traverse";
   smrule_t *r = NULL;
   trv_com_t tc;

   ws_init(&ws, fd, 1000, 0);
   tc_init(&tc);
   tc.ot = qc->tree;

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   for (; !disconn;)
   {
      log_debug("reading frame...");
      if ((n = ws_read(&ws, buf, sizeof(buf) - 1)) == -1)
      {
         // FIXME: this error should be handled better
         log_errno(LOG_ERR, "frame buffer too small");
         return -500;
      }
      if (!n)
         break;

      b.buf = buf;
      b.len = n;
      b.buf[b.len] = '\0';

      log_debug("parsing header");
      if ((msgt = smws_parse_header(&b)) < 0)
      {
         log_msg(LOG_WARN, "smws_parse_header() failed: %d", msgt);
         smws_send_error(&ws, -msgt);
         continue;
      }
      log_debug("type = %d", msgt);

      switch (msgt & 0xffff)
      {
         case WS_MSGT_OBJ:
            log_debug("command 'object'");
            if (r != NULL)
            {
               log_debug("restarting thread");
               tc_break(&tc);
               log_debug("freeing old rule");
               free(r);
               r = NULL;
            }
            // processing new object
            if (smws_proc_objt(&b, tlist, &o) < 0)
            {
               smws_send_error(&ws, WS_ERRT_ILLDATA);
               break;
            }

            if ((n = match_attr(o, "_action_", NULL)) >= 0)
            {
               smws_send_error(&ws, WS_ERRT_ILLDATA);
               free_obj(o);
               break;
            }

            // add _action_ tag here...
            struct otag *ot;
            if ((ot = realloc(o->otag, sizeof(*o->otag) * (o->tag_cnt + 1))) == NULL)
            {
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
               free_obj(o);
               smws_send_error(&ws, WS_ERRT_AGAIN);
               break;
            }
            o->otag = ot;
            set_const_tag(&o->otag[o->tag_cnt], str_action, str_ws_traverse);
            o->tag_cnt++;

            init_rule(o, &r);
            tc.r = r;
            tc_traverse(&tc);
            smws_send_error(&ws, WS_ERRT_ACK);
            break;

         case WS_MSGT_CMD:
            log_debug("command 'cmd'");
            switch (msgt >> 16)
            {
               case WS_CMDT_DISCONN:
                  smws_send_error(&ws, WS_ERRT_ACK);
                  disconn++;
                  break;

               case WS_CMDT_NEXT:
                  if ((nobj = tc_next(&tc)) != NULL)
                  {
                     smws_send_error(&ws, WS_ERRT_ACK);
                     //print_onode(stdout, nobj);
                     smws_print_onode(&ws, nobj);
                  }
                  else
                  {
                     smws_send_error(&ws, WS_ERRT_NODATA);
                  }
                  break;
            }
            break;

         default:
            log_debug("frame ignored");
      }
   }

   hpx_tm_free_tree(tlist);
   tc_free(&tc);
   ws_free(&ws);
   log_debug("exiting http_ws_com()");

   return err;
}


/*! Handle incoming connection.
 *  @param p Pointer to http_thread_t structure.
 */
void *handle_http(void *p)
{
   int fd;                    //!< local file descriptor
   char buf[HTTP_LINE_LENGTH + 1]; //!< input buffer, method, uri, and ver points to it
   char dbuf[HTTP_LINE_LENGTH + 1]; //!< copy of input buffer used for logging
   char buf0[HTTP_LINE_LENGTH + 1]; //!< input buffer to parse HTTP headers
   char *sptr;                //!< buffer used for strtok_r()
   char *method, *uri, *ver;  //!< pointers to tokens of request line
   off_t len;                 //!< length of (html) file
   int iver = 0;              //!< variable containing http version (9 = 0.9, 10 = 1.0, 11 = 1.1)
   struct sockaddr_in saddr;  //!< buffer for socket address of remote end
   socklen_t addrlen;         //!< variable containing socket address buffer length
   int headers;

   for (;;)
   {
      // accept connections on server socket
      addrlen = sizeof(saddr);
      if ((fd = accept(((http_thread_t*)p)->sfd, (struct sockaddr*) &saddr, &addrlen)) == -1)
         perror("accept"), exit(EXIT_FAILURE);

      log_debug("connection accepted");
      // read a line from socket
      if ((len = read_line(fd, buf, sizeof(buf) - 1)) == -1)
      {
         eclose(fd);
         log_access(&saddr, "", 0, 0);
         continue;
      } 
      // check if EOF, or string too long (i.e. not \n-terminated)
      if (!len || buf[len - 1] != '\n')
      {
         SEND_STATUS(fd, STATUS_400);
         log_access(&saddr, dbuf, 400, 0);
         eclose(fd);
         continue;
      }
      // \0-terminate string (and remove '\r\n')
      if (buf[len - 2] == '\r')
         buf[len - 2] = '\0';
      else
         buf[len - 1] = '\0';

      // make a copy of request line and split into tokens
      strcpy(dbuf, buf);
      method = strtok_r(buf, " ", &sptr);
      uri = strtok_r(NULL, " ", &sptr);
      if ((ver = strtok_r(NULL, " ", &sptr)) != NULL)
      {
         // check if protocol version is valid
         if (!strcmp(ver, "HTTP/1.0"))
            iver = HTTP_10;
         else if (!strcmp(ver, "HTTP/1.1"))
            iver = HTTP_11;
         else
         {
            SEND_STATUS(fd, STATUS_400);
            log_access(&saddr, dbuf, 400, 0);
            eclose(fd);
            continue;
         }
      }
      // if no protocol version is sent assume version 0.9
      else
         iver = HTTP_09;

      // check if request line contains URI and that it starts with '/'
      if ((uri == NULL) || (uri[0] != '/'))
      {
         SEND_STATUS(fd, STATUS_400);
         log_access(&saddr, dbuf, 400, 0);
         eclose(fd);
         continue;
      }

      // check if request method is "GET"
      if (!strcmp(method, "GET"))
      {
         int err;

         log_debug("initial processing of GET");
         // read HTTP headers 
         for (headers = 0, err = 0; !err;)
         {  // ...and check for input errors
            log_debug("reading HTTP header");
            len = read_line(fd, buf0, sizeof(buf0) - 1);
            if (len <= 0 || buf0[len - 1] != '\n')
            {
               SEND_STATUS(fd, STATUS_400);
               log_access(&saddr, dbuf, 400, 0);
               eclose(fd);
               err = 1;
               continue;
            }

            // check for empty line
            if (buf0[0] == '\r' && buf0[1] == '\n')
            {
               log_debug("end of HTTP header found");
               break;
            }
            // this is actually not conforming to the RFC
            if (buf0[0] == '\n')
            {
               log_debug("end of HTTP header found (only '\\n'-terminated)");
               break;
            }

            // headers are not allowed int HTTP/0.9
            if (iver == HTTP_09)
            {
               SEND_STATUS(fd, STATUS_400);
               log_access(&saddr, dbuf, 400, 0);
               eclose(fd);
               err = 1;
               continue;
            }

            if (buf0[len - 2] == '\r')
               buf0[len - 2] = '\0';
            else
               buf0[len - 1] = '\0';

            // check for websocket upgrade headers
            if (!strcmp(buf0, "Upgrade: websocket"))
            {
               log_debug("Upgrade header");
               headers |= 1;
            }
            else if (!strcmp(buf0, "Connection: Upgrade"))
            {
               log_debug("Connection header");
               headers |= 2;
            }
         } // for (headers = 0, err = 0; !err;)

         // fail in case of error during header parser
         if (err)
            continue;

         if ((headers & 3) == 3 && !strncmp(uri, WS_URI, strlen(WS_URI)))
         {
            // websocket request only allowed in HTTP/1.1
            if (iver == HTTP_11)
            {
               qcache_t *qc;
               int err;

               log_msg(LOG_INFO, "websocket request");
               if ((err = http_init_ws(fd, uri + strlen(WS_URI), &qc)) < 0)
               {
                  log_access(&saddr, dbuf, -err, 0);
                  switch (-err)
                  {
                     case 404:
                        SEND_STATUS(fd, STATUS_404);
                        break;
                     case 500:
                     default:
                        SEND_STATUS(fd, STATUS_500);
                        break;
                  }
                  continue;
               }

               log_access(&saddr, dbuf, 101, 0);
               err = http_ws_com(fd, qc);
               qc_release(qc);
               // FIXME: if http_ws_com() returns, should it not always close the connection?
               if (err < 0)
               {
                  log_access(&saddr, dbuf, -err, 0);
                  eclose(fd);
                  continue;
               }
            }
            else
            {
               SEND_STATUS(fd, STATUS_400);
               log_access(&saddr, dbuf, 400, 0);
               eclose(fd);
               continue;
            }
         }
         else
         {
            len = http_proc_get(fd, uri);
            http_flush_input_headers(fd);

            if (len < 0)
            {
               log_debug("http_proc_get returned %ld", (long) len);
               switch (len)
               {
                  case -500:
                     SEND_STATUS(fd, STATUS_500);
                     log_access(&saddr, dbuf, 500, 0);
                     break;

                  case -404:
                  default:
                     SEND_STATUS(fd, STATUS_404);
                     log_access(&saddr, dbuf, 404, 0);
               }
               eclose(fd);
            }
            else
               log_access(&saddr, dbuf, 200, len);
         }

         // http_proc_get() closes fd in case of success
         //eclose(fd);
      } // if (!strcmp(method, "GET"))
      // all other methods are not implemented
      else
      {
         http_flush_input_headers(fd);
         SEND_STATUS(fd, STATUS_501);
         log_access(&saddr, dbuf, 501, 0);
         eclose(fd);
      }
   }

   return NULL;
}


int httpd_init(struct smhttpd *smd)
{
   int so;
   struct sockaddr_in saddr;
   uint16_t port = DEF_PORT;

   // create TCP/IP socket
   if ((smd->fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
      perror("socket"), exit(EXIT_FAILURE);

   // modify socket to allow reuse of address
   so = 1;
   if (setsockopt(smd->fd, SOL_SOCKET, SO_REUSEADDR, &so, sizeof(so)) == -1)
      perror("setsockopt"), exit(EXIT_FAILURE);

   // bind it to specific port number
   saddr.sin_family = AF_INET;
   saddr.sin_port = htons(port);
   saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (bind(smd->fd, (struct sockaddr*) &saddr, sizeof(saddr)) == -1)
      perror("bind"), exit(EXIT_FAILURE);

   // make it listening
   if (listen(smd->fd, MAX_CONNS + 5) == -1)
      perror("listen"), exit(EXIT_FAILURE);

//#define SINGLE_THREADED
#ifdef SINGLE_THREADED
   smd->max_conns = 0;
   smd->htth[0].n = 0;
   smd->htth[0].sfd = smd->fd;
   handle_http(&smd->htth[0]);
   return 0;
#endif
   // create session handler tasks
   for (int i = 0; i < smd->max_conns; i++)
   {
      smd->htth[i].n = i;
      smd->htth[i].sfd = smd->fd;
#ifdef WITH_THREADS
      if ((errno = pthread_create(&smd->htth[i].th, NULL, handle_http, (void*) &smd->htth[i])))
         perror("pthread_create"), exit(EXIT_FAILURE);
#else
      switch ((smd->htth[i].pid = fork()))
      {
         case -1:
            perror("fork");
            exit(EXIT_FAILURE);

         // child
         case 0:
            handle_http((void*) &smd->htth[i]);
            exit(EXIT_SUCCESS);

      }
#endif
   }
   return 0;
}


int httpd_wait(struct smhttpd *smd)
{
#ifndef WITH_THREADS
   int so;
#endif

   // join threads
   for (int i = 0; i < smd->max_conns; i++)
#ifdef WITH_THREADS
      if ((errno = pthread_join(smd->htth[i].th, NULL)))
         perror("pthread_join"), exit(EXIT_FAILURE);
#else
      if (wait(&so) == -1)
         perror("wait"), exit(EXIT_FAILURE);
#endif

   // close server socket
   if (close(smd->fd) == -1)
      perror("close"), exit(EXIT_FAILURE);

   return 0;
}


int main_smrenderd(void)
{
   struct smhttpd *smd;

   if ((smd = malloc(sizeof(*smd) + sizeof(*smd->htth) * MAX_CONNS)) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   smd->max_conns = MAX_CONNS;
   smd->htth = (http_thread_t*) (smd + 1);

   httpd_init(smd);
   httpd_wait(smd);

   free(smd);

   return 0;
}

