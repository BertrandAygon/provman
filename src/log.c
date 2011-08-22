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
 * @file log.c
 *
 * @brief
 * Macros and functions for logging
 *
 ******************************************************************************/

#include <stdarg.h>
#include <stdio.h>

#include "config.h"
#include "log.h"
#include "error.h"

#ifdef PROVMAN_LOGGING
static FILE *g_log_file;
#endif

int provman_log_open(const char *log_file_name)
{
	int ret_val = PROVMAN_ERR_NONE;

#ifdef PROVMAN_LOGGING
	if (!g_log_file)
	{
		g_log_file = fopen(log_file_name, "w");

		if (!g_log_file)
			ret_val = PROVMAN_ERR_OPEN;
	}
#endif

	return ret_val;
}

void provman_log_close()
{
#ifdef PROVMAN_LOGGING
	if (g_log_file)
		fclose(g_log_file);
#endif
}

#ifdef PROVMAN_LOGGING

void provman_log_printf(unsigned int line_number, const char *file_name, 
				const char *message, ...)
{
	va_list args;

	if (g_log_file) {
		va_start(args, message);
		fprintf(g_log_file, "%s:%u ",file_name, line_number);
		vfprintf(g_log_file, message, args);
		fprintf(g_log_file, "\n");
		va_end(args);
		fflush(g_log_file);
	}
}

void provman_logu_printf(const char *message, ...)
{
	va_list args;

	if (g_log_file) {
		va_start(args, message);
		vfprintf(g_log_file, message, args);
		fprintf(g_log_file, "\n");
		va_end(args);
		fflush(g_log_file);
	}
}

#endif
