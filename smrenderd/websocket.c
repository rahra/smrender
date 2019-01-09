/* Copyright 2014-2016 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>.
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

/*! \file websocket.c
 * This file implements the functions for the Websocket protocol according to
 * RFC6455.
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

#ifdef WITH_LIBCRYPTO
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

#if USE_DEV_RANDOM
#include <sys/types.h>  // open()
#include <sys/stat.h>   // open()
#include <fcntl.h>      // open()
#endif

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


int32_t ws_random_mask(void)
{
   int32_t rnd;

   // handle rare case that the PRNG returns 0
   for (rnd = 0; !rnd;)
   {
#if USE_DEV_RANDOM
      static int fd = 0;

      if (!fd)
      {
         if ((fd = open("/dev/random", O_RDONLY)) == -1)
            log_errno(LOG_ERR, "error opening random source"), exit(1);
      }

      if (read(fd, &rnd, sizeof(rnd)) == -1)
            log_errno(LOG_ERR, "error reading random source"), exit(1);
#elif defined(WITH_OPENSSL)
      if (!RAND_bytes((unsigned char*) &rnd, sizeof(rnd)))
      {
         log_msg(LOG_ERR, "RAND_bytes() failed: %s",
               ERR_reason_error_string(ERR_get_error()));
         // get fallback pseudo random
         rnd = rand();
      }
#else
      log_msg(LOG_WARN, "Using rand() which is not suggested!");
      rnd = rand();
#endif
   }

   return rnd;
}


/*! This function encodes the frame defined by wf end writes it to the desired
 * file descriptor. The structure wf has to be setup properly before calling
 * ws_write_frame(). ws->hlen and ws->plen have to contain the proper lengths
 * of header and payload, and ws->buf must point to a buffer of at least
 * wf->ws->size bytes. ws->op has to have a valid opcode as defined in RFC6455
 * which is a value between 0 and 15.
 * @param wf Pointer to valid ws_frame_t structure as explained above.
 * @return The function returns the actual number of bytes written. On error,
 * unexpected EOF or a truncated write, -1 is returned and errno is set.
 */
int ws_write_frame(int fd, ws_frame_t *wf)
{
   int wlen;

   if (wf->plen <= 125)
   {
      wf->buf[1] = wf->plen;
   }
   else if (wf->plen < 0x10000)
   {
      wf->buf[1] = WS_LEN16;
      wf->buf[2] = wf->plen >> 8;
      wf->buf[3] = wf->plen;
   }
   else
   {
      int64_t plen = wf->plen;
      wf->buf[1] = WS_LEN64;
      for (int i = 9; i >= 2; i--, plen >>= 8)
         wf->buf[i] = plen;
   }

   wf->buf[0] = wf->op;

   // encode mask into header
   if (wf->mask)
   {
      // safety check
      if (wf->hlen < 4)
      {
         log_msg(LOG_EMERG, "header too short, wf->hlen = %d", wf->hlen);
         errno = EINVAL;
         return -1;
      }

      int32_t mask = wf->mask;
      wf->buf[1] |= WS_MASK;
      for (int i = 0; i < 4; i++, mask >>= 8)
         wf->buf[wf->hlen - i - 1] = mask;
   }

   // write complete frame (header + payload)
   if ((wlen = write(fd, wf->buf, wf->hlen + wf->plen)) == -1)
   {
      log_errno(LOG_ERR, "failed to write frame");
      return -1;
   }

   // check if write was truncated
   if (wlen < wf->hlen + wf->plen)
   {
      log_msg(LOG_ERR, "frame write to %d truncated", fd);
      errno = EIO;
      return -1;
   }

   return wlen;
}


/*! This function returns the number of bytes used to encode the payload
 * length.
 * @return Returns the number of bytes or -1 in case of error (i.e. len < 0).
 */
static int ws_len_bytes(int64_t len, char *len_code)
{
   // safety check
   if (len < 0)
   {
      log_msg(LOG_EMERG, "len < 0: %ld", (long) len);
      return -1;
   }

   if (len <= 125)
   {
      if (len_code != NULL)
         *len_code = len;
      return 0;
   }
   if (len <= 0xffff)
   {
      if (len_code != NULL)
         *len_code = WS_LEN16;
      return 2;
   }
   if (len_code != NULL)
      *len_code = WS_LEN64;
   return 8;
}


static int64_t ws_pld_len(const char *buf, int byte_count)
{
   int64_t plen = 0;

   for (int i = 0; i < byte_count; i++)
   {
      plen <<= 8;
      plen += (unsigned char) buf[i];
   }
   return plen;
}


/*! Decode header and payload length.
 * @param buf Pointer to data buffer containing the header.
 * @param len Length of buffer.
 * @param hdrlen Pointer to int which will receive the total length of the
 * header. This field is only set if at least as much data is in the buffer as
 * necessary to decode the payload and header length. Otherwise its value is
 * untouched.
 * @return The function returns the length of payload. If the buffer does not
 * contain the complete header at negative number is returned which represents
 * the minimum number of bytes missing in the buffer.
 */
static int64_t ws_decode_len(const char *buf, int len, int *hdrlen)
{
   int64_t plen;
   int hlen;

   if (len < WS_HDR_MINLEN)
      return len - WS_HDR_MINLEN;

   hlen = WS_HDR_MINLEN;

   switch (buf[1] & 0x7f)
   {
      case WS_LEN16:
         hlen += 2;
         break;

      case WS_LEN64:
         hlen += 8;
         break;

      default:
         *hdrlen = hlen + (buf[1] & WS_MASK ? 4 : 0);
         return buf[1] & 0x7f;
   }

   // check if enough data is in buffer
   if (len < hlen)
      return len - hlen;

   *hdrlen = hlen + (buf[1] & WS_MASK ? 4 : 0);
   plen = ws_pld_len(buf + WS_HDR_MINLEN, hlen - WS_HDR_MINLEN);

   // check for protocol violation
   if (plen <= 125 || (hlen == 10 && plen <= 0xffff))
   {
      log_msg(LOG_ERR, "Protocol violation: length encoded incorrect! hlen = %d, plen = %ld", hlen, (long) plen);
      // FIXME: how to handle this....
   }

   return plen;
}


/*! Read and decode frame from websocket connect. This function does not unmask
 * the data.
 *  @param fd File descriptor to read from.
 *  @param wf Destination frame buffer which will receive to frame data.
 *  @param size maximum size of frame buffer.
 *  @return Returns the number of bytes copied to buf (frame header + payload).
 *  If the size of frame buffer is
 *  not large enough to take the full data of the next frame, the buffer is
 *  filled with wf->size bytes and errno is set to ENOBUFS. The data is not
 *  cleared from the internal buffer, i.e. a subsequent read will return the
 *  same data. To handle this condition errno must be set to 0 before calling
 *  ws_read_frame(). In case of error -1 is returned and errno is set
 *  appropriately according to read() if data could not be read from socket. If
 *  the internal buffer of ws is too small for the frame, errno is set to
 *  ENOMEM. Please note that the websocket protocol allows data to be sent of
 *  up to 2^63 bytes but the return value of ws_read_frame() is of type int
 *  which typically is not more than 32 bits.
 */
int ws_read_frame(int fd, ws_frame_t *wf, int size)
{
   int len, need_data;

   wf->len = wf->hlen = wf->plen = 0;
   for (need_data = WS_HDR_MINLEN; need_data > 0;)
   {
      // the internal buffer.
      log_debug("reading on %d", fd);

      // safety check if buffer is large enough
      if (size - wf->len < need_data)
      {
         log_msg(LOG_ERR, "buffer too small");
         errno = ENOMEM;
         return -1;
      }

      // read data
      if ((len = read(fd, wf->buf + wf->len, need_data)) == -1)
      {
         int e = errno;
         log_msg(LOG_ERR, "read failed on %d: %s", fd, strerror(errno));
         errno = e;
         return -1;
      }

      // check for EOF
      if (!len)
      {
         log_msg(LOG_WARN, "unexpected EOF");
         return 0;
      }

      wf->len += len;
      need_data -= len;

      if (!wf->hlen)
      {
         need_data = ws_decode_len(wf->buf, wf->len, &wf->hlen);
         if (need_data < 0)
         {
            need_data = -need_data;
            continue;
         }
         wf->plen = need_data;
      }

      need_data = wf->hlen + wf->plen - wf->len;
   } // for (need_data = WS_HDR_MINLEN; need_data > 0;)

   // check if data is masked and set mask accordingly
   if (wf->buf[1] & WS_MASK)
   {
      wf->mask = ntohl(*((int32_t*) (wf->buf + wf->hlen - 4)));
      if (!wf->mask)
      {
         log_msg(LOG_WARN, "mask bit set but mask = 0");
         // FIXME: How to handle this? Does it violate the RFC?
      }
   }
   else
      wf->mask = 0;

   wf->op = wf->buf[0];

   return wf->hlen + wf->plen;
}


void ws_init(websocket_t *ws, int fd, int size, int mask)
{
   ws->fd = fd;
   ws->size = size;
   ws->mask = mask;
   ws->op = WS_OP_BIN;
}


void ws_free(websocket_t * UNUSED(ws))
{
   // nothing to free yet
}


/* Note: ws->op and ws->mask must be initialized
 */
int ws_write(websocket_t *ws, const char *buf, int size)
{
   char frmbuf[ws->size];
   ws_frame_t wf;
   int len, wlen, hlen;

   wf.buf = frmbuf;

   for (wlen = 0; size > 0; wlen += wf.plen)
   {
      wf.op = wlen ? 0 : ws->op & 0xf;

      len = size;
      // determine header and payload length
      wf.hlen = (ws->mask ? 4 : 0) + WS_HDR_MINLEN;
      hlen = ws_len_bytes(size, &wf.buf[1]);
      if (len > ws->size - wf.hlen - hlen)
         len = ws->size - wf.hlen - hlen;
      else
         wf.op |= WS_FIN;
      wf.plen = len;
      wf.hlen += ws_len_bytes(wf.plen, &wf.buf[1]);

      // safety check (should never happen...)
      if (wf.hlen + wf.plen > ws->size)
      {
         log_msg(LOG_EMERG, "wf->hlen + wf->plen > wf->size!");
         return -1;
      }

      // copy data to frame buffer
      memcpy(wf.buf + wf.hlen, buf, wf.plen);
      buf += wf.plen;
      size -= wf.plen;

      // mask if necessary
      if (ws->mask)
      {
         wf.mask = ws_random_mask();
         wf.buf[1] |= WS_MASK;
         ws_mask(wf.buf + wf.hlen, wf.plen, wf.mask);
      }
      else
         wf.mask = 0;

      // write and check for error
      if ((len = ws_write_frame(ws->fd, &wf)) == -1)
      {
         log_errno(LOG_ERR, "ws_write_frame() failed");
         return -1;
      }

      // check for EOF
      if (!len || len < wf.hlen + wf.plen)
      {
         log_msg(LOG_WARN, "unexpected EOF!?");
         break;
      }
   }

   return wlen;
}


int ws_read(websocket_t *ws, char *buf, int size)
{
   char frmbuf[ws->size];
   ws_frame_t wf;
   int len, rlen;

   wf.buf = frmbuf;
   wf.op = 0;

   for (rlen = 0; !(wf.op & WS_FIN); rlen += wf.plen)
   {
      if ((len = ws_read_frame(ws->fd, &wf, ws->size)) == -1)
      {
         log_errno(LOG_ERR, "ws_read_frame() failed");
         return -1;
      }

      if (!len)
         return 0;

      // check dest buffer size
      if (size < wf.plen)
      {
         errno = ENOMEM;
         wf.plen = size;
      }

      // safety check
      if (wf.hlen + wf.plen != len)
      {
         log_msg(LOG_EMERG, "wf.hlen + wf.plen != len");
         return -1;
      }

      if (wf.mask)
         ws_mask(wf.buf + wf.hlen, wf.plen, wf.mask);

      // protocol checks
      if (wf.mask && ws->mask)
         log_msg(LOG_WARN, "input data is masked but it shouldn't");
      if (!wf.mask && !ws->mask)
         log_msg(LOG_WARN, "input data is not masked but it should");

      // copy data to user buffer
      memcpy(buf, wf.buf + wf.hlen, wf.plen);
      buf += wf.plen;
      size -= wf.plen;
   }

   return rlen;
}

