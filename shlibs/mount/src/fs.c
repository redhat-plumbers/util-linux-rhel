/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: fs
 * @title: Filesystem
 * @short_description: mnt_fs represents one entry in fstab/mtab/mountinfo
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <blkid.h>
#include <stddef.h>

#include "nls.h"
#include "mountP.h"
#include "strutils.h"

/**
 * mnt_new_fs:
 *
 * Returns: newly allocated mnt_file fs.
 */
mnt_fs *mnt_new_fs(void)
{
	mnt_fs *fs = calloc(1, sizeof(struct _mnt_fs));
	if (!fs)
		return NULL;

	INIT_LIST_HEAD(&fs->ents);
	return fs;
}

/**
 * mnt_free_fs:
 * @fs: fs pointer
 *
 * Deallocates the fs.
 */
void mnt_free_fs(mnt_fs *fs)
{
	if (!fs)
		return;
	list_del(&fs->ents);

	free(fs->source);
	free(fs->bindsrc);
	free(fs->tagname);
	free(fs->tagval);
	free(fs->root);
	free(fs->target);
	free(fs->fstype);
	free(fs->vfs_optstr);
	free(fs->fs_optstr);
	free(fs->user_optstr);
	free(fs->attrs);

	free(fs);
}

/**
 * mnt_fs_get_userdata:
 * @fs: mnt_file instance
 *
 * Returns: private data set by mnt_fs_set_userdata() or NULL.
 */
void *mnt_fs_get_userdata(mnt_fs *fs)
{
	return fs ? fs->userdata : NULL;
}

/**
 * mnt_fs_set_userdata:
 * @fs: mnt_file instance
 * @data: user data
 *
 * The "userdata" are library independent data.
 *
 * Returns: 0 or negative number in case of error (if @fs is NULL).
 */
int mnt_fs_set_userdata(mnt_fs *fs, void *data)
{
	if (!fs)
		return -EINVAL;
	fs->userdata = data;
	return 0;
}

/**
 * mnt_fs_get_srcpath:
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * The mount "source path" is:
 * - a directory for 'bind' mounts (in fstab or mtab only)
 * - a device name for standard mounts
 *
 * See also mnt_fs_get_tag() and mnt_fs_get_source().
 *
 * Returns: mount source path or NULL in case of error or when the path
 * is not defined.
 */
const char *mnt_fs_get_srcpath(mnt_fs *fs)
{
	assert(fs);
	if (!fs)
		return NULL;

	/* fstab-like fs */
	if (fs->tagname)
		return NULL;	/* the source contains a "NAME=value" */
	return fs->source;
}

/**
 * mnt_fs_get_source:
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * Returns: mount source. Note that the source could be unparsed TAG
 * (LABEL/UUID). See also mnt_fs_get_srcpath() and mnt_fs_get_tag().
 */
const char *mnt_fs_get_source(mnt_fs *fs)
{
	return fs ? fs->source : NULL;
}

/* Used by parser mnt_file ONLY (@source has to be allocated) */
int __mnt_fs_set_source_ptr(mnt_fs *fs, char *source)
{
	char *t = NULL, *v = NULL;

	assert(fs);

	if (source && !strcmp(source, "none"))
		source = NULL;

	if (source && strchr(source, '=')) {
		if (blkid_parse_tag_string(source, &t, &v) != 0)
			return -1;
	}

	if (fs->source != source)
		free(fs->source);

	free(fs->tagname);
	free(fs->tagval);

	fs->source = source;
	fs->tagname = t;
	fs->tagval = v;
	return 0;
}

/**
 * mnt_fs_set_source:
 * @fs: fstab/mtab/mountinfo entry
 * @source: new source
 *
 * This function creates a private copy (strdup()) of @source.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_source(mnt_fs *fs, const char *source)
{
	char *p;
	int rc;

	if (!fs && !source)
		return -EINVAL;
	p = strdup(source);
	if (!p)
		return -ENOMEM;

	rc = __mnt_fs_set_source_ptr(fs, p);
	if (rc)
		free(p);
	return rc;
}

/*
 * mnt_fs_streq_target:
 * @fs: fs
 * @path: mount point
 *
 * Compares @fs target path with @path. The trailing slash is ignored.
 * See also mnt_fs_match_target().
 *
 * Returns: 1 if @fs target path equal to @path, otherwise 0.
 */
int mnt_fs_streq_target(mnt_fs *fs, const char *path)
{
	assert(fs);
	return fs && streq_except_trailing_slash(mnt_fs_get_target(fs), path);
}

/*
 * mnt_fs_streq_srcpath:
 * @fs: fs
 * @path: source path
 *
 * Compares @fs source path with @path. The trailing slash is ignored.
 * See also mnt_fs_match_source().
 *
 * Returns: 1 if @fs source path equal to @path, otherwise 0.
 */
int mnt_fs_streq_srcpath(mnt_fs *fs, const char *path)
{
	const char *p;

	assert(fs);
	if (!fs)
		return 0;

	p = mnt_fs_get_srcpath(fs);

	if (!(fs->flags & MNT_FS_PSEUDO))
		return streq_except_trailing_slash(p, path);

	if (!p && !path)
		return 1;

	return p && path && strcmp(p, path) == 0;
}

/**
 * mnt_fs_get_tag:
 * @fs: fs
 * @name: returns pointer to NAME string
 * @value: returns pointer to VALUE string
 *
 * "TAG" is NAME=VALUE (e.g. LABEL=foo)
 *
 * The TAG is the first column in the fstab file. The TAG or "srcpath" has to
 * be always set for all entries.
 *
 * See also mnt_fs_get_source().
 *
 * <informalexample>
 *   <programlisting>
 *	char *src;
 *	mnt_fs *fs = mnt_tab_find_target(tb, "/home", MNT_ITER_FORWARD);
 *
 *	if (!fs)
 *		goto err;
 *
 *	src = mnt_fs_get_srcpath(fs);
 *	if (!src) {
 *		char *tag, *val;
 *		if (mnt_fs_get_tag(fs, &tag, &val) == 0)
 *			printf("%s: %s\n", tag, val);	// LABEL or UUID
 *	} else
 *		printf("device: %s\n", src);		// device or bind path
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success or negative number in case that a TAG is not defined.
 */
int mnt_fs_get_tag(mnt_fs *fs, const char **name, const char **value)
{
	if (fs == NULL || !fs->tagname)
		return -EINVAL;
	if (name)
		*name = fs->tagname;
	if (value)
		*value = fs->tagval;
	return 0;
}

/**
 * mnt_fs_get_target:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to mountpoint path or NULL
 */
const char *mnt_fs_get_target(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->target : NULL;
}

/**
 * mnt_fs_set_target:
 * @fs: fstab/mtab/mountinfo entry
 * @target: mountpoint
 *
 * This function creates a private copy (strdup()) of @target.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_target(mnt_fs *fs, const char *target)
{
	char *p;

	assert(fs);

	if (!fs || !target)
		return -EINVAL;
	p = strdup(target);
	if (!p)
		return -ENOMEM;
	free(fs->target);
	fs->target = p;

	return 0;
}

/**
 * mnt_fs_get_fstype:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to filesystem type.
 */
const char *mnt_fs_get_fstype(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->fstype : NULL;
}

/* Used by mnt_file parser only */
int __mnt_fs_set_fstype_ptr(mnt_fs *fs, char *fstype)
{
	assert(fs);

	if (fstype != fs->fstype)
		free(fs->fstype);

	fs->fstype = fstype;
	fs->flags &= ~MNT_FS_PSEUDO;
	fs->flags &= ~MNT_FS_NET;

	/* save info about pseudo filesystems */
	if (fs->fstype) {
		if (mnt_fstype_is_pseudofs(fs->fstype))
			fs->flags |= MNT_FS_PSEUDO;
		else if (mnt_fstype_is_netfs(fs->fstype))
			fs->flags |= MNT_FS_NET;
		else if (!strcmp(fs->fstype, "swap"))
			fs->flags |= MNT_FS_SWAP;
	}
	return 0;
}

/**
 * mnt_fs_set_fstype:
 * @fs: fstab/mtab/mountinfo entry
 * @fstype: filesystem type
 *
 * This function creates a private copy (strdup()) of @fstype.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_fstype(mnt_fs *fs, const char *fstype)
{
	char *p = NULL;
	int rc;

	if (!fs)
		return -EINVAL;
	if (fstype) {
		p = strdup(fstype);
		if (!p)
			return -ENOMEM;
	}
	rc =  __mnt_fs_set_fstype_ptr(fs, p);
	if (rc)
		free(p);
	return rc;
}

/*
 * Merges @vfs and @fs options strings into a new string.
 * This function cares about 'ro/rw' options. The 'ro' is
 * always used if @vfs or @fs is read-only.
 * For example:
 *
 *    mnt_merge_optstr("rw,noexec", "ro,journal=update")
 *
 *           returns: "ro,noexec,journal=update"
 *
 *    mnt_merge_optstr("rw,noexec", "rw,journal=update")
 *
 *           returns: "rw,noexec,journal=update"
 */
static char *merge_optstr(const char *vfs, const char *fs)
{
	char *res, *p;
	size_t sz;
	int ro = 0, rw = 0;

	if (!vfs && !fs)
		return NULL;
	if (!vfs || !fs)
		return strdup(fs ? fs : vfs);
	if (!strcmp(vfs, fs))
		return strdup(vfs);		/* e.g. "aaa" and "aaa" */

	/* leave space for leading "r[ow],", "," and trailing zero */
	sz = strlen(vfs) + strlen(fs) + 5;
	res = malloc(sz);
	if (!res)
		return NULL;
	p = res + 3;			/* make a room for rw/ro flag */

	snprintf(p, sz - 3, "%s,%s", vfs, fs);

	/* remove 'rw' flags */
	rw += !mnt_optstr_remove_option(&p, "rw");	/* from vfs */
	rw += !mnt_optstr_remove_option(&p, "rw");	/* from fs */

	/* remove 'ro' flags if necessary */
	if (rw != 2) {
		ro += !mnt_optstr_remove_option(&p, "ro");
		if (ro + rw < 2)
			ro += !mnt_optstr_remove_option(&p, "ro");
	}

	if (!strlen(p))
		memcpy(res, ro ? "ro" : "rw", 3);
	else
		memcpy(res, ro ? "ro," : "rw,", 3);
	return res;
}

/**
 * mnt_fs_strdup_options:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Merges all mount options (VFS, FS and userspace) to the one options string
 * and returns the result. This function does not modigy @fs.
 *
 * Returns: pointer to string (can be freed by free(3)) or NULL in case of error.
 */
char *mnt_fs_strdup_options(mnt_fs *fs)
{
	char *res;

	assert(fs);

	errno = 0;
	res = merge_optstr(fs->vfs_optstr, fs->fs_optstr);
	if (!res && errno)
		return NULL;
	if (fs->user_optstr) {
		if (mnt_optstr_append_option(&res, fs->user_optstr, NULL)) {
			free(res);
			res = NULL;
		}
	}
	return res;
}

/**
 * mnt_fs_set_options:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @optstr: options string
 *
 * Splits @optstr to VFS, FS and userspace mount options and update relevat
 * parts of @fs.
 *
 * Returns: 0 on success, or negative number icase of error.
 */
int mnt_fs_set_options(mnt_fs *fs, const char *optstr)
{
	char *v = NULL, *f = NULL, *u = NULL;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (optstr) {
		int rc = mnt_split_optstr(optstr, &u, &v, &f, 0, 0);
		if (rc)
			return rc;
	}

	free(fs->fs_optstr);
	free(fs->vfs_optstr);
	free(fs->user_optstr);

	fs->fs_optstr = f;
	fs->vfs_optstr = v;
	fs->user_optstr = u;
	return 0;
}

/**
 * mnt_fs_append_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: mount options
 *
 * Parses (splits) @optstr and appends results to VFS, FS and userspace lists
 * of options.
 *
 * If @optstr is NULL then @fs is not modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_options(mnt_fs *fs, const char *optstr)
{
	char *v = NULL, *f = NULL, *u = NULL;
	int rc;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;

	rc = mnt_split_optstr((char *) optstr, &u, &v, &f, 0, 0);
	if (!rc && v)
		rc = mnt_optstr_append_option(&fs->vfs_optstr, v, NULL);
	if (!rc && f)
	       rc = mnt_optstr_append_option(&fs->fs_optstr, f, NULL);
	if (!rc && u)
	       rc = mnt_optstr_append_option(&fs->user_optstr, u, NULL);

	free(u);
	free(v);
	free(f);

	return rc;
}

/**
 * mnt_fs_prepend_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: mount options
 *
 * Parses (splits) @optstr and prepands results to VFS, FS and userspace lists
 * of options.
 *
 * If @optstr is NULL then @fs is not modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_options(mnt_fs *fs, const char *optstr)
{
	char *v = NULL, *f = NULL, *u = NULL;
	int rc;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;

	rc = mnt_split_optstr((char *) optstr, &u, &v, &f, 0, 0);
	if (!rc && v)
		rc = mnt_optstr_prepend_option(&fs->vfs_optstr, v, NULL);
	if (!rc && f)
		rc = mnt_optstr_prepend_option(&fs->fs_optstr, f, NULL);
	if (!rc && u)
		rc = mnt_optstr_prepend_option(&fs->user_optstr, u, NULL);

	free(u);
	free(v);
	free(f);

	return rc;
}

/*
 * mnt_fs_get_fs_options:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to superblock (fs-depend) mount option string or NULL.
 */
const char *mnt_fs_get_fs_options(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->fs_optstr : NULL;
}

/**
 * mnt_fs_set_fs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Sets FS specific mount options.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_fs_options(mnt_fs *fs, const char *optstr)
{
	char *p = NULL;

	if (!fs)
		return -EINVAL;
	if (optstr) {
		p = strdup(optstr);
		if (!p)
			return -ENOMEM;
	}
	free(fs->fs_optstr);
	fs->fs_optstr = p;

	return 0;
}

/**
 * mnt_fs_append_fs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Appends FS specific mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_fs_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_append_option(&fs->fs_optstr, optstr, NULL);
}

/**
 * mnt_fs_prepend_fs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Prepends FS specific mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_fs_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_prepend_option(&fs->fs_optstr, optstr, NULL);
}


/**
 * mnt_fs_get_vfs_options:
 * @fs: fstab/mtab entry pointer
 *
 * Returns: pointer to fs-independent (VFS) mount option string or NULL.
 */
const char *mnt_fs_get_vfs_options(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->vfs_optstr : NULL;
}

/**
 * mnt_fs_set_vfs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Sets VFS mount options.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_vfs_options(mnt_fs *fs, const char *optstr)
{
	char *p = NULL;

	if (!fs)
		return -EINVAL;
	if (optstr) {
		p = strdup(optstr);
		if (!p)
			return -ENOMEM;
	}
	free(fs->vfs_optstr);
	fs->vfs_optstr = p;

	return 0;
}

/**
 * mnt_fs_append_vfs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Appends VFS mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_vfs_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_append_option(&fs->vfs_optstr, optstr, NULL);
}

/**
 * mnt_fs_prepend_vfs_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Prepends VFS mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_vfs_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_prepend_option(&fs->vfs_optstr, optstr, NULL);
}

/**
 * mnt_fs_get_userspace_options:
 * @fs: fstab/mtab entry pointer
 *
 * Returns: pointer to userspace mount option string or NULL.
 */
const char *mnt_fs_get_userspace_options(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->user_optstr : NULL;
}

/**
 * mnt_fs_set_userspace_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Sets userspace mount options.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_userspace_options(mnt_fs *fs, const char *optstr)
{
	char *p = NULL;

	if (!fs)
		return -EINVAL;
	if (optstr) {
		p = strdup(optstr);
		if (!p)
			return -ENOMEM;
	}
	free(fs->user_optstr);
	fs->user_optstr = p;

	return 0;
}

/**
 * mnt_fs_append_userspace_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Appends userspace mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_userspace_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_append_option(&fs->user_optstr, optstr, NULL);
}

/**
 * mnt_fs_prepend_userspace_options:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Prepends userspace mount options. If @optstr is NULL then @fs is not
 * modified and 0 is returned.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_userspace_options(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;

	return mnt_optstr_prepend_option(&fs->user_optstr, optstr, NULL);
}

/**
 * mnt_fs_get_attributes:
 * @fs: fstab/mtab entry pointer
 *
 * Returns: pointer to attributes string or NULL.
 */
const char *mnt_fs_get_attributes(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->attrs : NULL;
}

/**
 * mnt_fs_set_attributes:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Sets mount attributes. The attributes are mount(2) and mount(8) independent
 * options, these options are not send to kernel and are not interpreted by
 * libmount. The attributes are stored in /dev/.mount/utab only.
 *
 * The atrtributes are managed by libmount in userspace only. It's possible
 * that information stored in userspace will not be available for libmount
 * after CLONE_FS unshare. Be carefull, and don't use attributes if possible.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_attributes(mnt_fs *fs, const char *optstr)
{
	char *p = NULL;

	if (!fs)
		return -EINVAL;
	if (optstr) {
		p = strdup(optstr);
		if (!p)
			return -ENOMEM;
	}
	free(fs->attrs);
	fs->attrs = p;

	return 0;
}

/**
 * mnt_fs_append_attributes
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Appends mount attributes. (See mnt_fs_set_attributes()).
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_attributes(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_append_option(&fs->attrs, optstr, NULL);
}

/**
 * mnt_fs_prepend_attributes
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * Prepends mount attributes. (See mnt_fs_set_attributes()).
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_attributes(mnt_fs *fs, const char *optstr)
{
	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;
	return mnt_optstr_prepend_option(&fs->attrs, optstr, NULL);
}


/**
 * mnt_fs_get_freq:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: dump frequency in days.
 */
int mnt_fs_get_freq(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->freq : 0;
}

/**
 * mnt_fs_set_freq:
 * @fs: fstab/mtab entry pointer
 * @freq: dump frequency in days
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_freq(mnt_fs *fs, int freq)
{
	assert(fs);
	if (!fs)
		return -EINVAL;
	fs->freq = freq;
	return 0;
}

/**
 * mnt_fs_get_passno:
 * @fs: fstab/mtab entry pointer
 *
 * Returns: "pass number on parallel fsck".
 */
int mnt_fs_get_passno(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->passno: 0;
}

/**
 * mnt_fs_set_passno:
 * @fs: fstab/mtab entry pointer
 * @passno: pass number
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_passno(mnt_fs *fs, int passno)
{
	assert(fs);
	if (!fs)
		return -EINVAL;
	fs->passno = passno;
	return 0;
}

/**
 * mnt_fs_get_root:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: root of the mount within the filesystem or NULL
 */
const char *mnt_fs_get_root(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->root : NULL;
}

/**
 * mnt_fs_set_root:
 * @fs: mountinfo entry
 * @root: path
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_root(mnt_fs *fs, const char *root)
{
	char *p = NULL;

	assert(fs);
	if (!fs)
		return -EINVAL;
	if (root) {
		p = strdup(root);
		if (!p)
			return -ENOMEM;
	}
	free(fs->root);
	fs->root = p;
	return 0;
}

/**
 * mnt_fs_get_bindsrc:
 * @fs: /dev/.mount/utab entry
 *
 * Returns: full path that was used for mount(2) on MS_BIND
 */
const char *mnt_fs_get_bindsrc(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->bindsrc : NULL;
}

/**
 * mnt_fs_set_bindsrc:
 * @fs: filesystem
 * @src: path
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_bindsrc(mnt_fs *fs, const char *src)
{
	char *p = NULL;

	assert(fs);
	if (!fs)
		return -EINVAL;
	if (src) {
		p = strdup(src);
		if (!p)
			return -ENOMEM;
	}
	free(fs->bindsrc);
	fs->bindsrc = p;
	return 0;
}

/**
 * mnt_fs_get_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: mount ID (unique identifier of the mount) or negative number in case of error.
 */
int mnt_fs_get_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->id : -EINVAL;
}

/**
 * mnt_fs_get_parent_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: parent mount ID or negative number in case of error.
 */
int mnt_fs_get_parent_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->parent : -EINVAL;
}

/**
 * mnt_fs_get_devno:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: value of st_dev for files on filesystem or 0 in case of error.
 */
dev_t mnt_fs_get_devno(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->devno : 0;
}

/**
 * mnt_fs_get_option:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @name: option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of options value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case of error.
 */
int mnt_fs_get_option(mnt_fs *fs, const char *name,
		char **value, size_t *valsz)
{
	char rc = 1;

	if (fs->fs_optstr)
		rc = mnt_optstr_get_option(fs->fs_optstr, name, value, valsz);
	if (rc == 1 && fs->vfs_optstr)
		rc = mnt_optstr_get_option(fs->vfs_optstr, name, value, valsz);
	if (rc == 1 && fs->user_optstr)
		rc = mnt_optstr_get_option(fs->user_optstr, name, value, valsz);
	return rc;
}

/**
 * mnt_fs_get_attribute:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @name: option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of options value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case of error.
 */
int mnt_fs_get_attribute(mnt_fs *fs, const char *name,
		char **value, size_t *valsz)
{
	char rc = 1;

	if (fs->attrs)
		rc = mnt_optstr_get_option(fs->attrs, name, value, valsz);
	return rc;
}

/**
 * mnt_fs_match_target:
 * @fs: filesystem
 * @target: mountpoint path
 * @cache: tags/paths cache or NULL
 *
 * Possible are three attempts:
 *	1) compare @target with @fs->target
 *	2) realpath(@target) with @fs->target
 *	3) realpath(@target) with realpath(@fs->target).
 *
 * The 2nd and 3rd attempts are not performed when @cache is NULL.
 *
 * Returns: 1 if @fs target is equal to @target else 0.
 */
int mnt_fs_match_target(mnt_fs *fs, const char *target, mnt_cache *cache)
{
	int rc = 0;

	if (!fs || !target || !fs->target)
		return 0;

	/* 1) native paths */
	rc = mnt_fs_streq_target(fs, target);

	if (!rc && cache) {
		/* 2) - canonicalized and non-canonicalized */
		char *cn = mnt_resolve_path(target, cache);
		rc = (cn && strcmp(cn, fs->target) == 0);

		/* 3) - canonicalized and canonicalized */
		if (!rc && cn) {
			char *tcn = mnt_resolve_path(fs->target, cache);
			rc = (tcn && strcmp(cn, tcn) == 0);
		}
	}

	return rc;
}

/**
 * mnt_fs_match_source:
 * @fs: filesystem
 * @source: tag or path (device or so)
 * @cache: tags/paths cache or NULL
 *
 * Possible are four attempts:
 *	1) compare @source with @fs->source
 *	2) compare realpath(@source) with @fs->source
 *	3) compare realpath(@source) with realpath(@fs->source)
 *	4) compare realpath(@source) with evaluated tag from @fs->source
 *
 * The 2nd, 3rd and 4th attempts are not performed when @cache is NULL. The
 * 2nd and 3rd attempts are not performed if @fs->source is tag.
 *
 * Returns: 1 if @fs source is equal to @source else 0.
 */
int mnt_fs_match_source(mnt_fs *fs, const char *source, mnt_cache *cache)
{
	char *cn;
	const char *src, *t, *v;

	if (!fs || !source || !fs->source)
		return 0;

	/* 1) native paths/tags */
	if (mnt_fs_streq_srcpath(fs, source))
		return 1;

	if (!cache)
		return 0;
	if (fs->flags & (MNT_FS_NET | MNT_FS_PSEUDO))
		return 0;

	cn = mnt_resolve_spec(source, cache);
	if (!cn)
		return 0;

	/* 2) canonicalized and native */
	src = mnt_fs_get_srcpath(fs);
	if (src && mnt_fs_streq_srcpath(fs, cn))
		return 1;

	/* 3) canonicalized and canonicalized */
	if (src) {
		src = mnt_resolve_path(src, cache);
		if (src && !strcmp(cn, src))
			return 1;
	}
	if (src || mnt_fs_get_tag(fs, &t, &v))
		/* src path does not match and tag is not defined */
		return 0;

	/* read @source's tags to the cache */
	if (mnt_cache_read_tags(cache, cn) < 0) {
		if (errno == EACCES) {
			/* we don't have permissions to read TAGs from
			 * @source, but can translate @fs tag to devname.
			 *
			 * (because libblkid uses udev symlinks and this is
			 * accessible for non-root uses)
			 */
			char *x = mnt_resolve_tag(t, v, cache);
			if (x && !strcmp(x, cn))
				return 1;
		}
		return 0;
	}

	/* 4) has the @source a tag that matches with tag from @fs ? */
	if (mnt_cache_device_has_tag(cache, cn, t, v))
		return 1;

	return 0;
}

/**
 * mnt_fs_match_fstype:
 * @fs: filesystem
 * @types: filesystem name or comma delimited list of filesystems
 *
 * For more details see mnt_match_fstype().
 *
 * Returns: 1 if @fs type is matching to @types else 0. The function returns
 * 0 when types is NULL.
 */
int mnt_fs_match_fstype(mnt_fs *fs, const char *types)
{
	return mnt_match_fstype(fs->fstype, types);
}

/**
 * mnt_fs_match_options:
 * @fs: filesystem
 * @options: comma delimited list of options (and nooptions)
 *
 * For more details see mnt_match_options().
 *
 * Returns: 1 if @fs type is matching to @options else 0. The function returns
 * 0 when types is NULL.
 */
int mnt_fs_match_options(mnt_fs *fs, const char *options)
{
	char *o = mnt_fs_strdup_options(fs);
	int rc = 0;

	if (o)
		rc = mnt_match_options(o, options);
	free(o);
	return rc;
}

/**
 * mnt_fs_print_debug
 * @fs: fstab/mtab/mountinfo entry
 * @file: output
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_print_debug(mnt_fs *fs, FILE *file)
{
	if (!fs)
		return -EINVAL;
	fprintf(file, "------ fs: %p\n", fs);
	fprintf(file, "source: %s\n", mnt_fs_get_source(fs));
	fprintf(file, "target: %s\n", mnt_fs_get_target(fs));
	fprintf(file, "fstype: %s\n", mnt_fs_get_fstype(fs));

	if (mnt_fs_get_vfs_options(fs))
		fprintf(file, "VFS-optstr: %s\n", mnt_fs_get_vfs_options(fs));
	if (mnt_fs_get_fs_options(fs))
		fprintf(file, "FS-opstr: %s\n", mnt_fs_get_fs_options(fs));
	if (mnt_fs_get_userspace_options(fs))
		fprintf(file, "user-optstr: %s\n", mnt_fs_get_userspace_options(fs));
	if (mnt_fs_get_attributes(fs))
		fprintf(file, "attributes: %s\n", mnt_fs_get_attributes(fs));

	if (mnt_fs_get_root(fs))
		fprintf(file, "root:   %s\n", mnt_fs_get_root(fs));
	if (mnt_fs_get_bindsrc(fs))
		fprintf(file, "bindsrc: %s\n", mnt_fs_get_bindsrc(fs));
	if (mnt_fs_get_freq(fs))
		fprintf(file, "freq:   %d\n", mnt_fs_get_freq(fs));
	if (mnt_fs_get_passno(fs))
		fprintf(file, "pass:   %d\n", mnt_fs_get_passno(fs));
	if (mnt_fs_get_id(fs))
		fprintf(file, "id:     %d\n", mnt_fs_get_id(fs));
	if (mnt_fs_get_parent_id(fs))
		fprintf(file, "parent: %d\n", mnt_fs_get_parent_id(fs));
	if (mnt_fs_get_devno(fs))
		fprintf(file, "devno:  %d:%d\n", major(mnt_fs_get_devno(fs)),
						 minor(mnt_fs_get_devno(fs)));
	return 0;
}

/**
 * mnt_free_mntent:
 * @mnt: mount entry
 *
 * Deallocates "mntent.h" mount entry.
 */
void mnt_free_mntent(struct mntent *mnt)
{
	if (mnt) {
		free(mnt->mnt_fsname);
		free(mnt->mnt_dir);
		free(mnt->mnt_type);
		free(mnt->mnt_opts);
		free(mnt);
	}
}

