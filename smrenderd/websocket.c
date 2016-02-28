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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "smrender.h"
#include "websocket.h"


/*! This function masks a buffer according to RFC6455. This function is
 * Endian-safe, i.e. the more siginifficant bits of the mask are applied first
 * independent of the endianess of the machine.
 *  @param buf Pointer to buffer.
 *  @param size Size of buffer.
 *  @param mask Mask to apply.
 */
void ws_mask(char *buf, int size, int32_t mask)
{
   if (!mask) return;
   mask = htonl(mask);
   for (int i = 0; i < size; i++)
      buf[i] ^= ((char*) &mask)[i % 4];
}


int ws_write_frame(websocket_t *ws, const char *buf, int size, int32_t mask)
{
   int len, wlen;
   char wsh[14];

   wsh[0] = WS_FIN | WS_OP_TXT;

   if (size >= 125)
   {
      wsh[1] = size;
      len = 2;
   }
   else if (size > 0x10000)
   {
      wsh[1] = 126;
      wsh[2] = size >> 8;
      wsh[3] = size;
      len = 4;
   }
   else
   {
      int64_t plen = size;
      wsh[1] = 127;
      for (int i = 0; i < 8; i++, plen >>= 8)
         wsh[9 - i] = plen;
      len = 10;
   }

   if (mask)
   {
      wsh[1] |= WS_MASK;
      for (int i = 0; i < 4; i++, mask >>= 8)
         wsh[len + 3 - i] = mask;
      len += 4;
   }

   if ((wlen = write(ws->fd, wsh, len)) == -1)
   {
      int e = errno;
      log_msg(LOG_ERR, "failed to write header to %d: %s", ws->fd, strerror(errno));
      errno = e;
      return -1;
   }

   if (wlen < len)
   {
      log_msg(LOG_ERR, "header write to %d truncated", ws->fd);
      errno = EIO;
      return -1;
   }

   if ((wlen = write(ws->fd, buf, size)) == -1)
   {
      int e = errno;
      log_msg(LOG_ERR, "failed to write data to %d: %s", ws->fd, strerror(errno));
      errno = e;
      return -1;
   }

   if (wlen < size)
   {
      log_msg(LOG_ERR, "write data to %d truncated", ws->fd);
      errno = EIO;
      return -1;
   }

   return size + len;
}


/*! This function seems useless but is there to avoid type-punned pointer
 * warning.
 */
static int32_t ws_mask_ptr(void *p, int off)
{
   return *((int32_t*) (p + off));
}


/*! Read frame from websocket connect.
 *  @param ws Pointer to websocket_t structure.
 *  @param buf Destination buffer for frame payload.
 *  @param size Size of buffer.
 *  @param mask Mask which has to be applied to the data afterwards. If mask is
 *  0, no masking is necessary.
 *  @return Returns the number of bytes copied to buf. If the size of buf is
 *  not large enough to take the full payload of the next frame, the buffer is
 *  filled with size bytes errno is set to ENOBUFS. The data is not cleared
 *  from the internal buffer, i.e. a subsequent read will return the same data.
 *  In case of error -1 is returned and errno is set appropriately according to
 *  read() if data could not be read from socket. If the internal buffer of ws
 *  is too small for the frame, errno is set to ENOMEM. Please not the the
 *  websocket protocol allows data to be sent of up to 2^63 bytes but the
 *  return value of ws_read_frame() as well as size is of type int which
 *  typically is not more the 32 bits.
 */
int ws_read_frame(websocket_t *ws, char *buf, int size, int32_t *mask)
{
   int64_t plen;
   int len, need_data;

   for (need_data = ws->len < 2;;)
   {
      // read data from socket if more data is needed
      if (need_data)
      {
         // the internal buffer.
         log_debug("reading on %d", ws->fd);
         if ((len = read(ws->fd, ws->buf + ws->len, ws->size - ws->len)) == -1)
         {
            int e = errno;
            log_msg(LOG_ERR, "read failed on %d: %s", ws->fd, strerror(errno));
            errno = e;
            return -1;
         }
         // FIXME: EOF should be handled better than here!
         if (!len)
            return -1;
         ws->len += len;

         // check if enough data is in buffer
         if (ws->len < 2)
            continue;
      }

      // decode length of app data
      plen = 0;
      if ((ws->buf[1] & 0x7f) >= 125)
      {
         plen = ws->buf[1] & 0x7f;
         len = 0;
      }
      else if ((ws->buf[1] & 0x7f) == 126)
      {
         // check if enough data is in buffer
         if (ws->len < 4)
         {
            need_data = 1;
            continue;
         }

         for (int i = 0; i < 2; i++)
         {
            plen <<= 8;
            plen += ws->buf[2 + i];
         }
         if (plen > 126)
         {
            log_msg(LOG_WARN, "length encoded incorrect, 16 instead if 7 bits");
            //FIXME: how to handle this?
         }
         len = 2;
      }
      else // if ((ws->buf[1] & 0x7f) == 127) ... if condition unnecessary because there's no other possible case
      {
         // check if enough data is in buffer
         if (ws->len < 10)
         {
            need_data = 1;
            continue;
         }

         for (int i = 0; i < 8; i++)
         {
            plen <<= 8;
            plen += ws->buf[2 + i];
         }
         if (plen > 0x10000)
         {
            log_msg(LOG_WARN, "length encoded incorrect, 64 instead if 16 bits");
            //FIXME: how to handle this RFC violation?
         }
         len = 8;
      }

      // check if the whole payload already is in the internal buffer
      if (plen <= ws->len - 2)
         break;
      else
         need_data = 1;
      
      // check if buffer is too small
      if (ws->len == ws->size && plen > ws->len - 2)
      {
         errno = ENOMEM;
         return -1;
      }
   }

   // check if data is masked and set mask accordingly
   if (ws->buf[1] & WS_MASK)
   {
      *mask = ntohl(ws_mask_ptr(ws->buf, len + 2));
      len += 4;
   }
   else
      *mask = 0;

   // check if destination buffer is large enough
   if (size >= plen - len)
   {
      size = plen - len;
      memcpy(buf, ws->buf + 2 + len, size);

      // move trailing data of ctrl buffer to the beginning
      memmove(ws->buf, ws->buf + 2 + plen, ws->len - (plen + 2));
      ws->len -= plen + 2;
   }
   else
   {
      memcpy(buf, ws->buf + 2 + len, size);
      errno = ENOBUFS;
   }

   return size;
}


websocket_t *ws_init(int fd, int size)
{
   websocket_t *ws;

   if (size <= 0)
   {
      errno = EINVAL;
      return NULL;
   }

   if ((ws = malloc(sizeof(*ws) + size)) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
      return NULL;
   }

   ws->fd = fd;
   ws->size = size;
   ws->len = 0;
   return ws;
}


void ws_free(websocket_t *ws)
{
   free(ws);
}

