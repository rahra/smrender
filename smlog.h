/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SMLOG_H
#define SMLOG_H


#include <syslog.h>


#define LOG_WARN LOG_WARNING

#define log_debug(x...) log_msg(LOG_DEBUG, ## x)
#define log_warn(x...) log_msg(LOG_WARN, ## x)


FILE *init_log(const char*, int);
void log_msg(int, const char*, ...);


#endif

