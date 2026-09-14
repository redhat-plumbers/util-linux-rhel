#ifndef UTIL_LINUX_FILEUTILS
#define UTIL_LINUX_FILEUTILS

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAVE_LINUX_OPENAT2_H
# include <linux/openat2.h>
#endif

#include "c.h"
#include "pathnames.h"

extern int mkstemp_cloexec(char *template);

extern int xmkstemp(char **tmpname, const char *dir, const char *prefix);

static inline FILE *xfmkstemp(char **tmpname, const char *dir, const char *prefix)
{
	int fd;
	FILE *ret;

	fd = xmkstemp(tmpname, dir, prefix);
	if (fd == -1)
		return NULL;

	if (!(ret = fdopen(fd, "w+" UL_CLOEXECSTR))) {
		close(fd);
		return NULL;
	}
	return ret;
}

#ifdef HAVE_OPENAT
static inline FILE *fopen_at(int dir, const char *filename,
                             int flags, const char *mode)
{
	int fd = openat(dir, filename, flags);
	if (fd < 0)
		return NULL;

	return fdopen(fd, mode);
}
#endif

static inline int is_same_inode(const int fd, const struct stat *st)
{
	struct stat f;

	if (fstat(fd, &f) < 0)
		return 0;
	else if (f.st_dev != st->st_dev || f.st_ino != st->st_ino)
		return 0;
	return 1;
}

extern int ul_open_no_symlinks(const char *path, int flags, mode_t mode);
extern int ul_openat_resolve(int dirfd, const char *path, int flags,
			     mode_t mode, unsigned long long resolve);

#ifndef RESOLVE_NO_SYMLINKS
# define RESOLVE_NO_SYMLINKS	0x04
#endif
#ifndef RESOLVE_BENEATH
# define RESOLVE_BENEATH	0x08
#endif

extern int dup_fd_cloexec(int oldfd, int lowfd);
extern int get_fd_tabsize(void);

extern int mkdir_p(const char *path, mode_t mode);
extern char *stripoff_last_component(char *path);

/* Size of the buffer for ul_fd_mkpath() */
#define UL_FDPATH_BUFSIZ	(sizeof(_PATH_PROC_FDDIR) + sizeof(stringify_value(INT_MAX)))

extern char *ul_fd_mkpath(char *buf, size_t bufsz, int fd);
extern char *ul_fd_get_path(int fd);

#endif /* UTIL_LINUX_FILEUTILS */
