AUTOMAKE_OPTIONS = subdir-objects

CUSTOM_CXXFLAGS = $(WARN_CFLAGS) \
	-I$(top_srcdir)/src \
	-isystem $(top_srcdir)/rapidxml \
	$(LIBEGT_CFLAGS)

CUSTOM_LDADD = $(LIBEGT_LIBS)

AM_CXXFLAGS = -DDATADIR=\"$(datadir)\"

bin_PROGRAMS = egt-launcher

egt_launcher_SOURCES = src/launcher.cpp
egt_launcher_CXXFLAGS = $(CUSTOM_CXXFLAGS) $(AM_CXXFLAGS)
egt_launcher_LDADD = $(CUSTOM_LDADD)
egt_launcherdir = $(prefix)/share/egt/launcher
dist_egt_launcher_DATA = $(top_srcdir)/images/background.png \
	$(top_srcdir)/taglines.txt
egt_launcher_LDFLAGS = $(AM_LDFLAGS)
egt_launcher_SCRIPTS = launch.sh

EXTRA_DIST = \
	README.md \
	COPYING \
	rapidxml \
	launch.sh \
	example.xml \
	taglines.txt \
	$(wildcard $(top_srcdir)/images/*.png)

TIDY_OPTIONS = $(AM_CPPFLAGS) $(AM_CXXFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(CUSTOM_CXXFLAGS)
TIDY_SOURCE = $(SOURCES)

tidy-local: all-am
	@for source in $(TIDY_SOURCE); do \
		[[ $$source == *.c ]] && continue; \
		[[ $$source == *.h ]] && continue; \
		$(CLANG_TIDY_BIN) $(srcdir)/$$source -- $(TIDY_OPTIONS); \
	done

tidy: tidy-local
