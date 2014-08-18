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

#ifndef SMHTTP_H
#define SMHTTP_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>

#include "bxtree.h"
#include "rdata.h"

#define HTTP_09 9
#define HTTP_10 10
#define HTTP_11 11

//! default listening port number
#define DEF_PORT 8080
//! number of sessions handled concurrently
#define MAX_CONNS 25
//! buffer length of lines being received
#define HTTP_LINE_LENGTH 1024
//! root path of contents (must be full path)
#define DOC_ROOT "/home/eagle"

// HTTP status message strings
#define STATUS_500 "HTTP/1.0 500 Internal Server Error\r\n\r\n<html><body>500 -- INTERNAL SERVER ERROR</h1></body></html>\r\n"
#define STATUS_501 "HTTP/1.0 501 Not Implemented\r\n\r\n<html><body><h1>501 -- METHOD NOT IMPLEMENTED</h1></body></html>\r\n"
#define STATUS_400 "HTTP/1.0 400 Bad Request\r\n\r\n<html><body><h1>400 -- BAD REQUEST</h1></body></html>\r\n"
#define STATUS_200 "HTTP/1.0 200 OK\r\n"
#define STATUS_404 "HTTP/1.0 404 Not Found\r\n\r\n<html><body><h1>404 -- NOT FOUND</h1></body></html>\r\n"

//! macro for sending answers to browser
#define SEND_STATUS(f, s) write(f, s, strlen(s))


#define API06_URI "/api/0.6/"
#define WS_URI "/ws/"

//! data structure handled over to threads
typedef struct http_thread
{
#ifdef WITH_THREADS
   pthread_t th;
#else
   pid_t pid;
#endif
   int n;
   int sfd;
} http_thread_t;


int main_smrenderd(void);

/* smdb.c */
bx_node_t *get_obj_bb(bx_node_t *, const struct bbox *);


#endif

