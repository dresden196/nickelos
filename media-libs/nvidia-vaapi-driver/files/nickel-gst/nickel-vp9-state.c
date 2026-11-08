/* NickelOS: gst-plugins-bad's VP9 parser, unmodified, plus an accessor for
 * its private state (nickel-vp9-state.h). Built instead of gstvp9parser.c,
 * which it includes for GstVp9ParserPrivate. */
#include "gstvp9parser.c"
#include "nickel-vp9-state.h"

void
nickel_vp9_parser_get_state (GstVp9Parser * parser, NickelVp9State * state)
{
  GstVp9ParserPrivate *priv = GST_VP9_PARSER_GET_PRIVATE (parser);

  memcpy (state->ref_deltas, priv->ref_deltas, sizeof (state->ref_deltas));
  memcpy (state->mode_deltas, priv->mode_deltas, sizeof (state->mode_deltas));
  state->segmentation_abs_delta = priv->segmentation_abs_delta;
  memcpy (state->segmentation, priv->segmentation,
      sizeof (state->segmentation));
}
