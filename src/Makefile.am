plugin_LTLIBRARIES = libgstgz.la

libgstgz_la_SOURCES = gstgz.c gstgzenc.c gstgzdec.c

libgstgz_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgz_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(ZLIB_LIBS)
libgstgz_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgz_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstgzdec.h gstgzenc.h
