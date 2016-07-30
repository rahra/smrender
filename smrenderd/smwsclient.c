#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "smrender.h"
#include "websocket.h"


int ws_connect(void)
{
   struct sockaddr_in saddr;
   uint16_t port = 8080;
   char buf[256] = "GET /ws/?bbox=14.7,43.9,14.9,44.1 HTTP/1.1\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
   int fd, len;

   if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
      log_errno(LOG_ERR, "socket() failed"), exit(1);

   saddr.sin_family = AF_INET;
   saddr.sin_port = htons(port);
   saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   if (connect(fd, (struct sockaddr*) &saddr, sizeof(saddr)) == -1)
      log_errno(LOG_ERR, "connect() failed"), exit(1);

   // send http message
   write(fd, buf, strlen(buf));

   // read answer
   if ((len = read(fd, buf, sizeof(buf))) == -1)
      log_errno(LOG_ERR, "read() failed"), exit(1);

   write(1, buf, len);
   return fd;
}


int main(int argc, char ** argv)
{
   char buf[8000];
   websocket_t ws;
   int fd, plen, len;

   fd = ws_connect();

   ws_init(&ws, fd, 1000, 1);

   for (int eof = 0; !eof;)
   {
      for (len = 0; !eof;)
      {
         if ((plen = read(0, buf + len, sizeof(buf) - len)) == -1)
            exit(1);

         // check for eof
         eof = plen == 0;

         len += plen;
         // check for "\n.\n" (single period on line)
         if (len > 2 && buf[len - 1] == '\n' &&
               buf[len - 3] == '\n' && buf[len - 2] == '.')
         {
            // remove it
            len -= 2;
            break;
         }
      }

      if ((ws_write(&ws, buf, len)) == -1)
         exit(1);

      if ((plen = ws_read(&ws, buf, sizeof(buf))) == -1)
         exit(1);

      if (!plen)
         break;

      write(1, buf, plen);
   }

   ws_free(&ws);

   close(fd);
   return 0;
}

