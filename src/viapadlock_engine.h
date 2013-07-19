/*
 * viapadlock.h -- VIA PadLock interface
 *
 * Copyright 2004,2005 Henrique de Moraes Holschuh <hmh@debian.org>
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

#ifndef VIAPADLOCK__H
#define VIAPADLOCK__H

#define _GNU_SOURCE

#include "rng-tools-config.h"
#ifdef  VIA_ENTSOURCE_DRIVER

#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>

typedef enum {
	VIAPADLOCK_RNG1_SOURCE_A  = 0,
	VIAPADLOCK_RNG1_SOURCE_B  = 1,
	VIAPADLOCK_RNG1_SOURCE_AB = 2
} viapadlock_noise_source_t;

typedef struct {
	unsigned int dc_bias;		/* DC bias, 0 = default */
	unsigned int string_filter;	/* String filter length, 0=disabled */
	viapadlock_noise_source_t 	/* Noise source */
		noise_source;
	unsigned int whitener;		/* Whitener, 1=enabled, 0=disabled */ 
	unsigned int divisor;		/* XSTORE divisor to use, 0-3 */
} viapadlock_rng_config_t;


/* 
 * Initialize VIA PadLock RNGs
 *
 * devicepath is the path to the cpuid and msr devices,
 * with a %u escape where the CPU number should go.
 * NULL implies the default path (/dev/cpu/%u)
 *
 * All RNGs in a system must be exactly of the same type,
 * and all CPUs must have a functional RNG.
 * 
 * Returns:
 *   0 if no functional VIA PadLock RNG set was detected
 *   1 if a functional VIA PadLock RNG set was detected
 *  -1 if an error happened (errno will be set)
 */
extern int viapadlock_rng_init(const char* devicepath);

/*
 * Free up resources used by the VIA PadLock RNGs
 * (does not disable or otherwise touch the RNGs)
 */
extern void viapadlock_rng_free(void);

/*
 * Generate recommended configuration for the given quality.
 *
 * Returns the estimated H (entropy per bit of data).
 *
 * quality:
 *  0: default for entropy pool seeding
 *  1 (low) < 2 (medium) < 3+ (high)
 *
 *  If the random data will be used directly, always use quality=HIGH.
 *
 * The bandwidth available from the RNG decreases with quality by a factor
 * of approximately 2 per step.
 *
 * We always enable as many sources as possible (as we assume them to be
 * of equal quality and completely uncoupled).  The whitener is always enabled,
 * as the RNG is not good enough for crypto without it.  We never use the full
 * RNG bandwidth because of measurable correlation between two consecutive bits
 * from the same source.  The string filter is always disabled, so that we get
 * to know (through FIPS test failures) if the combined RNG streams fail a long
 * run test.
 */
extern double viapadlock_rng_generate_config(unsigned int quality, 
	viapadlock_rng_config_t* cfg);

/*
 * Enable/disable VIA PadLock RNGs
 *
 * enable: 1 = enable, 0 = disable
 *
 * If enabling the RNGs, and cfg is not NULL, the RNGs are configured.
 * This is required the first time the RNGs are enabled.
 *
 * Returns -1 on error (errno set), 0 otherwise
 */
extern int viapadlock_rng_enable(unsigned int enable, 
		viapadlock_rng_config_t* cfg);

/*
 * Read data from a VIA PadLock RNG set
 *
 * Returns the number of bytes read if no errors happen
 *  -1 if an error happened, with errno set.
 */
extern ssize_t viapadlock_rng_read(void* buf, size_t size);

#endif /* VIA_ENTSOURCE_DRIVER */
#endif /* VIAPADLOCK__H */
