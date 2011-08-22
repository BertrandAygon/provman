/*
 * Provman
 *
 * Copyright (C) 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

/*!
 * @file log.h
 *
 * @brief
 * Macros and functions for logging
 *
 ******************************************************************************/

#ifndef PROVMAN_LOG_H
#define PROVMAN_LOG_H

#ifdef __cplusplus
extern "C"
{
#endif

/* TODO: Not thread safe */

int provman_log_open(const char *log_file_name);
void provman_log_printf(unsigned int line_number, const char *file_name,
				const char *message, ...);
void provman_logu_printf(const char *message, ...);
void provman_log_close(void);

#ifdef PROVMAN_LOGGING
	#define PROVMAN_LOGF(message, ...) provman_log_printf(__LINE__, \
			__FILE__, message, __VA_ARGS__)
	#define PROVMAN_LOG(message) provman_log_printf(__LINE__, __FILE__, \
			message)
	#define PROVMAN_LOGUF(message, ...) provman_logu_printf(message, \
			__VA_ARGS__)
	#define PROVMAN_LOGU(message) provman_logu_printf(message)
#else
	#define PROVMAN_LOGF(message, ...)
	#define PROVMAN_LOG(message)
	#define PROVMAN_LOGUF(message, ...)
	#define PROVMAN_LOGU(message)
#endif

#ifdef __cplusplus
}
#endif

#endif
