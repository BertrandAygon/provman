docpkg = $(PACKAGE_TARNAME)-doxy-$(PACKAGE_VERSION).tar.gz

doc_DATA = $(docpkg)

DEPENDENCIES = \
	$(top_srcdir)/doc/mainpage.h \
	$(top_srcdir)/doc/dbus.h \
	$(top_srcdir)/doc/coding-style.h \
	$(top_srcdir)/doc/getting-started.h \
	$(top_srcdir)/include/plugin.h \
	$(top_srcdir)/include/map_file.h \
	$(top_srcdir)/include/utils.h \
	$(top_srcdir)/src/plugin-session.c

$(docpkg): $(DEPENDENCIES)
	$(DOXYGEN) $(DOXYFLAGS)
	tar chof - html | gzip -9 -c >$@

install-data-hook:
	cd $(DESTDIR)$(docdir) && tar xf $(docpkg)

uninstall-hook:
	cd $(DESTDIR)$(docdir) && rm -rf html

CLEANFILES = doxywarn.txt $(docpkg)

clean-local:
	rm -rf html

