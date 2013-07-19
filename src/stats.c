/*
 * stats.c -- Statistics helpers
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


/* For printf types macros (PRIu64) */
#define __STDC_FORMAT_MACROS

#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include <assert.h>

#include "fips.h"
#include "stats.h"


static char stat_prefix[20] = "";

void set_stat_prefix(const char* prefix)
{
	if (prefix) {
		strncpy(stat_prefix, prefix, sizeof(stat_prefix)-1);
		stat_prefix[sizeof(stat_prefix)-1] = 0;
	} else stat_prefix[0] = 0;
}

static void scale_mult_unit(char *unit, size_t unitsize, 
		       const char *baseunit, 
		       double *value_min,
		       double *value_avg,
		       double *value_max)
{
	unsigned int mult = 0;
	char multchar[] = "KMGTPE";

/*	assert(unit != NULL && baseunit != NULL && 
		value_min != NULL && value_avg != NULL && value_max != NULL); */

	while ((*value_min >= 1024.0) && (*value_avg >= 1024.0) && 
	       (*value_max >= 1024.0) && (mult < sizeof(multchar))) {
		mult++;
		*value_min = *value_min / 1024.0;
		*value_max = *value_max / 1024.0;
		*value_avg = *value_avg / 1024.0;
	}
	if (mult)
		snprintf(unit, unitsize, "%ci%s", multchar[mult-1], baseunit);
	else
		strncpy(unit, baseunit, unitsize);
	unit[unitsize-1] = 0;
}

/* Updates min-max stat */
void update_stat(struct rng_stat *stat, uint64_t value)
{
	uint64_t overflow = stat->num_samples;

	assert(stat != NULL);

	if ((stat->min == 0 ) || (value < stat->min)) stat->min = value;
	if (value > stat->max) stat->max = value;
	if (++stat->num_samples > overflow) {
		stat->sum += value;
	} else {
		stat->sum = value;
		stat->num_samples = 1;
	}
}

char *dump_stat_counter(char *buf, size_t size,
		       const char *msg, uint64_t value)
{
	assert(buf != NULL && msg != NULL);

	snprintf(buf, size-1, "%s%s: %" PRIu64 , stat_prefix, msg, value);
	buf[size-1] = 0;

	return buf;
}

char *dump_stat_stat(char *buf, size_t size,
		    const char *msg, const char *unit, struct rng_stat *stat)
{
	double avg = 0.0;

	assert(stat != NULL && msg != NULL && unit != NULL && buf != NULL);

	if (stat->num_samples > 0)
		avg = (double)stat->sum / stat->num_samples;

	buf[size-1] = 0;
	snprintf(buf, size-1,
		 "%s%s: (min=%" PRIu64 "; avg=%.3f; max=%" PRIu64 ")%s",
		 stat_prefix, msg, stat->min, avg, stat->max, unit);

	return buf;
}

char *dump_stat_bw(char *buf, size_t size,
		  const char *msg, const char *unit, 
		  struct rng_stat *stat, 
		  uint64_t blocksize)
{
	char unitscaled[20];
	double bw_avg = 0.0, bw_min = 0.0, bw_max = 0.0;

	assert(stat != NULL && msg != NULL && unit != NULL);

	if (stat->max > 0)
		bw_min = (1000000.0 * blocksize) / stat->max;
	if (stat->min > 0)
		bw_max = (1000000.0 * blocksize) / stat->min;
	if (stat->num_samples > 0)
		bw_avg = (1000000.0 * blocksize * stat->num_samples) / stat->sum;

	scale_mult_unit(unitscaled, sizeof(unitscaled), unit,
			&bw_min, &bw_avg, &bw_max);

	buf[size-1] = 0;
	snprintf(buf, size-1, "%s%s: (min=%.3f; avg=%.3f; max=%.3f)%s/s",
		 stat_prefix, msg, bw_min, bw_avg, bw_max, unitscaled);

	return buf;
}

