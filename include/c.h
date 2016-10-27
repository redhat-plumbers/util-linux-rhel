/*
 * Fundamental C definitions.
 */

#ifndef UTIL_LINUX_C_H
#define UTIL_LINUX_C_H

#include <limits.h>

/*
 * Compiler specific stuff
 */
#ifdef __GNUC__

/* &a[0] degrades to a pointer: a different type from an array */
# define __must_be_array(a) \
	BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), typeof(&a[0])))

#else /* !__GNUC__ */
# define __must_be_array(a)	0
# define __attribute__(_arg_)
#endif /* !__GNUC__ */


/* Force a compilation error if condition is true, but also produce a
 * result (of value 0 and type size_t), so the expression can be used
 * e.g. in a structure initializer (or where-ever else comma expressions
 * aren't permitted).
 */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

#ifndef min
# define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })
#endif

#ifndef max
# define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif


/*
 * Constant strings for usage() functions. For more info see
 * Documentation/howto-usage-function.txt and sys-utils/arch.c
 */
#define USAGE_HEADER     _("\nUsage:\n")
#define USAGE_OPTIONS    _("\nOptions:\n")
#define USAGE_SEPARATOR  _("\n")
#define USAGE_HELP       _(" -h, --help     display this help and exit\n")
#define USAGE_VERSION    _(" -V, --version  output version information and exit\n")
#define USAGE_MAN_TAIL(_man)   _("\nFor more details see %s.\n"), _man

#define UTIL_LINUX_VERSION _("%s from %s\n"), program_invocation_short_name, PACKAGE_STRING


#endif /* UTIL_LINUX_C_H */
