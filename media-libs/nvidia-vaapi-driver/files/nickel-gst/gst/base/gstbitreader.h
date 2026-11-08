/* NickelOS: see gst/gst.h. The part of GstBitReader the VP9 parser uses: MSB
 * first, and its "unchecked" reads past the end of the data read zeros, so
 * a truncated frame header from a video cannot make it read past the buffer. */
#ifndef NICKEL_GST_BIT_READER_H
#define NICKEL_GST_BIT_READER_H

#include <glib.h>

typedef struct {
  const guint8 *data;
  guint size;
  guint byte;
  guint bit;
} GstBitReader;

static inline void
gst_bit_reader_init (GstBitReader * reader, const guint8 * data, guint size)
{
  reader->data = data;
  reader->size = size;
  reader->byte = 0;
  reader->bit = 0;
}

static inline guint
gst_bit_reader_get_pos (const GstBitReader * reader)
{
  return reader->byte * 8 + reader->bit;
}

static inline guint32
gst_bit_reader_get_bits_uint32_unchecked (GstBitReader * reader, guint nbits)
{
  guint32 value = 0;
  for (guint i = 0; i < nbits; i++) {
    guint bit = 0;
    if (reader->byte < reader->size)
      bit = (reader->data[reader->byte] >> (7 - reader->bit)) & 1;
    value = (value << 1) | bit;
    if (++reader->bit == 8) {
      reader->bit = 0;
      reader->byte++;
    }
  }
  return value;
}

static inline guint8
gst_bit_reader_get_bits_uint8_unchecked (GstBitReader * reader, guint nbits)
{
  return (guint8) gst_bit_reader_get_bits_uint32_unchecked (reader, nbits);
}

#endif
