/*
 * util.h -- General utility functions
 *
 * Copyright (C) 2004 Henrique de Moraes Holschuh <hmh@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE

#include "rng-tools-config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include "util.h"

#include <assert.h>

/* Computes elapsed time in microseconds */
uint64_t elapsed_time(struct timeval *start,
		       struct timeval *stop)
{
	int64_t diff;

	assert(start != NULL && stop != NULL);

	diff = (stop->tv_sec - start->tv_sec) * 1000000ULL
		+ stop->tv_usec - start->tv_usec;

	return llabs(diff);
}

/* Returns kernel support level */
/* FIXME: track down safe 2.5 version */
kernel_mode_t kernel_mode( void ) {
	struct utsname buf;
	long i;
	char *p, *q;

	if (uname(&buf))
		return KERNEL_UNSUPPORTED;

	if (!strncmp(buf.sysname, "Linux", 6)) {
		i = strtol(buf.release, &p, 10); /* Major version */
		if ((i < 2) || *p != '.')
			return KERNEL_UNSUPPORTED;
		if (i > 2)
			return KERNEL_LINUX_26;
		p++;
		q = p;
		i = strtol(p, &q, 10);	/* Minor version */
		if (p == q || *q != '.' || i < 4)
			return KERNEL_UNSUPPORTED;
		if (i < 6)
			return KERNEL_LINUX_24;
		return KERNEL_LINUX_26;
	}
	return KERNEL_UNSUPPORTED;
}

