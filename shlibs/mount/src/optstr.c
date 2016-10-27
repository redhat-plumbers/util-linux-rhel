/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: optstr
 * @title: Options string
 * @short_description: low-level API for work with mount options
 *
 * This is simple and low-level API to work with mount options that are stored
 * in string. This API is independent on the high-level options container and
 * option maps.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include "nls.h"
#include "mountP.h"

/*
 * Option location
 */
struct mnt_optloc {
	char	*begin;
	char	*end;
	char	*value;
	size_t	valsz;
	size_t  namesz;
};

#define mnt_init_optloc(_ol)	(memset((_ol), 0, sizeof(struct mnt_optloc)))

/*
 * Parses the first option from @optstr. The @optstr pointer is set to begin of
 * the next option.
 *
 * Returns -EINVAL on parse error, 1 at the end of optstr and 0 on success.
 */
static int mnt_optstr_parse_next(char **optstr,	 char **name, size_t *namesz,
					char **value, size_t *valsz)
{
	int open_quote = 0;
	char *start = NULL, *stop = NULL, *p, *sep = NULL;
	char *optstr0;

	assert(optstr);
	assert(*optstr);

	optstr0 = *optstr;

	if (name)
		*name = NULL;
	if (namesz)
		*namesz = 0;
	if (value)
		*value = NULL;
	if (valsz)
		*valsz = 0;

	for (p = optstr0; p && *p; p++) {
		if (!start)
			start = p;		/* begin of the option item */
		if (*p == '"')
			open_quote ^= 1;	/* reverse the status */
		if (open_quote)
			continue;		/* still in quoted block */
		if (!sep && *p == '=')
			sep = p;		/* name and value separator */
		if (*p == ',')
			stop = p;		/* terminate the option item */
		else if (*(p + 1) == '\0')
			stop = p + 1;		/* end of optstr */
		if (!start || !stop)
			continue;
		if (stop <= start)
			goto error;

		if (name)
			*name = start;
		if (namesz)
			*namesz = sep ? sep - start : stop - start;
		*optstr = *stop ? stop + 1 : stop;

		if (sep) {
			if (value)
				*value = sep + 1;
			if (valsz)
				*valsz = stop - sep - 1;
		}
		return 0;
	}

	return 1;				/* end of optstr */

error:
	DBG(OPTIONS, mnt_debug("parse error: \"%s\"", optstr0));
	return -EINVAL;
}

/*
 * Locates the first option that match with @name. The @end is set to
 * char behind the option (it means ',' or \0).
 *
 * Returns negative number on parse error, 1 when not found and 0 on success.
 */
static int mnt_optstr_locate_option(char *optstr, const char *name,
					struct mnt_optloc *ol)
{
	char *n;
	size_t namesz, nsz;
	int rc;

	if (!optstr)
		return 1;

	assert(name);
	assert(optstr);

	namesz = strlen(name);

	do {
		rc = mnt_optstr_parse_next(&optstr, &n, &nsz,
					&ol->value, &ol->valsz);
		if (rc)
			break;

		if (namesz == nsz && strncmp(n, name, nsz) == 0) {
			ol->begin = n;
			ol->end = *(optstr - 1) == ',' ? optstr - 1 : optstr;
			ol->namesz = nsz;
			return 0;
		}
	} while(1);

	return rc;
}

/**
 * mnt_optstr_next_option:
 * @optstr: option string, returns position to next option
 * @name: returns option name
 * @namesz: returns option name length
 * @value: returns option value or NULL
 * @valuesz: returns option value length or zero
 *
 * Parses the first option in @optstr.
 *
 * Returns: 0 on success, 1 at the end of @optstr or negative number in case of
 * error.
 */
int mnt_optstr_next_option(char **optstr, char **name, size_t *namesz,
					char **value, size_t *valuesz)
{
	if (!optstr || !*optstr)
		return -EINVAL;
	return mnt_optstr_parse_next(optstr, name, namesz, value, valuesz);
}

static int __mnt_optstr_append_option(char **optstr,
			const char *name, size_t nsz,
			const char *value, size_t vsz)
{
	char *p;
	size_t sz, osz;

	assert(name);

	osz = *optstr ? strlen(*optstr) : 0;

	sz = osz + nsz + 1;		/* 1: '\0' */
	if (osz)
		sz++;			/* ',' options separator */
	if (vsz)
		sz += vsz + 1;		/* 1: '=' */

	p = realloc(*optstr, sz);
	if (!p)
		return -ENOMEM;
	*optstr = p;

	if (osz) {
		p += osz;
		*p++ = ',';
	}

	memcpy(p, name, nsz);
	p += nsz;

	if (vsz) {
		*p++ = '=';
		memcpy(p, value, vsz);
		p += vsz;
	}
	*p = '\0';

	return 0;
}

/**
 * mnt_optstr_append_option:
 * @optstr: option string or NULL, returns reallocated string
 * @name: value name
 * @value: value
 *
 * Returns: 0 on success or -1 in case of error. After error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_append_option(char **optstr, const char *name, const char *value)
{
	size_t vsz, nsz;

	if (!name)
		return 0;

	nsz = strlen(name);
	vsz = value ? strlen(value) : 0;

	return __mnt_optstr_append_option(optstr, name, nsz, value, vsz);
}

/**
 * mnt_optstr_prepend_option:
 * @optstr: option string or NULL, returns reallocated string
 * @name: value name
 * @value: value
 *
 * Returns: 0 on success or -1 in case of error. After error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_prepend_option(char **optstr, const char *name, const char *value)
{
	int rc = 0;
	char *tmp = *optstr;

	*optstr = NULL;

	rc = mnt_optstr_append_option(optstr, name, value);
	if (!rc && tmp && *tmp)
		rc = mnt_optstr_append_option(optstr, tmp, NULL);
	if (!rc) {
		free(tmp);
		return 0;
	}

	free(*optstr);
	*optstr = tmp;

	DBG(OPTIONS, mnt_debug("failed to prepend '%s[=%s]' to '%s'",
				name, value, *optstr));
	return rc;
}

/**
 * mnt_optstr_get_option:
 * @optstr: string with comma separated list of options
 * @name: requested option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of the value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_get_option(char *optstr, const char *name,
				char **value, size_t *valsz)
{
	struct mnt_optloc ol;
	int rc;

	mnt_init_optloc(&ol);

	rc = mnt_optstr_locate_option(optstr, name, &ol);
	if (!rc) {
		if (value)
			*value = ol.value;
		if (valsz)
			*valsz = ol.valsz;
	}
	return rc;
}

/*
 * The result never starts or ends with comma or contains two commas
 *    (e.g. ",aaa,bbb" or "aaa,,bbb" or "aaa,")
 */
int mnt_optstr_remove_option_at(char **optstr, char *begin, char *end)
{
	size_t sz;

	if (!optstr || !begin || !end)
		return -EINVAL;

	if ((begin == *optstr || *(begin - 1) == ',') && *end == ',')
		end++;

	sz = strlen(end);

	memmove(begin, end, sz + 1);
	if (!*begin && *(begin - 1) == ',')
		*(begin - 1) = '\0';

	return 0;
}

/* insert 'substr' or '=substr' to @str on position @pos */
static int insert_value(char **str, char *pos, const char *substr, char **next)
{
	size_t subsz = strlen(substr);			/* substring size */
	size_t strsz = strlen(*str);
	size_t possz = strlen(pos);
	size_t posoff;
	char *p = NULL;
	int sep;

	/* is it necessary to prepend '=' before the substring ? */
	sep = !(pos > *str && *(pos - 1) == '=');

	/* save an offset of the place where we need add substr */
	posoff = pos - *str;

	p = realloc(*str, strsz + sep + subsz + 1);
	if (!p)
		return -ENOMEM;

	/* zeroize new allocated memory -- valgind loves is... */
	memset(p + strsz, 0, sep + subsz + 1);

	/* set pointers to the reallocated string */
	*str = p;
	pos = p + posoff;

	if (possz)
		/* create a room for new substring */
		memmove(pos + subsz + sep, pos, possz + 1);
	if (sep)
		*pos++ = '=';

	memcpy(pos, substr, subsz);

	if (next) {
		/* set pointer to the next option */
		*next = pos + subsz + sep + 1;
		if (**next == ',')
			(*next)++;
	}
	return 0;
}

/**
 * mnt_optstr_set_option:
 * @optstr: string with comma separated list of options
 * @name: requested option
 * @value: new value or NULL
 *
 * Set or unset option @value.
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_set_option(char **optstr, const char *name, const char *value)
{
	struct mnt_optloc ol;
	char *nameend;
	int rc = 1;

	if (!optstr)
		return -EINVAL;

	mnt_init_optloc(&ol);

	if (*optstr)
		rc = mnt_optstr_locate_option(*optstr, name, &ol);
	if (rc < 0)
		return rc;			/* parse error */
	if (rc == 1)
		return mnt_optstr_append_option(optstr, name, value);	/* not found */

	nameend = ol.begin + ol.namesz;

	if (value == NULL && ol.value && ol.valsz)
		/* remove unwanted "=value" */
		mnt_optstr_remove_option_at(optstr, nameend, ol.end);

	else if (value && ol.value == NULL)
		/* insert "=value" */
		rc = insert_value(optstr, nameend, value, NULL);

	else if (value && ol.value && strlen(value) == ol.valsz)
		/* simply replace =value */
		memcpy(ol.value, value, ol.valsz);

	else if (value && ol.value) {
		mnt_optstr_remove_option_at(optstr, nameend, ol.end);
		rc = insert_value(optstr, nameend, value, NULL);
	}
	return rc;
}

/**
 * mnt_optstr_remove_option:
 * @optstr: string with comma separated list of options
 * @name: requested option name
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_remove_option(char **optstr, const char *name)
{
	struct mnt_optloc ol;
	int rc;

	mnt_init_optloc(&ol);

	rc = mnt_optstr_locate_option(*optstr, name, &ol);
	if (rc != 0)
		return rc;

	mnt_optstr_remove_option_at(optstr, ol.begin, ol.end);
	return 0;
}

/**
 * mnt_split_optstr:
 * @optstr: string with comma separated list of options
 * @user: returns newly allocated string with userspace options
 * @vfs: returns newly allocated string with VFS options
 * @fs: returns newly allocated string with FS options
 * @ignore_user: option mask for options that should be ignored
 * @ignore_vfs: option mask for options that should be ignored
 *
 * For example:
 *
 *	mnt_split_optstr(optstr, &u, NULL, NULL, MNT_NOMTAB, 0);
 *
 * returns all userspace options, the options that does not belong to
 * mtab are ignored.
 *
 * Note that FS options are all options that are undefined in MNT_USERSPACE_MAP
 * or MNT_LINUX_MAP.
 *
 * Returns: 0 on success, or negative number in case of error.
 */
int mnt_split_optstr(const char *optstr, char **user, char **vfs, char **fs, int ignore_user, int ignore_vfs)
{
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz;
	struct mnt_optmap const *maps[2];

	assert(optstr);

	if (!optstr)
		return -EINVAL;

	maps[0] = mnt_get_builtin_optmap(MNT_LINUX_MAP);
	maps[1] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);

	if (vfs)
		*vfs = NULL;
	if (fs)
		*fs = NULL;
	if (user)
		*user = NULL;

	while(!mnt_optstr_next_option(&str, &name, &namesz, &val, &valsz)) {
		int rc = 0;
		const struct mnt_optmap *ent = NULL;
		const struct mnt_optmap *m =
			 mnt_optmap_get_entry(maps, 2, name, namesz, &ent);

		if (ent && !ent->id)
			continue;	/* ignore undefined options (comments) */

		if (ent && m && m == maps[0] && vfs) {
			if (ignore_vfs && (ent->mask & ignore_vfs))
				continue;
			rc = __mnt_optstr_append_option(vfs, name, namesz,
								val, valsz);
		} else if (ent && m && m == maps[1] && user) {
			if (ignore_user && (ent->mask & ignore_user))
				continue;
			rc = __mnt_optstr_append_option(user, name, namesz,
								val, valsz);
		} else if (!m && fs)
			rc = __mnt_optstr_append_option(fs, name, namesz,
								val, valsz);
		if (rc) {
			if (vfs)
				free(*vfs);
			if (fs)
				free(*fs);
			if (user)
				free(*user);
			return rc;
		}
	}

	return 0;
}

/**
 * mnt_optstr_get_options
 * @optstr: string with comma separated list of options
 * @subset: returns newly allocated string with options
 * @map: options map
 * @ignore: mask of the options that should be ignored
 *
 * Extracts options from @optstr that belongs to the @map, for example:
 *
 *	 mnt_split_optstr_by_map(optstr, &p,
 *			mnt_get_builtin_optmap(MNT_LINUX_MAP),
 *			MNT_NOMTAB);
 *
 * returns all VFS options, the options that does not belong to mtab
 * are ignored.
 *
 * Returns: 0 on success, or negative number in case of error.
 */
int mnt_optstr_get_options(const char *optstr, char **subset,
			    const struct mnt_optmap *map, int ignore)
{
	struct mnt_optmap const *maps[1];
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz;

	if (!optstr || !subset)
		return -EINVAL;

	maps[0] = map;
	*subset = NULL;

	while(!mnt_optstr_next_option(&str, &name, &namesz, &val, &valsz)) {
		int rc = 0;
		const struct mnt_optmap *ent;

		mnt_optmap_get_entry(maps, 1, name, namesz, &ent);

		if (!ent || !ent->id)
			continue;	/* ignore undefined options (comments) */

		if (ignore && (ent->mask & ignore))
			continue;
		rc = __mnt_optstr_append_option(subset, name, namesz, val, valsz);
		if (rc) {
			free(*subset);
			return rc;
		}
	}

	return 0;
}

