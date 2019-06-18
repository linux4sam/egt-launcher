AUTOMAKE_OPTIONS = subdir-objects

CUSTOM_CXXFLAGS = $(WARN_CFLAGS) \
	-I$(top_srcdir)/src \
	-isystem $(top_srcdir)/rapidxml \
	$(LIBEGT_CFLAGS)

CUSTOM_LDADD = $(LIBEGT_LIBS)

bin_PROGRAMS = launcher

launcher_SOURCES = src/launcher.cpp
launcher_CXXFLAGS = $(CUSTOM_CXXFLAGS) $(AM_CXXFLAGS)
launcher_LDADD = $(LIBEGT_LIBS) $(CUSTOM_LDADD)
launcherdir = $(prefix)/share/egt/samples/launcher
dist_launcher_DATA = $(wildcard $(top_srcdir)/images/*.png) \
	$(top_srcdir)/menu.xml \
	$(top_srcdir)/images/background.jpg
launcher_LDFLAGS = $(AM_LDFLAGS)
launcher_SCRIPTS = launch.sh
