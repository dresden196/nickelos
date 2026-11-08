/* NickelOS: what nvidia-vaapi-driver needs from the VP9 parser beyond the
 * frame header. The header holds only what its frame updates; the loop
 * filter deltas and segment features in effect are the parser's state,
 * carried over from earlier frames (a keyframe's ref deltas 1, 0, -1, -1
 * last until a frame changes them). Reading them from the header gave NVDEC
 * zeros on most inter frames: slightly wrong loop filtering. */
#ifndef NICKEL_VP9_STATE_H
#define NICKEL_VP9_STATE_H

#include <gst/codecparsers/gstvp9parser.h>

typedef struct {
  gint8 ref_deltas[GST_VP9_MAX_REF_LF_DELTAS];
  gint8 mode_deltas[GST_VP9_MAX_MODE_LF_DELTAS];
  gboolean segmentation_abs_delta;
  GstVp9SegmentationInfoData segmentation[GST_VP9_MAX_SEGMENTS];
} NickelVp9State;

/* After gst_vp9_parser_parse_frame_header(): the state for that frame. */
void nickel_vp9_parser_get_state (GstVp9Parser * parser, NickelVp9State * state);

#endif
