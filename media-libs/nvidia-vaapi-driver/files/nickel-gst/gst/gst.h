/* NickelOS: just enough of GStreamer for gst-plugins-bad's VP9 frame header
 * parser (gstvp9parser.c), which nvidia-vaapi-driver builds in instead of
 * linking the whole of GStreamer. Logging is compiled out. */
#ifndef NICKEL_GST_H
#define NICKEL_GST_H

#include <glib.h>

#define GST_DEBUG_CATEGORY_STATIC(cat) static int cat G_GNUC_UNUSED
#define GST_DEBUG_CATEGORY_INIT(cat, name, color, description) ((void) 0)
#define GST_ERROR(...) ((void) 0)
#define GST_WARNING(...) ((void) 0)
#define GST_INFO(...) ((void) 0)
#define GST_DEBUG(...) ((void) 0)
#define GST_LOG(...) ((void) 0)

#endif
