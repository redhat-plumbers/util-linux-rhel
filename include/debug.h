/*
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 *
 * This file may be distributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_DEBUG_H
#define UTIL_LINUX_DEBUG_H

#include <stdarg.h>
#include <string.h>

struct dbg_mask { char *mname; int val; };
#define UL_DEBUG_EMPTY_MASKNAMES {{ NULL, 0 }}

#define UL_DEBUG_MASK(m)         m ## _debug_mask
#define UL_DEBUG_DEFINE_MASK(m)  int UL_DEBUG_MASK(m)
#define UL_DEBUG_DECLARE_MASK(m) extern UL_DEBUG_DEFINE_MASK(m)
#define UL_DEBUG_DEFINE_MASKNAMES(m) static const struct dbg_mask m ## _masknames[]

/*
 * Internal mask flags (above 0xffffff)
 */
#define __UL_DEBUG_FL_NOADDR   (1 << 24)       /* Don't print object address */

/* l - library name, p - flag prefix, m - flag postfix, x - function */
#define __UL_DBG(l, p, m, x) \
	do { \
		if ((p ## m) & l ## _debug_mask) { \
			fprintf(stderr, "%d: %s: %8s: ", getpid(), # l, # m); \
			x; \
		} \
	} while (0)

#define __UL_DBG_CALL(l, p, m, x) \
	do { \
		if ((p ## m) & l ## _debug_mask) { \
			x; \
		} \
	} while (0)

#define __UL_DBG_FLUSH(l, p) \
	do { \
		if (l ## _debug_mask && \
		    l ## _debug_mask != p ## INIT) { \
			fflush(stderr); \
		} \
	} while (0)


#define __UL_INIT_DEBUG(lib, pref, mask, env) \
	do { \
		if (lib ## _debug_mask & pref ## INIT) \
		; \
		else if (!mask) { \
			char *str = getenv(# env); \
			if (str) \
				lib ## _debug_mask = parse_envmask(lib ## _masknames, str); \
		} else \
			lib ## _debug_mask = mask; \
		if (lib ## _debug_mask) { \
			if (getuid() != geteuid() || getgid() != getegid()) \
				lib ## _debug_mask |= __UL_DEBUG_FL_NOADDR; \
		} \
		lib ## _debug_mask |= pref ## INIT; \
		if (lib ## _debug_mask != pref ## INIT) { \
			__UL_DBG(lib, pref, INIT, ul_debug("library debug mask: 0x%04x", \
					lib ## _debug_mask)); \
		} \
	} while (0)


static inline void __attribute__ ((__format__ (__printf__, 1, 2)))
ul_debug(const char *mesg, ...)
{
	va_list ap;
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}
static inline int parse_envmask(const struct dbg_mask flagnames[],
				const char *mask)
{
	int res;
	char *ptr;

	/* let's check for a numeric mask first */
	res = strtoul(mask, &ptr, 0);

	/* perhaps it's a comma-separated string? */
	if (*ptr != '\0' && flagnames) {
		char *msbuf, *ms, *name;
		res = 0;

		ms = msbuf = strdup(mask);
		if (!ms)
			return res;

		while ((name = strtok_r(ms, ",", &ptr))) {
			size_t i = 0;
			ms = ptr;

			while (flagnames[i].mname) {
				if (!strcmp(name, flagnames[i].mname)) {
					res |= flagnames[i].val;
					break;
				}
				++i;
			}
			/* nothing else we can do by OR-ing the mask */
			if (res == 0xffff)
				break;
		}
		free(msbuf);
	}
	return res;
}
#endif /* UTIL_LINUX_DEBUG_H */
