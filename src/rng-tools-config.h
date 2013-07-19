/* rng-tools-config.h.  Generated from rng-tools-config.h.in by configure.  */
/* rng-tools-config.h.in.  Generated from configure.ac by autoheader.  */

/* Name of the source HRNG device */
#define DEVHWRANDOM "/dev/hwrng"

/* Name of the kernel RNG device */
#define DEVRANDOM "/dev/random"

/* Name of package */
#define PACKAGE "rng-tools"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "rng-tools@packages.debian.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "rng-tools"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "rng-tools 2-unofficial-mt.14"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "rng-tools"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "2-unofficial-mt.14"

/* Name of the pidfile for rngd */
#define PIDFILE "/var/run/rngd.pid"

/* Syslog facility to use. */
#define SYSLOG_FACILITY LOG_DAEMON

/* Version number of package */
#define VERSION "2-unofficial-mt.14"

/* Include code for VIA PadLock TRNG driver */
/* #undef VIA_ENTSOURCE_DRIVER */

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */
