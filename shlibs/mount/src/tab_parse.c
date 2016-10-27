/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "nls.h"
#include "mountP.h"
#include "pathnames.h"

#define isoctal(a) (((a) & ~7) == '0')

void unmangle_to_buffer(const char *s, char *buf, size_t len)
{
	size_t sz = 0;

	if (!s)
		return;

	while(*s && sz < len - 1) {
		if (*s == '\\' && sz + 4 < len - 1 && isoctal(s[1]) &&
		    isoctal(s[2]) && isoctal(s[3])) {

			*buf++ = 64*(s[1] & 7) + 8*(s[2] & 7) + (s[3] & 7);
			s += 4;
			sz += 4;
		} else {
			*buf++ = *s++;
			sz++;
		}
	}
	*buf = '\0';
}

static inline void unmangle_string(char *s)
{
	unmangle_to_buffer(s, s, strlen(s) + 1);
}

static inline char *skip_spaces(char *s)
{
	assert(s);

	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static int next_number(char **s, int *num)
{
	char *end = NULL;

	assert(num);
	assert(s);

	*s = skip_spaces(*s);
	if (!**s)
		return -1;
	*num = strtol(*s, &end, 10);
	if (end == NULL || *s == end)
	       return -1;

	*s = end;

	/* valid end of number is space or terminator */
	if (*end == ' ' || *end == '\t' || *end == '\0')
		return 0;
	return -1;
}

/*
 * Parses one line from {fs,m}tab
 */
static int mnt_parse_tab_line(mnt_fs *fs, char *s)
{
	int rc, n = 0;
	char *src, *fstype, *optstr;

	rc = sscanf(s,	"%ms "	/* (1) source */
			"%ms "	/* (2) target */
			"%ms "	/* (3) FS type */
			"%ms "  /* (4) options */
			"%n",	/* byte count */
			&src,
			&fs->target,
			&fstype,
			&optstr,
			&n);

	if (rc == 4) {
		unmangle_string(src);
		unmangle_string(fs->target);
		unmangle_string(fstype);
		unmangle_string(optstr);

		rc = __mnt_fs_set_source_ptr(fs, src);
		if (!rc)
			rc = __mnt_fs_set_fstype_ptr(fs, fstype);
		if (!rc)
			rc = mnt_fs_set_options(fs, optstr);
		free(optstr);
	} else {
		DBG(TAB, mnt_debug("tab parse error: [sscanf rc=%d]: '%s'", rc, s));
		rc = -EINVAL;
	}

	if (rc)
		return rc;	/* error */

	fs->passno = fs->freq = 0;
	s = skip_spaces(s + n);
	if (*s) {
		if (next_number(&s, &fs->freq) != 0) {
			if (*s) {
				DBG(TAB, mnt_debug("tab parse error: [freq]"));
				rc = -EINVAL;
			}
		} else if (next_number(&s, &fs->passno) != 0 && *s) {
			DBG(TAB, mnt_debug("tab parse error: [passno]"));
			rc = -EINVAL;
		}
	}

	return rc;
}

/*
 * Parses one line from mountinfo file
 */
static int mnt_parse_mountinfo_line(mnt_fs *fs, char *s)
{
	int rc;
	unsigned int maj, min;
	char *fstype, *src;

	rc = sscanf(s,	"%u "		/* (1) id */
			"%u "		/* (2) parent */
			"%u:%u "	/* (3) maj:min */
			"%ms "		/* (4) mountroot */
			"%ms "		/* (5) target */
			"%ms"		/* (6) vfs options (fs-independent) */
			"%*[^-]"	/* (7) optional fields */
			"- "		/* (8) separator */
			"%ms "		/* (9) FS type */
			"%ms "		/* (10) source */
			"%ms",		/* (11) fs options (fs specific) */

			&fs->id,
			&fs->parent,
			&maj, &min,
			&fs->root,
			&fs->target,
			&fs->vfs_optstr,
			&fstype,
			&src,
			&fs->fs_optstr);

	if (rc == 10) {
		fs->devno = makedev(maj, min);

		unmangle_string(fs->root);
		unmangle_string(fs->target);
		unmangle_string(fs->vfs_optstr);
		unmangle_string(fstype);

		if (!strcmp(src, "none")) {
			free(src);
			src = NULL;
		} else
			unmangle_string(src);

		if (!strcmp(fs->fs_optstr, "none")) {
			free(fs->fs_optstr);
			fs->fs_optstr = NULL;
		} else
			unmangle_string(fs->fs_optstr);

		rc = __mnt_fs_set_fstype_ptr(fs, fstype);
		if (!rc)
			rc = __mnt_fs_set_source_ptr(fs, src);
	} else {
		DBG(TAB, mnt_debug(
			"mountinfo parse error [sscanf rc=%d]: '%s'", rc, s));
		rc = -EINVAL;
	}
	return rc;
}

/*
 * Returns {m,fs}tab or mountinfo file format (MNT_FMT_*)
 *
 * mountinfo: "<number> <number> ... "
 */
static int guess_tab_format(char *line)
{
	unsigned int a, b;

	if (sscanf(line, "%u %u", &a, &b) == 2)
		return MNT_FMT_MOUNTINFO;
	return MNT_FMT_FSTAB;
}

/*
 * Read and parse the next line from {fs,m}tab or mountinfo
 */
static int mnt_tab_parse_next(mnt_tab *tb, FILE *f, mnt_fs *fs,
				const char *filename, int *nlines)
{
	char buf[BUFSIZ];
	char *s;

	assert(tb);
	assert(f);
	assert(fs);

	/* read the next non-blank non-comment line */
	do {
		if (fgets(buf, sizeof(buf), f) == NULL)
			return -EINVAL;
		++*nlines;
		s = index (buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(TAB, mnt_debug_h(tb,
					"%s: no final newline",	filename));
				s = index (buf, '\0');
			} else {
				DBG(TAB, mnt_debug_h(tb,
					"%s:%d: missing newline at line",
					filename, *nlines));
				goto err;
			}
		}
		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = skip_spaces(buf);
	} while (*s == '\0' || *s == '#');

	if (tb->fmt == MNT_FMT_GUESS)
		tb->fmt = guess_tab_format(s);

	if (tb->fmt == MNT_FMT_FSTAB) {
		if (mnt_parse_tab_line(fs, s) != 0)
			goto err;

	} else if (tb->fmt == MNT_FMT_MOUNTINFO) {
		if (mnt_parse_mountinfo_line(fs, s) != 0)
			goto err;

	}

	/*DBG(TAB, mnt_fs_print_debug(fs, stderr));*/

	return 0;
err:
	DBG(TAB, mnt_debug_h(tb, "%s:%d: %s parse error", filename, *nlines,
				tb->fmt == MNT_FMT_MOUNTINFO ? "mountinfo" :
				tb->fmt == MNT_FMT_FSTAB ? "fstab" : "uknown"));

	/* by default all errors are recoverable, otherwise behavior depends on
	 * errcb() function. See mnt_tab_set_parser_errcb().
	 */
	return tb->errcb ? tb->errcb(tb, filename, *nlines) : 1;
}

/**
 * mnt_tab_parse_stream:
 * @tb: tab pointer
 * @f: file stream
 * @filename: filename used for debug and error messages
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_tab_parse_stream(mnt_tab *tb, FILE *f, const char *filename)
{
	int nlines = 0;
	int rc = -1;

	assert(tb);
	assert(f);
	assert(filename);

	DBG(TAB, mnt_debug_h(tb, "%s: start parsing", filename));

	while (!feof(f)) {
		mnt_fs *fs = mnt_new_fs();

		if (!fs)
			goto err;

		rc = mnt_tab_parse_next(tb, f, fs, filename, &nlines);
		if (!rc)
			rc = mnt_tab_add_fs(tb, fs);
		if (rc) {
			mnt_free_fs(fs);
			if (rc == 1)
				continue;	/* recoverable error */
			if (feof(f))
				break;
			goto err;		/* fatal error */
		}
	}

	DBG(TAB, mnt_debug_h(tb, "%s: stop parsing", filename));
	return 0;
err:
	DBG(TAB, mnt_debug_h(tb, "%s: parse error (rc=%d)", filename, rc));
	return rc;
}

/**
 * mnt_tab_parse_file:
 * @tb: tab pointer
 * @filename: file
 *
 * Parses whole table (e.g. /etc/mtab) and appends new records to the @tab.
 *
 * The libmount parser ignores broken (syntax error) lines, these lines are
 * reported to caller by errcb() function (see mnt_tab_set_parser_errcb()).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_tab_parse_file(mnt_tab *tb, const char *filename)
{
	FILE *f;
	int rc;

	assert(tb);
	assert(filename);

	if (!filename || !tb)
		return -EINVAL;

	f = fopen(filename, "r");
	if (f) {
		rc = mnt_tab_parse_stream(tb, f, filename);
		fclose(f);
	} else
		return -errno;

	return rc;
}

mnt_tab *__mnt_new_tab_from_file(const char *filename, int fmt)
{
	mnt_tab *tb;
	struct stat st;

	assert(filename);

	if (!filename)
		return NULL;
	if (stat(filename, &st))
		return NULL;
	tb = mnt_new_tab();
	if (tb) {
		tb->fmt = fmt;
		if (mnt_tab_parse_file(tb, filename) != 0) {
			mnt_free_tab(tb);
			tb = NULL;
		}
	}
	return tb;
}

/**
 * mnt_new_tab_from_file:
 * @filename: /etc/{m,fs}tab or /proc/self/mountinfo path
 *
 * Same as mnt_new_tab() + mnt_tab_parse_file(). Use this function for private
 * files only. This function does not allow to use error callback, so you
 * cannot provide any feedback to end-users about broken records in files (e.g.
 * fstab).
 *
 * Returns: newly allocated tab on success and NULL in case of error.
 */
mnt_tab *mnt_new_tab_from_file(const char *filename)
{
	return __mnt_new_tab_from_file(filename, MNT_FMT_GUESS);
}

/**
 * mnt_tab_set_parser_errcb:
 * @tb: pointer to table
 * @cb: pointer to callback function
 *
 * The error callback function is called by table parser (mnt_tab_parse_file())
 * in case of syntax error. The callback function could be used for errors
 * evaluation, libmount will continue/stop parsing according to callback return
 * codes:
 *
 *   <0  : fatal error (abort parsing)
 *    0	 : success (parsing continue)
 *   >0  : recoverable error (the line is ignored, parsing continue).
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_tab_set_parser_errcb(mnt_tab *tb,
		int (*cb)(mnt_tab *tb, const char *filename, int line))
{
	assert(tb);
	tb->errcb = cb;
	return 0;
}

/**
 * mnt_tab_parse_fstab:
 * @tb: table
 * @filename: overwrites default (/etc/fstab or $LIBMOUNT_FSTAB) or NULL
 *
 * This function parses /etc/fstab or /etc/fstab.d and appends new lines to the
 * @tab. If the system contains classic fstab file and also fstab.d directory
 * then the fstab file is parsed before the fstab.d directory.
 *
 * The fstab.d directory:
 *	- files are sorted by strverscmp(3)
 *	- files that starts with "." are ignored (e.g. ".10foo.fstab")
 *	- files without the ".fstab" extension are ignored
 *
 * See also mnt_tab_set_parser_errcb().
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_tab_parse_fstab(mnt_tab *tb, const char *filename)
{
	FILE *f;

	assert(tb);

	if (!tb)
		return -EINVAL;
	if (!filename)
		filename = mnt_get_fstab_path();

	tb->fmt = MNT_FMT_FSTAB;

	f = fopen(filename, "r");
	if (f) {
		int rc = mnt_tab_parse_stream(tb, f, filename);
		fclose(f);

		if (rc)
			return rc;

		if (strcmp(filename, _PATH_MNTTAB))
			/* /etc/fstab.d sould be used together with /etc/fstab only */
			return 0;
	}

	return 0;
}

/**
 * mnt_tab_parse_mtab:
 * @tb: table
 * @filename: overwrites default (/etc/mtab or $LIBMOUNT_MTAB) or NULL
 *
 * This function parses /etc/mtab or /proc/self/mountinfo or
 * /proc/mounts.
 *
 * See also mnt_tab_set_parser_errcb().
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_tab_parse_mtab(mnt_tab *tb, const char *filename)
{
	int rc;

	if (mnt_has_regular_mtab(&filename, NULL)) {

		DBG(TAB, mnt_debug_h(tb, "force %s usage", filename));

		rc = mnt_tab_parse_file(tb, filename);
		if (!rc)
			return 0;
		filename = NULL;	/* failed */
	}

	/*
	 * useless /etc/mtab
	 * -- read kernel information from /proc/self/mountinfo
	 */
	tb->fmt = MNT_FMT_MOUNTINFO;
	rc = mnt_tab_parse_file(tb, _PATH_PROC_MOUNTINFO);
	if (rc) {
		/* hmm, old kernel? ...try /proc/mounts */
		tb->fmt = MNT_FMT_MTAB;
		return mnt_tab_parse_file(tb, _PATH_PROC_MOUNTS);
	}

	return 0;
}
