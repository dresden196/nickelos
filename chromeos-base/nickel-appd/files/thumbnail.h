// Distributed under the terms of the GNU General Public License v2

#ifndef NICKEL_APPD_THUMBNAIL_H_
#define NICKEL_APPD_THUMBNAIL_H_

#include <gio/gio.h>

struct Thumbnails;

Thumbnails* ThumbnailsNew();

// Thumbnail(s path, u size) -> (ay png): a PNG preview of the file at |path|,
// at most |size| pixels on its longer side (rounded up to the freedesktop
// sizes 128, 256, 512 or 1024), or an error if there is none.
void HandleThumbnail(Thumbnails* thumbnails,
                     GDBusMethodInvocation* invocation,
                     GVariant* parameters);

#endif  // NICKEL_APPD_THUMBNAIL_H_
