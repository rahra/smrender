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

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>


#define WS_FIN 0x80
#define WS_MASK 0x80

#define WS_OP_TXT 0x1
#define WS_OP_BIN 0x2
#define WS_OP_CLOSE 0x8
#define WS_OP_PING 0x9
#define WS_OP_PONG 0xa

#define WS_LEN16 0x7e
#define WS_LEN64 0x7f

//! minimum frame length
#define WS_HDR_MINLEN 2
//! maximum frame length
#define WS_HDR_MAXLEN 14
//! determine maximum payload size
#define WS_PLD_SIZE(x) ((x)->size - WS_HDR_MAXLEN)


typedef struct websocket
{
   int fd;        //!< file descriptor of socket
   int size;      //!< maximum frame size
   int mask;      //!< 0 if frames shall not be masked
   int op;
} websocket_t;

typedef struct ws_frame
{
   int len;       //!< bytes used in buffer (for reading)
   int hlen;      //!< length of header
   int64_t plen;  //!< length of payload
   int op;        //!< opcode (FIN, TXT, ...)
   int32_t mask;  //!< frame encoding mask (see RFC6455)
   char *buf;     //!< frame buffer
} ws_frame_t;


int ws_write_frame(int , ws_frame_t *);
int ws_read_frame(int , ws_frame_t *, int );

void ws_init(websocket_t *, int , int , int );
void ws_free(websocket_t *);

int ws_write(websocket_t *, const char *, int );
int ws_read(websocket_t *, char *, int );

#endif

