/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the GNU Public
 * License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <blkid.h>

#include "nls.h"
#include "closestream.h"
#include "c.h"

static void __attribute__((__noreturn__)) usage(int rc)
{
	FILE *out = rc ? stderr : stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] {LABEL,UUID,PARTUUID,PARTLABEL}=<value>\n"),
		program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("findfs(8)"));
	exit(rc);
}

int main(int argc, char **argv)
{
	char	*dev;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc != 2)
		/* we return '2' for backward compatibility
		 * with version from e2fsprogs */
		usage(2);

	if (strcmp(argv[1], "-V") == 0 ||
		   strcmp(argv[1], "--version") == 0) {
		printf(UTIL_LINUX_VERSION);
		return EXIT_SUCCESS;
	} else if (strcmp(argv[1], "-h") == 0 ||
		   strcmp(argv[1], "--help") == 0) {
		usage(EXIT_SUCCESS);
	} else if (argv[1][0] == '-')
		usage(2);

	dev = blkid_evaluate_tag(argv[1], NULL, NULL);
	if (!dev)
		errx(EXIT_FAILURE, _("unable to resolve '%s'"),	argv[1]);

	puts(dev);
	exit(EXIT_SUCCESS);
}

