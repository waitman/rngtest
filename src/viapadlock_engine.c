/*
 * viapadlock.c -- VIA PadLock interface
 *
 * Copyright 2004,2005 Henrique de Moraes Holschuh <hmh@debian.org>
 * Copyright 2004 Martin Peck <coderman@peertech.org>
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
#ifdef VIA_ENTSOURCE_DRIVER

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sched.h>

#include <assert.h>

#include "viapadlock_engine.h"

#define MAX_CPUS 32
#define DEVCPU_DEFAULT_PATH "/dev/cpu/%u"

/*
 * VIA PadLock RNG types
 *
 * type 1: as described in VIA Nehemiah RNG Programming Guide version 1.0
 *    with a functional string filter
 */
typedef enum {
	VIA_RNG_NONE,		/* PadLock RNG not functional/blacklisted */
	VIA_RNG_TYPE1_ONESRC,	/* PadLock RNG type 1, one noise source */
	VIA_RNG_TYPE1_TWOSRC	/* PadLock RNG type 1, two noise sources */
} via_rng_type_t;

/*
 * VIA PadLock RNG type 1
 *   CentaurHauls Family 6 Model 9 Stepping 3 and above.
 *
 * The RNG has measurable (but small) consecutive bit correlations, so
 * for best H we should use the maximum possible bit distance (maximum 
 * xstore divisor), which gives us one bit and discards the next
 * three.
 *
 * The string filter could be used to avoid a FIPS discard for long
 * runs, but we *want* to know the RNG produced a long run, so we keep 
 * it disabled: it makes the code that much simpler to interface with
 * the rest of rngd.
 *
 * Double-source RNGs have double the bandwidth, and about the same
 * adjacent bit correlation (since xstore will always fetch a byte
 * from the same source), so it is completely transparent to us other
 * than for setup and MSR verification purposes.
 *
 * ALL CPUs in a system must have the RNG enabled for xstore to work
 * right, otherwise bad things will happen.  Since we don't know which
 * CPU we are using, they must also have the same type of RNG, and the
 * RNGs need to be configured exactly the same.
 */

/* RNG CPUID constants */
enum {
	CENTAUR_EXFF_LEVEL	= 0xc0000000,
	CENTAUR_EXFF_RNG	= 0xc0000001,
	CENTAUR_EXFF_RNG_MASK	= (1 << 2)
};

/* RNG MSR Control Register */
enum {
	/* 31:22 reserved
	 * 21:16 string filter count
	 * 15:15 string filter failed
	 * 14:14 string filter enabled
	 * 13:13 raw bits enabled
	 * 12:10 dc bias value
	 * 09:08 noise source select
	 * 07:07 reserved
	 * 06:06 rng enabled
	 * 05:05 reserved
	 * 04:00 current byte count
	 */
	MSR_VIA_RNG1		= 0x110b,
	VIA1_STRFILT_CNT_SHIFT	= 16,
	VIA1_STRFILT_FAIL	= (1 << 15),
	VIA1_STRFILT_ENABLE	= (1 << 14),
	VIA1_STRFILT_MIN	= 8,
	VIA1_STRFILT_MAX	= 63,
	VIA1_STRFILT_MASK	= (VIA1_STRFILT_MAX << VIA1_STRFILT_CNT_SHIFT),
	VIA1_RAWBITS_ENABLE	= (1 << 13),
	VIA1_NOISE_SRC_SHIFT	= 8,
	VIA1_NOISE_SRC_MASK	= (3 << VIA1_NOISE_SRC_SHIFT),
	VIA1_RNG_ENABLE		= (1 << 6),
	VIA1_DCBIAS_SHIFT	= 10,
	VIA1_DCBIAS_MAX		= 7,
	VIA1_DCBIAS_MASK	= (VIA1_DCBIAS_MAX << VIA1_DCBIAS_SHIFT),
	VIA1_XSTORE_CNT_MASK	= 0x0f,
};

/* Can be higher than 1 on SMP machines */
static unsigned int viapadlock_engines_detected = 0;

/* RNG state */
typedef struct {
	uint32_t	MSR_LSW;
	uint32_t	MSR_LSW_MASK;
	via_rng_type_t	rng_type;
	int		msr_fd[MAX_CPUS];
	uint32_t	divisor;
} viapadlock_rng_t;
static viapadlock_rng_t viapadlock_state;

/* CPU devices path */
static char cpudev_path[PATH_MAX+1];
static const char* const cpudev_default_path = DEVCPU_DEFAULT_PATH;



/* Decode CPUID level 1, algorithm from Linux 2.4.28 */
static uint32_t decode_cpu_revision(uint32_t tfms)
{
	unsigned int family, model, stepping;
	
	family = (tfms >> 8) & 0xf;
	model = (tfms >> 4) & 0xf;
	if ((tfms & 0xf00) == 0xf00) {
		family += ((tfms >> 20) & 0xff);
		model |= (tfms >> 12) & 0xf0;
	}
	stepping = tfms & 0xf;
	return (family << 16) | (model << 8) | stepping;
}

/*
 * Detect a VIA PadLock RNG
 *
 * Returns:
 *  0	: detected RNG
 * -1	: did not detect RNG
 * other: errno
 *
 * This test is less strict than what we could make it be,
 * to avoid the need to fix this code way too often.
 */
static int detect_via_padlock_rng(int cpuid_fd, viapadlock_rng_t *rng,
		int is_first_rng)
{
	uint32_t cpuid_buf[8];
	uint32_t CPU_revision, msr_lsw_mask;
	via_rng_type_t rng_type;

	assert(rng != NULL);
	assert(cpuid_fd != -1);

	/* This would be much easier if we would just parse
	 * /proc/cpuinfo, I suppose. But I am not writing a parser
	 * into rngd just to deal with cumbersome ASCII interfaces...
	 */

#define READCPUID(LEVEL, SIZE) do { \
	if (lseek(cpuid_fd, LEVEL, SEEK_SET) != LEVEL) return errno; \
	if (read(cpuid_fd, &cpuid_buf, SIZE) == -1) return errno; \
} while (0);
	
	READCPUID(0, 32);

	/* CPU Vendor "CentaurHauls" & CPUID level >= 1 */
	if (cpuid_buf[0] == 0 ||
	    cpuid_buf[1] != 0x746e6543 ||
	    cpuid_buf[3] != 0x48727561 ||
	    cpuid_buf[2] != 0x736c7561) return -1;

	/* CPU revision from CPUID level 1 */
	CPU_revision = decode_cpu_revision(cpuid_buf[4]);

	/* Optimized for CENTAUR_EXFF_RNG == CENTAUR_EXFF_LEVEL + 1 */
	READCPUID(CENTAUR_EXFF_LEVEL, 32);

	/* Presence of Centaur Extended Features Flags */
	if (cpuid_buf[0] < CENTAUR_EXFF_RNG) return -1;

	/* Presence of the PadLock RNG */
	if (!(cpuid_buf[7] & CENTAUR_EXFF_RNG_MASK)) return -1;

	/* Ok, we have a CentaurHauls chip with the PadLock RNG */
	/* We assume that the kernel always enables SSE because
	 * it is not easy to test for that directly */

	/* Blacklist CentaurHauls Family 6 Model 9 Stepping 0 to 2
	 * plus any unknown chips earlier than that */
	if (CPU_revision <= 0x0060902) return -1;

	/* Default capabilities mask */
	msr_lsw_mask = VIA1_STRFILT_ENABLE | VIA1_RAWBITS_ENABLE |
		VIA1_DCBIAS_MASK | VIA1_RNG_ENABLE;

	/*
	 * One-noise-source RNGs: CentaurHauls F6 M9 S3-7
	 * Two-noise-sources RNGs: anything newer than that
	 */
	if (CPU_revision <= 0x0060907)
		rng_type = VIA_RNG_TYPE1_ONESRC;
	else {
		rng_type = VIA_RNG_TYPE1_TWOSRC;
		msr_lsw_mask |= VIA1_NOISE_SRC_MASK;
	}

	/* Is it the first chip, or identical to all others detected so
	 * far ? */
	if (!is_first_rng) {
		if (rng_type != rng->rng_type || 
		  	msr_lsw_mask != rng->MSR_LSW_MASK) {
			return -1; /* no, so it is not usable */
		}
	} else {
		rng->rng_type = rng_type;
		rng->MSR_LSW_MASK = msr_lsw_mask;
		rng->MSR_LSW = 0;
		rng->divisor = 0;
	}

	return 0;
}


/* 
 * Initialize VIA PadLock RNGs
 *
 * devicepath is the path to the cpuid and msr devices,
 * with a %u escape where the CPU number should go.
 * NULL implies the default path (/dev/cpu/%u)
 *
 * Returns:
 *   0 if no working VIA PadLock RNG was detected
 *   1 if all VIA PadLock RNGs have been detected
 *  -1 if an error happened (errno will be set)
 */
int viapadlock_rng_init(const char* devicepath)
{
	int msr_fd, cpuid_fd;
	char devpath[PATH_MAX+1];
	int i;
	int error;

	if (viapadlock_engines_detected != 0)
		viapadlock_rng_free();

	if (!devicepath) devicepath = cpudev_default_path;
	strncpy(cpudev_path, devicepath, sizeof(cpudev_path));
	cpudev_path[sizeof(cpudev_path)-1] = 0;

	msr_fd = cpuid_fd = -1;
	error = 0;

	for(i = 0; i < MAX_CPUS; i++) {
		snprintf(devpath, sizeof(devpath), cpudev_path, i);
		devpath[sizeof(devpath)-1] = 0;
		strncat(devpath, "/msr", sizeof(devpath)-1);
		msr_fd = open(devpath, O_RDWR);
		if (msr_fd == -1) {
			if (errno != ENXIO && errno != ENOENT)
				error = errno; /* not past last CPU yet */
			if (errno == EIO) error = -1; /* wrong CPU type */
			break;
		}

		snprintf(devpath, sizeof(devpath), cpudev_path, i);
		devpath[sizeof(devpath)-1] = 0;
		strncat(devpath, "/cpuid", sizeof(devpath)-1);
		cpuid_fd = open(devpath, O_RDONLY);
		if (cpuid_fd == -1) {
			error = errno;
			if (errno == EIO) error = -1; /* wrong CPU type */
			break;
		}

		/*
		 * ALL CPUs must have a working RNG or we can't use them
		 * (as we do not have a good interface to bind a thread
		 * exclusively to a specific CPU, and thus, to a specific
		 * RNG).
		 */
		error = detect_via_padlock_rng(cpuid_fd,
			  &viapadlock_state, 
			  (viapadlock_engines_detected == 0));
		if (error) break;

		viapadlock_state.msr_fd[viapadlock_engines_detected] = msr_fd;
		viapadlock_engines_detected++;

		close(cpuid_fd);
		msr_fd = cpuid_fd = -1;
	}
	if (msr_fd != -1) close(msr_fd);
	if (cpuid_fd != -1) close (cpuid_fd);
	if (error) {
		viapadlock_rng_free();
		if (error != -1) {
			errno = error;
			return -1;
		}
		return 0;
	}

	return (viapadlock_engines_detected > 0) ? 1 : 0;
}

/*
 * Free up any resources used by the VIA PadLock RNGs.
 * It is legal to viapadlock_rng_init() after this
 */
void viapadlock_rng_free(void)
{
	while (viapadlock_engines_detected > 0) {
		if (viapadlock_state.msr_fd[viapadlock_engines_detected-1] != -1)
			close(viapadlock_state.msr_fd[viapadlock_engines_detected-1]);
		viapadlock_engines_detected--;
	}
}

/*
 * Generate recommended configuration for the given quality
 *
 * quality:
 *  3+ HIGH   - DC_bias: default; whitener: enabled; divisor: 3; H>0.75
 *  2= MEDIUM - DC_bias: default; whitener: enabled; divisor: 2; H>0.75
 *  1= LOW    - DC_bias: default; whitener: enabled; divisor: 1; H>0.75
 *  0= DEFAULT - same as LOW
 *
 * We always enable as many sources as possible (since we assume them to be
 * of equal quality and completely uncoupled).  The whitener is always enabled,
 * as the RNG is not good enough for crypto without it.  We never use the full
 * RNG bandwidth (divisor = 0) because of measurable correlation between
 * two consecutive bits from the same source.  The string filter is always
 * disabled, so that we get to know (through FIPS test failures) if the
 * combined RNG streams fail.
 *
 * Note that when VIA releases a PadLock RNG with SHA-1 post-processing,
 * the above figures will change for such beasts...
 */
double viapadlock_rng_generate_config(unsigned int quality, 
	viapadlock_rng_config_t* cfg)
{
	assert(cfg != NULL);

	cfg->dc_bias = 0;
	cfg->whitener = 1;
	cfg->string_filter = 0;
	/* the code will ignore this when invalid, so it is
	 * safe to just set it to both sources */
	cfg->noise_source = VIAPADLOCK_RNG1_SOURCE_AB;

	if (quality >= 3 || quality == 0)
		cfg->divisor = 3;
	else cfg->divisor = quality;

	return 0.75; /* Cryptographic Research is quite clear on their
			paper that this is the low bound to use, even if
			we are likely working with H>0.90 here, since the
			whitener is enabled.
			
			Unfortunately, I have no idea how better H gets
			when higher divisors are used, so we go with 
			safety over speed. */
}

/*
 * Enable/disable VIA PadLock RNGs
 *
 * enable: 1 = enable, 0 = disable
 * 
 * If cfg is non-null, configure the RNG when enabling.
 *
 * Returns -1 on error (errno set), 0 otherwise
 */
int viapadlock_rng_enable(unsigned int enable, viapadlock_rng_config_t* cfg)
{
	int error;
	int i;
	uint32_t buf[2];

	if (!viapadlock_engines_detected) {
		errno = ENXIO;
		return -1;
	}

	if (cfg) {
		/* Clean up config */
		if (cfg->dc_bias > VIA1_DCBIAS_MAX) 
			cfg->dc_bias = VIA1_DCBIAS_MAX;
		if (cfg->string_filter && cfg->string_filter < VIA1_STRFILT_MIN)
			cfg->string_filter = VIA1_STRFILT_MIN;
		if (cfg->string_filter > VIA1_STRFILT_MAX)
			cfg->string_filter = VIA1_STRFILT_MAX;

		viapadlock_state.MSR_LSW = 
			(((cfg->whitener)? 0 : VIA1_RAWBITS_ENABLE) |
			(cfg->dc_bias << VIA1_DCBIAS_SHIFT) |
			(cfg->noise_source << VIA1_NOISE_SRC_SHIFT) |
			((cfg->string_filter > 0)? 
			     VIA1_STRFILT_ENABLE | 
			      (cfg->string_filter << VIA1_STRFILT_CNT_SHIFT) : 
			     0) |
			VIA1_RNG_ENABLE) & viapadlock_state.MSR_LSW_MASK;
		viapadlock_state.divisor = cfg->divisor;
	} else if (!viapadlock_state.MSR_LSW) {
		/* never configured before */
		errno = EINVAL;
		return -1;
	}

	buf[0] = viapadlock_state.MSR_LSW;
	if (!enable) buf[0] &= ~VIA1_RNG_ENABLE;

	buf[1] = 0;
	error = 0;
	for (i = 0; i < viapadlock_engines_detected; i++) {
		if ((lseek(viapadlock_state.msr_fd[i], MSR_VIA_RNG1, SEEK_SET)
		    != MSR_VIA_RNG1) ||
		    (write(viapadlock_state.msr_fd[i], &buf, 8) == -1) ) {
			error = errno;
			break;
		}
	}

	errno = error;
	if (error) return -1;
	return 0;
}

/*
 * VIA xstore
 */
static inline uint32_t via_xstore(uint64_t *addr, uint32_t edx_in)
{
	uint32_t eax_out, edi_out;

	asm(".byte 0x0F,0xA7,0xC0 /* xstore %%edi (addr=%0) */"
	    :"=m"(*addr), "=a"(eax_out), "=D"(edi_out)
	    :"D"(addr), "d"(edx_in));
	return eax_out;
}


/*
 * Read data from a VIA PadLock RNG set
 *
 * Returns the number of bytes read if no errors happen
 *  -1 if an error happened, with errno set.
 *
 * Returns EAGAIN if the read was interrupted by a RNG event,
 *   (and resets and reconfigures the RNG)
 */
ssize_t viapadlock_rng_read(void* buf, size_t size)
{
	/*
	 * Some VIA CPUs can write too much data to the buffer,
	 * overruning data.  This is an absurdly dangerous bug,
	 * so better alocate an entire cacheline or two in case
	 * VIA will screw this up again even worse.
	 *
	 * Needs 16-byte alignment, must be static, must be at least
	 * 16-byte long.
	 */
	static uint64_t xstore_buffer[16] __attribute__((aligned (16)));

	size_t bytes_read = 0;
	uint32_t xstore_divisor, xstore_flags;
	unsigned int s, i;
	struct timespec ts;

	assert (buf != NULL);

	if (!viapadlock_engines_detected) {
		errno = ENXIO;
		return -1;
	}
	xstore_divisor = viapadlock_state.divisor;
	s = 8 >> (xstore_divisor & 3);

	/* time to wait for more data if all FIFOs are empty */
	/* since there are 4 of them, we can wait more than the average
	 * time it takes to fill one of them up */
	ts.tv_sec = 0;
	ts.tv_nsec = (viapadlock_state.rng_type == VIA_RNG_TYPE1_ONESRC) ?
			20000 : 10000;

	/* algorithm from mtrng 0.4, by Martin Peck */
	while (size > 0) {
		for (i = 0; i < 2; i++) {
			/* Use XSTORE to get RNG data and current config */
			xstore_flags = via_xstore(xstore_buffer,
					xstore_divisor);

			/* Make sure no one messed with the RNG */
			if ((xstore_flags & viapadlock_state.MSR_LSW_MASK) !=
			    viapadlock_state.MSR_LSW) {
				/* reset it */
				if (!viapadlock_rng_enable(1, NULL)) return -1;
				errno = EAGAIN;
				return -1;
			}

			/* Retry a dry read immediately, only useful on 
			 * slow C5XL */
			if ((xstore_flags & VIA1_XSTORE_CNT_MASK) == s) break;
		}

		if ((xstore_flags & VIA1_XSTORE_CNT_MASK) != s) {
			/* no random data, or other weirdness */
			sched_yield(); /* Eats a lot of SYS time?! */
			continue;
		}

		if (s > size) s = size;
		memcpy((unsigned char *)buf + bytes_read, &xstore_buffer, s);

		size -= s;
		bytes_read += s;
	}

	errno = 0;
	return bytes_read;
}

#endif /* VIA_ENTSOURCE_DRIVER */
