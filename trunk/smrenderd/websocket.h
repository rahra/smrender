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
#define WS_OP_TXT 0x01
#define WS_MASK 0x80


typedef struct websocket
{
   int fd;        //!< file descriptor of socket
   int size;      //!< size of input buffer
   int len;       //!< data within input buffer
   uint8_t buf[]; //!< pointer to input buffer
} websocket_t;


void ws_mask(char *, int , int32_t );
int ws_write_frame(websocket_t *, const char *, int size, int32_t );
int ws_read_frame(websocket_t *, char *, int , int32_t *);
websocket_t *ws_init(int , int );
void ws_free(websocket_t *);

#endif

