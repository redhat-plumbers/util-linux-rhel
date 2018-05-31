/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Written by Karel Zak <kzak@redhat.com>
 */
#include <ctype.h>

#include "c.h"
#include "ttyutils.h"

/*
 * Backported for RHEL7.6 libsmartcols
 */
static int get_env_int(const char *name)
{
	const char *cp = getenv(name);

	if (cp) {
		char *end = NULL;
		long x;

		errno = 0;
		x = strtol(cp, &end, 10);

		if (errno == 0 && end && *end == '\0' && end > cp &&
		    x > 0 && x <= INT_MAX)
			return x;
	}

	return -1;
}

int get_terminal_dimension(int *cols, int *lines)
{
	int c = 0, l = 0;

#if defined(TIOCGWINSZ)
	struct winsize	w_win;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w_win) == 0) {
		c = w_win.ws_col;
		l = w_win.ws_row;
	}
#elif defined(TIOCGSIZE)
	struct ttysize	t_win;
	if (ioctl (STDOUT_FILENO, TIOCGSIZE, &t_win) == 0) {
		c = t_win.ts_cols;
		l = t_win.ts_lines;
	}
#endif

	if (cols && c <= 0)
		c = get_env_int("COLUMNS");
	if (lines && l <= 0)
		l = get_env_int("LINES");

	if (cols)
		*cols = c;
	if (lines)
		*lines = l;
	return 0;
}

int get_terminal_width(void)
{
#ifdef TIOCGSIZE
	struct ttysize	t_win;
#endif
#ifdef TIOCGWINSZ
	struct winsize	w_win;
#endif
        const char	*cp;

#ifdef TIOCGSIZE
	if (ioctl (STDIN_FILENO, TIOCGSIZE, &t_win) == 0)
		return t_win.ts_cols;
#endif
#ifdef TIOCGWINSZ
	if (ioctl (STDIN_FILENO, TIOCGWINSZ, &w_win) == 0)
		return w_win.ws_col;
#endif
        cp = getenv("COLUMNS");
	if (cp) {
		char *end = NULL;
		long c;

		errno = 0;
		c = strtol(cp, &end, 10);

		if (errno == 0 && end && *end == '\0' && end > cp &&
		    c > 0 && c <= INT_MAX)
			return c;
	}
	return 0;
}

int get_terminal_name(int fd,
		      const char **path,
		      const char **name,
		      const char **number)
{
	const char *tty;
	const char *p;

	if (name)
		*name = NULL;
	if (path)
		*path = NULL;
	if (number)
		*number = NULL;

	tty = ttyname(fd);
	if (!tty)
		return -1;
	if (path)
		*path = tty;
	tty = strncmp(tty, "/dev/", 5) == 0 ? tty + 5 : tty;
	if (name)
		*name = tty;
	if (number) {
		for (p = tty; p && *p; p++) {
			if (isdigit(*p)) {
				*number = p;
				break;
			}
		}
	}
	return 0;
}


#ifdef TEST_PROGRAM
# include <stdlib.h>
int main(void)
{
	const char *path, *name, *num;

	if (get_terminal_name(STDERR_FILENO, &path, &name, &num) == 0) {
		fprintf(stderr, "tty path:   %s\n", path);
		fprintf(stderr, "tty name:   %s\n", name);
		fprintf(stderr, "tty number: %s\n", num);
	}
	fprintf(stderr,         "tty width:  %d\n", get_terminal_width());

	return EXIT_SUCCESS;
}
#endif
