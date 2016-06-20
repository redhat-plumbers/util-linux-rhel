/*
 * utmpdump
 *
 * Simple program to dump UTMP and WTMP files in raw format, so they can be
 * examined.
 *
 * Based on utmpdump dump from sysvinit suite.
 *
 * Copyright (C) 1991-2000 Miquel van Smoorenburg <miquels@cistron.nl>
 *
 * Copyright (C) 1998 Danek Duvall <duvall@alumni.princeton.edu>
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <time.h>
#include <ctype.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#ifdef HAVE_INOTIFY_INIT
#include <sys/inotify.h>
#endif

#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "closestream.h"

static char *timetostr(const time_t time)
{
	static char s[29];	/* [Sun Sep 01 00:00:00 1998 PST] */

	if (time != 0)
		strftime(s, 29, "%a %b %d %T %Y %Z", localtime(&time));
	else
		s[0] = '\0';

	return s;
}

static time_t strtotime(const char *s_time)
{
	struct tm tm;

	memset(&tm, '\0', sizeof(struct tm));

	if (s_time[0] == ' ' || s_time[0] == '\0')
		return (time_t)0;

	strptime(s_time, "%a %b %d %T %Y", &tm);

	/* Cheesy way of checking for DST */
	if (s_time[26] == 'D')
		tm.tm_isdst = 1;

	return mktime(&tm);
}

#define cleanse(x) xcleanse(x, sizeof(x))
static void xcleanse(char *s, int len)
{
	for ( ; *s && len-- > 0; s++)
		if (!isprint(*s) || *s == '[' || *s == ']')
			*s = '?';
}

static void print_utline(struct utmp ut)
{
	const char *addr_string, *time_string;
	char buffer[INET6_ADDRSTRLEN];

	if (ut.ut_addr_v6[1] || ut.ut_addr_v6[2] || ut.ut_addr_v6[3])
		addr_string = inet_ntop(AF_INET6, &(ut.ut_addr_v6), buffer, sizeof(buffer));
	else
		addr_string = inet_ntop(AF_INET, &(ut.ut_addr_v6), buffer, sizeof(buffer));

	time_string = timetostr(ut.ut_time);
	cleanse(ut.ut_id);
	cleanse(ut.ut_user);
	cleanse(ut.ut_line);
	cleanse(ut.ut_host);

	/*            pid    id       user     line     host     addr       time */
	printf("[%d] [%05d] [%-4.4s] [%-*.*s] [%-*.*s] [%-*.*s] [%-15s] [%-28.28s]\n",
	       ut.ut_type, ut.ut_pid, ut.ut_id, 8, UT_NAMESIZE, ut.ut_user,
	       12, UT_LINESIZE, ut.ut_line, 20, UT_HOSTSIZE, ut.ut_host,
	       addr_string, time_string);
}

#ifdef HAVE_INOTIFY_INIT
#define EVENTS		(IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT)
#define NEVENTS		4

static void roll_file(const char *filename, off_t *size)
{
	FILE *fp;
	struct stat st;
	struct utmp ut;
	off_t pos;

	if (!(fp = fopen(filename, "r")))
		err(EXIT_FAILURE, _("cannot open %s"), filename);

	if (fstat(fileno(fp), &st) == -1)
		err(EXIT_FAILURE, _("%s: stat failed"), filename);

	if (st.st_size == *size)
		goto done;

	if (fseek(fp, *size, SEEK_SET) != (off_t) -1) {
		while (fread(&ut, sizeof(ut), 1, fp) == 1)
			print_utline(ut);
	}

	pos = ftello(fp);
	/* If we've successfully read something, use the file position, this
	 * avoids data duplication.  If we read nothing or hit an error,
	 * reset to the reported size, this handles truncated files.
	 */
	*size = (pos != -1 && pos != *size) ? pos : st.st_size;

done:
	fclose(fp);
}

static int follow_by_inotify(FILE *fp, const char *filename)
{
	char buf[NEVENTS * sizeof(struct inotify_event)];
	int fd, wd, event;
	ssize_t length;
	off_t size;

	fd = inotify_init();
	if (fd == -1)
		return -1;	/* probably reached any limit ... */

	size = ftello(fp);
	fclose(fp);

	wd = inotify_add_watch(fd, filename, EVENTS);
	if (wd == -1)
		err(EXIT_FAILURE, _("%s: cannot add inotify watch."), filename);

	while (wd >= 0) {
		errno = 0;
		length = read(fd, buf, sizeof(buf));

		if (length < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (length < 0)
			err(EXIT_FAILURE, _("%s: cannot read inotify events"),
				    filename);

		for (event = 0; event < length;) {
			struct inotify_event *ev =
				    (struct inotify_event *) &buf[event];

			if (ev->mask & IN_MODIFY)
				roll_file(filename, &size);
			else {
				close(wd);
				wd = -1;
				break;
			}
			event += sizeof(struct inotify_event) + ev->len;
		}
	}

	close(fd);
	return 0;
}
#endif /* HAVE_INOTIFY_INIT */

static FILE *dump(FILE *fp, const char *filename, int follow)
{
	struct utmp ut;

	if (follow)
		fseek(fp, -10 * sizeof(ut), SEEK_END);

	while (fread(&ut, sizeof(ut), 1, fp) == 1)
		print_utline(ut);

	if (!follow)
		return fp;

#ifdef HAVE_INOTIFY_INIT
	if (follow_by_inotify(fp, filename) == 0)
		return NULL;				/* file already closed */
	else
#endif
		/* fallback for systems without inotify or with non-free
		 * inotify instances */
		for (;;) {
			while (fread(&ut, sizeof(ut), 1, fp) == 1)
				print_utline(ut);
			sleep(1);
		}

	return fp;
}


/* This function won't work properly if there's a ']' or a ' ' in the real
 * token.  Thankfully, this should never happen.  */
static int gettok(char *line, char *dest, int size, int eatspace)
{
	int bpos, epos, eaten;

	bpos = strchr(line, '[') - line;
	if (bpos < 0)
		errx(EXIT_FAILURE, _("Extraneous newline in file. Exiting."));

	line += 1 + bpos;
	epos = strchr(line, ']') - line;
	if (epos < 0)
		errx(EXIT_FAILURE, _("Extraneous newline in file. Exiting."));

	line[epos] = '\0';
	eaten = bpos + epos + 1;

	if (eatspace) {
		char *t;
		if ((t = strchr(line, ' ')))
			*t = 0;
	}
	strncpy(dest, line, size);

	return eaten + 1;
}

static void undump(FILE *fp)
{
	struct utmp ut;
	char s_addr[INET6_ADDRSTRLEN + 1], s_time[29], *linestart, *line;
	int count = 0;

	line = linestart = xmalloc(1024 * sizeof(*linestart));
	s_time[28] = 0;

	while (fgets(linestart, 1023, fp)) {
		line = linestart;
		memset(&ut, '\0', sizeof(ut));
		sscanf(line, "[%hd] [%d] [%4c] ", &ut.ut_type, &ut.ut_pid, ut.ut_id);

		line += 19;
		line += gettok(line, ut.ut_user, sizeof(ut.ut_user), 1);
		line += gettok(line, ut.ut_line, sizeof(ut.ut_line), 1);
		line += gettok(line, ut.ut_host, sizeof(ut.ut_host), 1);
		line += gettok(line, s_addr, sizeof(s_addr) - 1, 1);
		line += gettok(line, s_time, sizeof(s_time) - 1, 0);

		if (strchr(s_addr, '.'))
			inet_pton(AF_INET, s_addr, &(ut.ut_addr_v6));
		else
			inet_pton(AF_INET6, s_addr, &(ut.ut_addr_v6));
		ut.ut_time = strtotime(s_time);

		ignore_result( fwrite(&ut, sizeof(ut), 1, stdout) );

		++count;
	}

	free(linestart);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options] [filename]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --follow   output appended data as the file grows\n"
		" -r, --reverse  write back dumped data into utmp file\n"), out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("utmpdump(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	FILE *fp = NULL;
	int reverse = 0, follow = 0;
	const char *filename = NULL;

	static const struct option longopts[] = {
		{ "follow",  0, 0, 'f' },
		{ "reverse", 0, 0, 'r' },
		{ "help",    0, 0, 'h' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "frhV", longopts, NULL)) != -1) {
		switch (c) {
		case 'r':
			reverse = 1;
			break;

		case 'f':
			follow = 1;
			break;

		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	if (optind < argc) {
		filename = argv[optind];
		fp = fopen(filename, "r");
		if (!fp)
			err(EXIT_FAILURE, _("cannot open %s"), filename);
	} else {
		if (follow)
			errx(EXIT_FAILURE, _("following standard input is unsupported"));
		filename = "/dev/stdin";
		fp = stdin;
	}

	if (reverse) {
		fprintf(stderr, _("Utmp undump of %s\n"), filename);
		undump(fp);
	} else {
		fprintf(stderr, _("Utmp dump of %s\n"), filename);
		fp = dump(fp, filename, follow);
	}

	if (fp && fp != stdin)
		fclose(fp);

	return EXIT_SUCCESS;
}
