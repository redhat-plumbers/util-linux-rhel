include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libmount_incdir) \
	       -I$(ul_libmount_srcdir) \
	       -I$(ul_libblkid_incdir)





usrlib_exec_LTLIBRARIES = libmount.la
libmount_la_SOURCES =	mountP.h utils.c init.c cache.c \
			optstr.c iter.c optmap.c \
			fs.c tab.c tab_parse.c \
			$(mountinc_HEADERS) \
			$(top_srcdir)/include/list.h \
			$(top_srcdir)/lib/canonicalize.c \
			$(top_srcdir)/lib/strutils.c \
			$(top_srcdir)/lib/env.c

nodist_libmount_la_SOURCES = mount.h mountP.h

libmount_la_LIBADD = $(ul_libblkid_la)

libmount_la_DEPENDENCIES = $(libmount_la_LIBADD) mount.sym mount.h.in

libmount_la_LDFLAGS = -Wl,--version-script=$(ul_libmount_srcdir)/mount.sym \
                      -version-info $(LIBMOUNT_VERSION_INFO)

EXTRA_DIST = mount.sym mount.h.in

# move lib from $(usrlib_execdir) to $(libdir) if needed
install-exec-hook:
	if test "$(usrlib_execdir)" != "$(libdir)"; then \
		mkdir -p $(DESTDIR)$(libdir); \
		mv $(DESTDIR)$(usrlib_execdir)/libmount.so.* $(DESTDIR)$(libdir); \
		so_img_name=$$(readlink $(DESTDIR)$(usrlib_execdir)/libmount.so); \
		so_img_rel_target=$$(echo $(usrlib_execdir) | sed 's,\(^/\|\)[^/][^/]*,..,g'); \
		(cd $(DESTDIR)$(usrlib_execdir) && \
			rm -f libmount.so && \
			$(LN_S) $$so_img_rel_target$(libdir)/$$so_img_name libmount.so); \
	fi

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libmount.so*
