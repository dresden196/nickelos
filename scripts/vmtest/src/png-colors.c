/* NickelOS hardware tests: what colors a screenshot holds, for
 * hw/x-sync-test.sh (test machines have no PIL, and `screenshot --crop`
 * comes back black on NVIDIA). Reads every 8th row of each PNG and prints
 * "red", "blue" or "mixed N%red N%blue N%black N%other mean=R,G,B".
 * --window: only inside the test window (the rows and columns that are
 * mostly red or blue), 16 pixels in from its edges, clear of its rounded
 * corners; "none" without one.
 *   png-colors [--window] FILE.png...
 * Build: vmtest/build-test-client.sh png-colors libpng. */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RED, BLUE, BLACK, OTHER, KINDS };

static int classify(const png_byte* p) {
  if (p[0] > 200 && p[1] < 50 && p[2] < 50)
    return RED;
  if (p[2] > 200 && p[0] < 50 && p[1] < 50)
    return BLUE;
  if (p[0] < 30 && p[1] < 30 && p[2] < 30)
    return BLACK;
  return OTHER;
}

static int window_only = 0;

static int check(const char* path) {
  png_image image = {0};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path)) {
    printf("%s: %s\n", path, image.message);
    return 1;
  }
  image.format = PNG_FORMAT_RGB;
  png_bytep buffer = malloc(PNG_IMAGE_SIZE(image));
  if (!buffer || !png_image_finish_read(&image, NULL, buffer, 0, NULL)) {
    printf("%s: %s\n", path, image.message);
    return 1;
  }
  png_uint_32 x0 = 0, y0 = 0, x1 = image.width, y1 = image.height;
  if (window_only) {
    /* The window: rows and columns with many red or blue pixels (icons
     * elsewhere, e.g. on the shelf, have only a few). */
    long* rows = calloc(image.height, sizeof(long));
    long* cols = calloc(image.width, sizeof(long));
    for (png_uint_32 y = 0; y < image.height; y += 2) {
      const png_byte* row = buffer + (size_t)y * image.width * 3;
      for (png_uint_32 x = 0; x < image.width; x += 2) {
        int kind = classify(row + x * 3);
        if (kind == RED || kind == BLUE) {
          rows[y]++;
          cols[x]++;
        }
      }
    }
    png_uint_32 bx0 = image.width, by0 = image.height, bx1 = 0, by1 = 0;
    for (png_uint_32 y = 0; y < image.height; y += 2) {
      if (rows[y] > (long)image.width / 16) {
        if (y < by0) by0 = y;
        by1 = y;
      }
    }
    for (png_uint_32 x = 0; x < image.width; x += 2) {
      if (cols[x] > (long)image.height / 16) {
        if (x < bx0) bx0 = x;
        bx1 = x;
      }
    }
    free(rows);
    free(cols);
    if (bx1 < bx0 + 64 || by1 < by0 + 64) {
      printf("none\n");
      free(buffer);
      return 0;
    }
    x0 = bx0 + 16, y0 = by0 + 16, x1 = bx1 - 16, y1 = by1 - 16;
  }
  long counts[KINDS] = {0}, total = 0, sum[3] = {0};
  for (png_uint_32 y = y0; y < y1; y += 8) {
    const png_byte* row = buffer + (size_t)y * image.width * 3;
    for (png_uint_32 x = x0; x < x1; x++) {
      counts[classify(row + x * 3)]++;
      for (int c = 0; c < 3; c++)
        sum[c] += row[x * 3 + c];
      total++;
    }
  }
  free(buffer);
  if (counts[RED] == total)
    printf("red\n");
  else if (counts[BLUE] == total)
    printf("blue\n");
  else
    printf("mixed %ld%%red %ld%%blue %ld%%black %ld%%other mean=%ld,%ld,%ld\n",
           100 * counts[RED] / total, 100 * counts[BLUE] / total,
           100 * counts[BLACK] / total, 100 * counts[OTHER] / total,
           sum[0] / total, sum[1] / total, sum[2] / total);
  return 0;
}

int main(int argc, char** argv) {
  int status = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--window"))
      window_only = 1;
    else
      status |= check(argv[i]);
  }
  return status;
}
