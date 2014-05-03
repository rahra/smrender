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
#include <inttypes.h>

#include "smrender.h"
//#include "smrender_dev.h" // only there for print_onode()
#include "smhttp.h"


struct smhttpd
{
   int fd;
   int max_conns;
   http_thread_t *htth;
};

/*! create httpd acces log to stdout
 *  @param saddr Pointer to sockaddr_in structure containing
 *               the address of the remote end.
 *  @param req Request line.
 *  @param stat Response status.
 *  @param siz Number of bytes (of HTTP body) returned.
 */
void log_access(const struct sockaddr_in *saddr, const char *req, int stat, int siz)
{
   char addr[100], tms[100];
   time_t t;
   struct tm tm;

   if (inet_ntop(AF_INET, &saddr->sin_addr.s_addr, addr, 100) == NULL)
      perror("inet_ntop"), exit(EXIT_FAILURE);
   t = time(NULL);
   (void) localtime_r(&t, &tm);
   (void) strftime(tms, 100, "%d/%b/%Y:%H:%M:%S %z", &tm);
   printf("%s - - [%s] \"%s\" %d %d \"-\" \"-\"\n", addr, tms, req, stat, siz);
}


/** Read \r\n-terminated line from file descriptor.
  * @param s File descriptor.                      
  * @param buf Pointer to buffer.                  
  * @param size Size of buffer.                    
  * @return Number of characters read, -1 on error, errno is set.
  */                                                             
int read_line(int fd, char *buf, int size)                       
{                                                                
   int r = 0, s = size;                                          

   // buffer too small?
   if (--size <= 0)    
   {                   
      errno = EMSGSIZE;
      return(-1);      
   }                   

   while (1)
   {        
      // read a single character.
      if (recv(fd, buf, 1, 0) <= 0)
         return(-1);               

      // decrease buffer size counter
      size--;                        

      // Detect end-of-line characters (\r\n)
      switch (*buf)                          
      {                                      
         case '\r' :                         
            r = 1;                           
            break;                           

         case '\n' :
            // previous char '\r'?
            if (r == 1)           
            {                     
               // end-of-line found, return.
               *(buf - 1) = 0;              
               return(s - size - 2);        
            }                               

         default :
            r = 0;
      }           

      buf++;

      // buffer full?
      if (size == 0) 
      {              
         *buf = 0;   
         errno = ENOBUFS;
         return(-1);     
      }                  
   }                     
}                        


static int http_flush_input_headers(int fd)
{
   int buf[2048];
   int s, len;

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
   len += fprintf(f, "%sServer: smrenderd\r\nDate: %s\r\n\r\n", STATUS_200, buf);
   return len;
}


static int print_onode(FILE *f, const osm_obj_t *o)
{
   return 0;
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


int http_proc_get(int fd, const char *uri)
{
   log_debug("processing request '%s'", uri);
   if (!strncmp(API06_URL, uri, strlen(API06_URL)))
      return http_proc_api06(fd, uri + strlen(API06_URL));
#if 0
   else if (!strncmp(WS_URL, uri, strlen(WS_URL)))
   {
   }
#endif

   return -404;
}


/*! Handle incoming connection.
 *  @param p Pointer to http_thread_t structure.
 */
void *handle_http(void *p)
{
   int fd;                    //!< local file descriptor
   char buf[HTTP_LINE_LENGTH + 1]; //!< input buffer
   char dbuf[HTTP_LINE_LENGTH + 1]; //!< copy of input buffer used for logging
   char *sptr;                //!< buffer used for strtok_r()
   char *method, *uri, *ver;  //!< pointers to tokens of request line
   off_t len;                 //!< length of (html) file
   int v09 = 0;               //!< variable containing http version (0 = 0.9, 1 = 1.0, 1.1)
   struct sockaddr_in saddr;  //!< buffer for socket address of remote end
   socklen_t addrlen;         //!< variable containing socket address buffer length

   for (;;)
   {
      // accept connections on server socket
      addrlen = sizeof(saddr);
      if ((fd = accept(((http_thread_t*)p)->sfd, (struct sockaddr*) &saddr, &addrlen)) == -1)
         perror("accept"), exit(EXIT_FAILURE);

      log_debug("connection accepted");
      // read a line from socket
      if (read_line(fd, buf, sizeof(buf)) == -1)
      {
         eclose(fd);
         log_access(&saddr, "", 0, 0);
         continue;
      } 

      // check if string is empty
      if (!strlen(buf))
      {
         SEND_STATUS(fd, STATUS_400);
         log_access(&saddr, dbuf, 400, 0);
         eclose(fd);
         continue;
      }

      // make a copy of request line and split into tokens
      strcpy(dbuf, buf);
      method = strtok_r(buf, " ", &sptr);
      uri = strtok_r(NULL, " ", &sptr);
      if ((ver = strtok_r(NULL, " ", &sptr)) != NULL)
      {
         // check if protocol version is valid
         if ((strcmp(ver, "HTTP/1.0") != 0) && (strcmp(ver, "HTTP/1.1") != 0))
         {
            SEND_STATUS(fd, STATUS_400);
            log_access(&saddr, dbuf, 400, 0);
            eclose(fd);
            continue;
         }
      }
      // if no protocol version is sent assume version 0.9
      else
         v09 = 1;

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
         http_flush_input_headers(fd);
         if ((len = http_proc_get(fd, uri)) < 0)
         {
            log_debug("http_proc_get returned %ld", (long) len);
            switch (len)
            {
               case -500:
                  SEND_STATUS(fd, STATUS_500);
                  log_access(&saddr, dbuf, 500, 0);
                  break;

               default:
                  SEND_STATUS(fd, STATUS_404);
                  log_access(&saddr, dbuf, 404, 0);
            }
            eclose(fd);
         }
         else
            log_access(&saddr, dbuf, 200, len);

         // http_proc_get() closes fd in case of success
         //eclose(fd);
         continue;
      }

      // all other methods are not implemented
      SEND_STATUS(fd, STATUS_501);
      log_access(&saddr, dbuf, 501, 0);
      eclose(fd);
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

   fprintf(stderr, "%s\n", "e(xtrem) t(iny) Httpd by Bernhard R. Fischer, V0.1");
   return 0;
}


int httpd_wait(struct smhttpd *smd)
{
   int so;

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

