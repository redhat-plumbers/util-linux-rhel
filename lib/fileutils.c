/*
 * Copyright (C) 2012 Sami Kerola <kerolasa@iki.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "c.h"
#include "canonicalize.h"
#include "fileutils.h"
#include "pathnames.h"
#include "strutils.h"

int mkstemp_cloexec(char *template)
{
#ifdef HAVE_MKOSTEMP
	return mkostemp(template, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC);
#else
	int fd, old_flags, errno_save;

	fd = mkstemp(template);
	if (fd < 0)
		return fd;

	old_flags = fcntl(fd, F_GETFD, 0);
	if (old_flags < 0)
		goto unwind;
	if (fcntl(fd, F_SETFD, old_flags | O_CLOEXEC) < 0)
		goto unwind;

	return fd;

unwind:
	errno_save = errno;
	unlink(template);
	close(fd);
	errno = errno_save;

	return -1;
#endif
}

/* Create open temporary file in safe way.  Please notice that the
 * file permissions are -rw------- by default. */
int xmkstemp(char **tmpname, const char *dir, const char *prefix)
{
	char *localtmp;
	const char *tmpenv;
	mode_t old_mode;
	int fd, rc;

	/* Some use cases must be capable of being moved atomically
	 * with rename(2), which is the reason why dir is here.  */
	tmpenv = dir ? dir : getenv("TMPDIR");
	if (!tmpenv)
		tmpenv = _PATH_TMP;

	rc = asprintf(&localtmp, "%s/%s.XXXXXX", tmpenv, prefix);
	if (rc < 0)
		return -1;

	old_mode = umask(077);
	fd = mkstemp_cloexec(localtmp);
	umask(old_mode);
	if (fd == -1) {
		free(localtmp);
		localtmp = NULL;
	}
	*tmpname = localtmp;
	return fd;
}

int dup_fd_cloexec(int oldfd, int lowfd)
{
	int fd, flags, errno_save;

#ifdef F_DUPFD_CLOEXEC
	fd = fcntl(oldfd, F_DUPFD_CLOEXEC, lowfd);
	if (fd >= 0)
		return fd;
#endif

	fd = dup(oldfd);
	if (fd < 0)
		return fd;

	flags = fcntl(fd, F_GETFD);
	if (flags < 0)
		goto unwind;
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
		goto unwind;

	return fd;

unwind:
	errno_save = errno;
	close(fd);
	errno = errno_save;

	return -1;
}

/*
 * portable getdtablesize()
 */
int get_fd_tabsize(void)
{
	int m;

#if defined(HAVE_GETDTABLESIZE)
	m = getdtablesize();
#elif defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)
	struct rlimit rl;

	getrlimit(RLIMIT_NOFILE, &rl);
	m = rl.rlim_cur;
#elif defined(HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
	m = sysconf(_SC_OPEN_MAX);
#else
	m = OPEN_MAX;
#endif
	return m;
}

int mkdir_p(const char *path, mode_t mode)
{
	char *p, *dir;
	int rc = 0;

	if (!path || !*path)
		return -EINVAL;

	dir = p = strdup(path);
	if (!dir)
		return -ENOMEM;

	if (*p == '/')
		p++;

	while (p && *p) {
		char *e = strchr(p, '/');
		if (e)
			*e = '\0';
		if (*p) {
			rc = mkdir(dir, mode);
			if (rc && errno != EEXIST)
				break;
			rc = 0;
		}
		if (!e)
			break;
		*e = '/';
		p = e + 1;
	}

	free(dir);
	return rc;
}

/* returns basename and keeps dirname in the @path, if @path is "/" (root)
 * then returns empty string */
char *stripoff_last_component(char *path)
{
	char *p = path ? strrchr(path, '/') : NULL;

	if (!p)
		return NULL;
	*p = '\0';
	return p + 1;
}

/* Composes the /proc/self/fd/<fd> pathname for @fd. The @bufsz has to be at
 * least UL_FDPATH_BUFSIZ bytes.
 *
 * This is the only place where the /proc/self/fd/ pathnames are generated.
 *
 * Returns @buf, or NULL on error.
 */
char *ul_fd_mkpath(char *buf, size_t bufsz, int fd)
{
	int len;

	if (fd < 0) {
		errno = EBADF;
		return NULL;
	}

	len = snprintf(buf, bufsz, _PATH_PROC_FDDIR "/%d", fd);
	if (len < 0 || (size_t) len >= bufsz) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	return buf;
}

/* Returns the pathname the @fd refers to as used by the kernel, or NULL on
 * error. The result has to be deallocated by free().
 */
char *ul_fd_get_path(int fd)
{
	ssize_t ssz;
	char buf[PATH_MAX];
	char fdpath[UL_FDPATH_BUFSIZ];

	if (!ul_fd_mkpath(fdpath, sizeof(fdpath), fd))
		return NULL;

	ssz = readlink(fdpath, buf, sizeof(buf));
	if (ssz < 0)
		return NULL;

	/* readlink() does not terminate the result and it does not report
	 * truncation; a name we cannot read completely is unusable */
	if ((size_t) ssz >= sizeof(buf)) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	buf[ssz] = '\0';

	/* readlink() also succeeds for things without a pathname (pipes,
	 * sockets, ...) and it returns "<path> (deleted)" for unlinked files;
	 * refuse all of it rather than return a bogus path.
	 *
	 * Note that this also refuses a real file named "foo (deleted)". To
	 * tell it apart we would have to stat() the name, and such a path
	 * lookup may trigger an automount or block on an unreachable NFS
	 * server. */
	if (*buf != '/' || endswith(buf, PATH_DELETED_SUFFIX)) {
		errno = ENOENT;
		return NULL;
	}

	return strdup(buf);
}

#if defined(SYS_openat2) && defined(HAVE_LINUX_OPENAT2_H)
int ul_openat_resolve(int dirfd, const char *path, int flags,
		      mode_t mode, unsigned long long resolve)
{
	struct open_how how = {
		.flags = (__u64) flags,
		.mode = (__u64) mode,
		.resolve = resolve,
	};

	return syscall(SYS_openat2, dirfd, path, &how, sizeof(how));
}
#else
int ul_openat_resolve(
		int dirfd __attribute__((__unused__)),
		const char *path __attribute__((__unused__)),
		int flags __attribute__((__unused__)),
		mode_t mode __attribute__((__unused__)),
		unsigned long long resolve __attribute__((__unused__)))
{
	errno = ENOSYS;
	return -1;
}
#endif

#ifdef __linux__
/*
 * Fallback for kernels without openat2() (Linux < 5.6).
 *
 * Open the path and then ask the kernel for the name of the result. If any
 * component of the path is a symbolic link then the name reported by the
 * kernel differs from the requested path and we refuse the file descriptor.
 * A concurrent rename is detected the same way. The name always belongs to
 * the file descriptor we return, so there is no time-of-check-to-time-of-use
 * window between the check and the use.
 *
 * Note that the symlink is detected after it has been followed rather than
 * refused during the path resolution. The path is opened with O_PATH to keep
 * this free of side effects (no device open, no blocking on a FIFO, ...) and
 * the caller's flags are applied by re-opening the verified file descriptor.
 *
 * Returns a file descriptor, or -1 and sets errno to ELOOP when a symlink has
 * been detected, or to ENOSYS when the check is not possible.
 */
static int open_no_symlinks_fallback(const char *path, int flags, mode_t mode)
{
	char *abspath = NULL, *kpath = NULL;
	struct stat st;
	int fd = -1, errsv;

	/* we cannot verify a file we have to create first */
	if (!path || (flags & O_CREAT)) {
		errno = ENOSYS;
		return -1;
	}

	if (is_relative_path(path)) {
		/* the kernel reports an absolute pathname; note that
		 * absolute_path() only prepends the CWD as returned by
		 * getcwd(), it resolves nothing */
		abspath = absolute_path(path);
		if (!abspath)
			return -1;
	}

	if (flags & O_PATH)
		fd = open(path, flags);
	else
		fd = open(path, O_PATH | O_CLOEXEC |
				(flags & (O_NOFOLLOW | O_DIRECTORY)));
	if (fd < 0)
		goto fail;

	/* O_PATH|O_NOFOLLOW returns a FD to the symlink itself */
	if (fstat(fd, &st) != 0)
		goto fail;
	if (S_ISLNK(st.st_mode)) {
		errno = ELOOP;
		goto fail;
	}

	kpath = ul_fd_get_path(fd);
	if (!kpath) {
		errno = ENOSYS;		/* no /proc, no verification */
		goto fail;
	}

	/* streq_paths() ignores duplicate and trailing slashes, but "." and
	 * ".." in the requested path are refused as a symlink */
	if (streq_paths(abspath ? abspath : path, kpath) != 1) {
		errno = ELOOP;
		goto fail;
	}

	if (!(flags & O_PATH)) {
		char fdpath[UL_FDPATH_BUFSIZ];
		int nfd = -1;

		/* apply the caller's flags; the /proc link refers to the
		 * verified file, the path is not resolved for the second time */
		if (ul_fd_mkpath(fdpath, sizeof(fdpath), fd))
			nfd = open(fdpath, flags, mode);
		if (nfd < 0)
			goto fail;
		close(fd);
		fd = nfd;
	}

	free(abspath);
	free(kpath);
	return fd;
fail:
	errsv = errno;
	free(abspath);
	free(kpath);
	if (fd >= 0)
		close(fd);
	errno = errsv;
	return -1;
}
#else /* !__linux__ */
/* O_PATH and the /proc/self/fd/ names are Linux specific */
static int open_no_symlinks_fallback(
			const char *path __attribute__((__unused__)),
			int flags __attribute__((__unused__)),
			mode_t mode __attribute__((__unused__)))
{
	errno = ENOSYS;
	return -1;
}
#endif /* __linux__ */

/* Opens @path with the guarantee that no component of the path is a symbolic
 * link, otherwise it fails with ELOOP.
 */
int ul_open_no_symlinks(const char *path, int flags, mode_t mode)
{
	int fd = ul_openat_resolve(AT_FDCWD, path, flags, mode,
				   RESOLVE_NO_SYMLINKS);

	/* openat2() is Linux 5.6+ */
	if (fd < 0 && errno == ENOSYS)
		fd = open_no_symlinks_fallback(path, flags, mode);

	return fd;
}

#ifdef TEST_PROGRAM_FILEUTILS
int main(int argc, char *argv[])
{
	if (argc < 2)
		errx(EXIT_FAILURE, "Usage %s --{mkstemp,open-no-symlinks}", argv[0]);

	if (strcmp(argv[1], "--mkstemp") == 0) {
		FILE *f;
		char *tmpname = NULL;

		f = xfmkstemp(&tmpname, NULL, "test");
		unlink(tmpname);
		free(tmpname);
		fclose(f);

	} else if (strcmp(argv[1], "--open-no-symlinks") == 0) {
#ifdef __linux__
		int flags = O_PATH | O_CLOEXEC;
#else
		int flags = O_RDONLY | O_CLOEXEC;
#endif
		int fallback = 0;
		char *name;
		int fd, i;

		if (argc < 3)
			errx(EXIT_FAILURE, "no path specified");

		for (i = 3; i < argc; i++) {
			/* the fallback is used on kernels without openat2()
			 * only, "--fallback" makes it testable anywhere */
			if (strcmp(argv[i], "--fallback") == 0)
				fallback = 1;
			else if (strcmp(argv[i], "--rdonly") == 0)
				flags = O_RDONLY | O_CLOEXEC;
		}

		if (fallback)
			fd = open_no_symlinks_fallback(argv[2], flags, 0);
		else
			fd = ul_open_no_symlinks(argv[2], flags, 0);
		if (fd < 0)
			err(EXIT_FAILURE, "%s", argv[2]);

		name = ul_fd_get_path(fd);
		printf("%s\n", name);
		free(name);
		close(fd);
	}
	return EXIT_SUCCESS;
}
#endif
