#ifndef BENCH_CURSOR_H
#define BENCH_CURSOR_H
#include <gdk-pixbuf/gdk-pixbuf.h>
/* Loads the supplied CUR's 32-bit DIB frame, preserving alpha and hotspot.
 * No icon-loader plugins or image conversions are required at runtime. */
GdkPixbuf *cursor_load(const char *path,int size,int *hot_x,int *hot_y,GError **e);
#endif
