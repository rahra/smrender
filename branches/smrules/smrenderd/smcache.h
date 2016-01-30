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

#ifndef SMCACHE_H
#define SMCACHE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif

#include "bxtree.h"


#define MAX_CACHE 3


struct bboxi
{
   int coord[4];
};

//! query cache structure
typedef struct qcache
{
   struct bboxi bb;  //!< bounding box of query
   bx_node_t *tree;  //!< root element of tree
   time_t age;       //!< age of cache entry, 0 means cache free
   int ctr;          //!< usage counter, 0 means unused
} qcache_t;


#ifdef WITH_THREADS
#define qc_lock(x) pthread_mutex_lock(x)
#define qc_unlock(x) pthread_mutex_unlock(x)
#define qc_wait(x, y) pthread_cond_wait(x, y)
#define qc_signal(x) pthread_cond_signal(x)
#else
#define qc_lock(x)
#define qc_unlock(x)
#define qc_wait(x, y)
#define qc_signal(x)
#endif


qcache_t *qc_lookup(const struct bboxi *);
void qc_release(qcache_t *);
void qc_cleanup(void);
qcache_t *qc_put(const struct bboxi *, bx_node_t *);

#endif

