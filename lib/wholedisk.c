
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "blkdev.h"
#include "sysfs.h"
#include "wholedisk.h"

int is_whole_disk_fd(int fd, const char *name)
{
#ifdef HDIO_GETGEO
	if (fd != -1) {
		struct hd_geometry geometry;
		int i = ioctl(fd, HDIO_GETGEO, &geometry);
		if (i == 0)
			return geometry.start == 0;
	}
#endif
	/*
	 * The "silly heuristic" is still sexy for us, because
	 * for example Xen doesn't implement HDIO_GETGEO for virtual
	 * block devices (/dev/xvda).
	 *
	 * -- kzak@redhat.com (23-Feb-2006)
	 */
	while (*name)
		name++;
	return !isdigit(name[-1]);
}

int is_whole_disk(const char *name)
{
	dev_t devno = sysfs_devname_to_devno(name, NULL);

	if (sysfs_devno_is_lvm_private(devno) ||
	    sysfs_devno_is_wholedisk(devno) <= 0)
		return 0;

	return 1;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	printf("%s: is%s whole disk\n", argv[1],
			is_whole_disk(argv[1]) ? "" : " NOT");
	exit(EXIT_SUCCESS);
}
#endif
