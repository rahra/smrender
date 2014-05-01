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

#include "smhttp.h"


struct smhttpd
{
   int fd;
   HttpThread_t htth[MAX_CONNS];
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


/*! Close file descriptor and exit on error.
 *  @param fd File descriptor to be closed.
 */
void eclose(int fd)
{
   if (close(fd) == -1)
      perror("close"), exit(EXIT_FAILURE);
}


/*! Handle incoming connection.
 *  @param p Pointer to HttpThread_t structure.
 */
void *handle_http(void *p)
{
   int fd;                    //!< local file descriptor
   int lfd;                   //!< file descriptor of local (html) file
   char buf[HTTP_LINE_LENGTH + 1]; //!< input buffer
   char dbuf[HTTP_LINE_LENGTH + 1]; //!< copy of input buffer used for logging
   char *sptr;                //!< buffer used for strtok_r()
   char *method, *uri, *ver;  //!< pointers to tokens of request line
   off_t len;                 //!< length of (html) file
   int v09 = 0;               //!< variable containing http version (0 = 0.9, 1 = 1.0, 1.1)
   struct sockaddr_in saddr;  //!< buffer for socket address of remote end
   socklen_t addrlen;         //!< variable containing socket address buffer length
   struct stat st;            //!< buffer for fstat() of (html) file
   char path[strlen(DOC_ROOT) + HTTP_LINE_LENGTH + 2]; //!< buffer for requested path to file
   char rpath[PATH_MAX + 1];  //!< buffer for real path of file
   char *fbuf;                //!< pointer to data of file

   for (;;)
   {
      // accept connections on server socket
      addrlen = sizeof(saddr);
      if ((fd = accept(((HttpThread_t*)p)->sfd, (struct sockaddr*) &saddr, &addrlen)) == -1)
         perror("accept"), exit(EXIT_FAILURE);

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
         strcpy(path, DOC_ROOT);
         strcat(path, uri);
         // test if path is a real path and is within DOC_ROOT
         // and file is openable
         if ((realpath(path, rpath) == NULL) || 
               strncmp(rpath, DOC_ROOT, strlen(DOC_ROOT)) ||
               ((lfd = open(rpath, O_RDONLY)) == -1))

         {
            SEND_STATUS(fd, STATUS_404);
            log_access(&saddr, dbuf, 404, 0);
            eclose(fd);
            continue;
         }

         // stat file
         if (fstat(lfd, &st) == -1)
            perror("fstat"), exit(EXIT_FAILURE);

         // check if file is regular file
         if (!S_ISREG(st.st_mode))
         {
            SEND_STATUS(fd, STATUS_404);
            log_access(&saddr, dbuf, 404, 0);
            eclose(fd);
            eclose(lfd);
            continue;
         }

         // get memory for file to send
         if ((fbuf = malloc(st.st_size)) == NULL)
         {
            SEND_STATUS(fd, STATUS_500);
            log_access(&saddr, dbuf, 500, 0);
            eclose(fd);
            eclose(lfd);
            continue;
         }

         // read data of file
         len = read(lfd, fbuf, st.st_size);
         eclose(lfd);

         // check if read was successful
         if (len == -1)
         {
            SEND_STATUS(fd, STATUS_500);
            log_access(&saddr, dbuf, 500, 0);
            free(fbuf);
            eclose(fd);
            continue;
         }

         // create response dependent on protocol version
         if (!v09)
         {
            SEND_STATUS(fd, STATUS_200);
            snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n\r\n", len);
            write(fd, buf, strlen(buf));
         }
         write(fd, fbuf, len);
         free(fbuf);
         log_access(&saddr, dbuf, 200, len);
         eclose(fd);
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
   if ((smd->fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
      perror("socket"), exit(EXIT_FAILURE);

   // modify socket to allow reuse of address
   so = 1;
   if (setsockopt(smd->fd, SOL_SOCKET, SO_REUSEADDR, &so, sizeof(so)) == -1)
      perror("setsockopt"), exit(EXIT_FAILURE);

   // bind it to specific port number
   saddr.sin_family = AF_INET;
   saddr.sin_port = htons(port);
   saddr.sin_addr.s_addr = INADDR_ANY;
   if (bind(smd->fd, (struct sockaddr*) &saddr, sizeof(saddr)) == -1)
      perror("bind"), exit(EXIT_FAILURE);

   // make it listening
   if (listen(smd->fd, MAX_CONNS + 5) == -1)
      perror("listen"), exit(EXIT_FAILURE);

   // create session handler tasks
   for (int i = 0; i < MAX_CONNS; i++)
   {
      smd->htth[i].n = i;
      smd->htth[i].sfd = smd->fd;
#ifdef MULTITHREADED
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
   for (int i = 0; i < MAX_CONNS; i++)
#ifdef MULTITHREADED
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


#if TEST_SMHTTP
int main(int argc, char **argv)
{
   struct smhttpd smd;

   httpd_init(&smd);
   httpd_wait(&smd);
   return 0;
}
#endif

