AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libgda \
	-I$(top_builddir) \
	$(COREDEPS_CFLAGS) \
	$(COREDEPS_WFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	-DROOT_DIR=\""$(top_srcdir)"\"

TESTS_ENVIRONMENT = GDA_TOP_SRC_DIR="$(abs_top_srcdir)" GDA_TOP_BUILD_DIR="$(abs_top_builddir)"
TESTS = check_holder check_set check_statement
check_PROGRAMS = check_holder check_set check_statement

common_sources = \
	../test-errors.h \
	../test-errors.c \
	common.c \
	common.h

check_holder_SOURCES = check_holder.c $(common_sources)
check_holder_LDADD = $(JSON_GLIB_LIBS) \
	$(top_builddir)/libgda/libgda-6.0.la \
	$(COREDEPS_LIBS)

check_set_SOURCES = check_set.c $(common_sources)
check_set_LDADD = $(JSON_GLIB_LIBS) \
	$(top_builddir)/libgda/libgda-6.0.la \
	$(COREDEPS_LIBS)

check_statement_SOURCES = check_statement.c $(common_sources)
check_statement_LDADD = $(JSON_GLIB_LIBS) \
	$(top_builddir)/libgda/libgda-6.0.la \
	$(COREDEPS_LIBS)


EXTRA_DIST = set.data stmt.data
