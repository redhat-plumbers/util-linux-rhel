/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: utils
 * @title: Utils
 * @short_description: misc utils.
 */
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "strutils.h"
#include "pathnames.h"
#include "mountP.h"
#include "canonicalize.h"
#include "env.h"

int endswith(const char *s, const char *sx)
{
	ssize_t off;

	assert(s);
	assert(sx);

	off = strlen(s);
	if (!off)
		return 0;
	off -= strlen(sx);
	if (off < 0)
		return 0;

        return !strcmp(s + off, sx);
}

int startswith(const char *s, const char *sx)
{
	size_t off;

	assert(s);
	assert(sx);

	off = strlen(sx);
	if (!off)
		return 0;

        return !strncmp(s, sx, off);
}

/* returns basename and keeps dirname in the @path, if @path is "/" (root)
 * then returns empty string */
static char *stripoff_last_component(char *path)
{
	char *p = path ? strrchr(path, '/') : NULL;

	if (!p)
		return NULL;
	*p = '\0';
	return ++p;
}

/**
 * mnt_fstype_is_pseudofs:
 * @type: filesystem name
 *
 * Returns: 1 for filesystems like proc, sysfs, ... or 0.
 */
int mnt_fstype_is_pseudofs(const char *type)
{
	if (!type)
		return 0;
	if (strcmp(type, "none")  == 0 ||
	    strcmp(type, "proc")  == 0 ||
	    strcmp(type, "tmpfs") == 0 ||
	    strcmp(type, "sysfs") == 0 ||
	    strcmp(type, "devpts") == 0||
	    strcmp(type, "cgroups") == 0 ||
	    strcmp(type, "devfs") == 0 ||
	    strcmp(type, "dlmfs") == 0 ||
	    strcmp(type, "cpuset") == 0 ||
	    strcmp(type, "securityfs") == 0 ||
	    strcmp(type, "rpc_pipefs") == 0 ||
	    strcmp(type, "fusectl") == 0 ||
	    strcmp(type, "binfmt_misc") == 0 ||
	    strcmp(type, "fuse.gvfs-fuse-daemon") == 0 ||
	    strcmp(type, "debugfs") == 0 ||
	    strcmp(type, "spufs") == 0)
		return 1;
	return 0;
}

/**
 * mnt_fstype_is_netfs:
 * @type: filesystem name
 *
 * Returns: 1 for filesystems like cifs, nfs, ... or 0.
 */
int mnt_fstype_is_netfs(const char *type)
{
	if (!type)
		return 0;
	if (strcmp(type, "cifs")   == 0 ||
	    strcmp(type, "smbfs")  == 0 ||
	    strncmp(type,"nfs", 3) == 0 ||
	    strcmp(type, "afs")    == 0 ||
	    strcmp(type, "ncpfs")  == 0 ||
	    strncmp(type,"9p", 2)  == 0)
		return 1;
	return 0;
}

/**
 * mnt_match_fstype:
 * @type: filesystem type
 * @pattern: filesystem name or comma delimitted list of names
 *
 * The @pattern list of filesystem can be prefixed with a global
 * "no" prefix to invert matching of the whole list. The "no" could
 * also used for individual items in the @pattern list. So,
 * "nofoo,bar" has the same meaning as "nofoo,nobar".
 *
 * "bar"  : "nofoo,bar"		-> False   (global "no" prefix)
 *
 * "bar"  : "foo,bar"		-> True
 *
 * "bar" : "foo,nobar"		-> False
 *
 * Returns: 1 if type is matching, else 0. This function also returns
 *          0 if @pattern is NULL and @type is non-NULL.
 */
int mnt_match_fstype(const char *type, const char *pattern)
{
	int no = 0;		/* negated types list */
	int len;
	const char *p;

	if (!pattern && !type)
		return 1;
	if (!pattern)
		return 0;

	if (!strncmp(pattern, "no", 2)) {
		no = 1;
		pattern += 2;
	}

	/* Does type occur in types, separated by commas? */
	len = strlen(type);
	p = pattern;
	while(1) {
		if (!strncmp(p, "no", 2) && !strncmp(p+2, type, len) &&
		    (p[len+2] == 0 || p[len+2] == ','))
			return 0;
		if (strncmp(p, type, len) == 0 && (p[len] == 0 || p[len] == ','))
			return !no;
		p = strchr(p,',');
		if (!p)
			break;
		p++;
	}
	return no;
}


/* Returns 1 if needle found or noneedle not found in haystack
 * Otherwise returns 0
 */
static int check_option(const char *haystack, size_t len,
			const char *needle, size_t needle_len)
{
	const char *p;
	int no = 0;

	if (needle_len >= 2 && !strncmp(needle, "no", 2)) {
		no = 1;
		needle += 2;
		needle_len -= 2;
	}

	for (p = haystack; p && p < haystack + len; p++) {
		char *sep = strchr(p, ',');
		size_t plen = sep ? sep - p : len - (p - haystack);

		if (plen == needle_len) {
			if (!strncmp(p, needle, plen))
				return !no;	/* foo or nofoo was found */
		}
		p += plen;
	}

	return no;  /* foo or nofoo was not found */
}

/**
 * mnt_match_options:
 * @optstr: options string
 * @pattern: comma delimitted list of options
 *
 * The "no" could used for individual items in the @options list. The "no"
 * prefix does not have a global meanning.
 *
 * Unlike fs type matching, nonetdev,user and nonetdev,nouser have
 * DIFFERENT meanings; each option is matched explicitly as specified.
 *
 * "xxx,yyy,zzz" : "nozzz"	-> False
 *
 * "xxx,yyy,zzz" : "xxx,noeee"	-> True
 *
 * Returns: 1 if pattern is matching, else 0. This function also returns 0
 *          if @pattern is NULL and @optstr is non-NULL.
 */
int mnt_match_options(const char *optstr, const char *pattern)
{
	const char *p;
	size_t len, optstr_len = 0;

	if (!pattern && !optstr)
		return 1;
	if (!pattern)
		return 0;

	len = strlen(pattern);
	if (optstr)
		optstr_len = strlen(optstr);

	for (p = pattern; p < pattern + len; p++) {
		char *sep = strchr(p, ',');
		size_t plen = sep ? sep - p : len - (p - pattern);

		if (!plen)
			continue; /* if two ',' appear in a row */

		if (!check_option(optstr, optstr_len, p, plen))
			return 0; /* any match failure means failure */

		p += plen;
	}

	/* no match failures in list means success */
	return 1;
}

static int try_write(const char *filename)
{
	int fd;

	if (!filename)
		return -EINVAL;

	fd = open(filename, O_RDWR|O_CREAT, S_IWUSR| \
					    S_IRUSR|S_IRGRP|S_IROTH);
	if (fd >= 0) {
		close(fd);
		return 0;
	}
	return -errno;
}

/**
 * mnt_has_regular_mtab:
 * @mtab: returns path to mtab
 * @writable: returns 1 if the file is writable
 *
 * If the file does not exist and @writable argument is not NULL then it will
 * try to create the file
 *
 * Returns: 1 if /etc/mtab is a reqular file, and 0 in case of error (check
 *          errno for more details).
 */
int mnt_has_regular_mtab(const char **mtab, int *writable)
{
	struct stat st;
	int rc;
	const char *filename = mtab && *mtab ? *mtab : mnt_get_mtab_path();

	if (writable)
		*writable = 0;
	if (mtab && !*mtab)
		*mtab = filename;

	DBG(UTILS, mnt_debug("mtab: %s", filename));

	rc = lstat(filename, &st);

	if (rc == 0) {
		/* file exist */
		if (S_ISREG(st.st_mode)) {
			if (writable)
				*writable = !try_write(filename);
			return 1;
		}
		goto done;
	}

	/* try to create the file */
	if (writable) {
		*writable = !try_write(filename);
		if (*writable)
			return 1;
	}

done:
	DBG(UTILS, mnt_debug("%s: irregular/non-writable", filename));
	return 0;
}

/**
 * mnt_get_fstab_path:
 *
 * Returns: path to /etc/fstab or $LIBMOUNT_FSTAB.
 */
const char *mnt_get_fstab_path(void)
{
	const char *p = safe_getenv("LIBMOUNT_FSTAB");
	return p ? : _PATH_MNTTAB;
}

/**
 * mnt_get_mtab_path:
 *
 * This function returns *default* location of the mtab file. The result does
 * not have to be writable. See also mnt_has_regular_mtab().
 *
 * Returns: path to /etc/mtab or $LIBMOUNT_MTAB.
 */
const char *mnt_get_mtab_path(void)
{
	const char *p = safe_getenv("LIBMOUNT_MTAB");
	return p ? : _PATH_MOUNTED;
}

