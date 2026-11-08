// Distributed under the terms of the GNU General Public License v2
//
// nickel-xim: ChromeOS's input methods for X11 apps.
//
// Wayland apps type through ChromeOS's input methods with text-input (Exo).
// X11 apps reach an input method through XIM, the X11 protocol every X11
// toolkit speaks: Xlib (Wine and Proton, Java, SDL, xterm), GTK 3 (its xim
// module) and Chromium and Electron on X11 (through GTK). This is an XIM
// server (xcb-imdkit, the XIM library fcitx5 uses) that hands an app's input
// context to ChromeOS's input method:
//
//   - sommelier, which runs Xwayland, serves a Wayland socket for X11 input
//     methods, DISPLAY-<display>-wl, with text-input (zwp_text_input_v1, the
//     version Exo implements) and zcr_text_input_crostini_v1.activate_x11,
//     which points the input method at an X11 window's surface. Crostini's
//     cros_im uses it from inside GTK; this serves every X11 app;
//   - when an app focuses an input context, the input method is activated
//     for the app's top-level window (the one sommelier, the window manager,
//     marked with WM_STATE). Exo then hands key events to the input method
//     first: keys it composes with never reach the app, the rest do;
//   - the app forwards those keys here (XIM's usual event flow) and gets
//     them back after what the input method committed before them: a key
//     that ends a composition (Korean's space after 한글) reaches the app
//     from Xwayland sooner than the commit comes through here, so each key
//     waits for a round trip to sommelier, which passes Exo's commit on
//     before the key;
//   - the composition (preedit) is drawn by apps that draw it themselves
//     (XIMPreeditCallbacks: GTK, Wine, Java's on-the-spot); for the others
//     (over-the-spot, root) ChromeOS shows it in its candidate window
//     (inline composition unsupported). Committed text reaches the app as
//     XIM commits (compound text), keys the input method sends as keysyms as
//     text or forwarded key events;
//   - the caret (XNSpotLocation, relative to the app's focus window) places
//     the candidate window.
//
// It is also the display's XSETTINGS manager (sommelier runs none), with
// Gtk/IMModule=xim: GTK on X11 reads its input method module there, GTK on
// Wayland from the settings portal (wayland). One GTK_IM_MODULE for both
// would have GTK 3 load the Wayland module on X11, which crashes it.
//
// One text-input per input context, created when it first takes focus;
// zwp_text_input_v1 has no destructor, so objects of destroyed contexts are
// kept for reuse rather than left behind in Exo.
//
// Run by the nickel-xim job as the session user, with DISPLAY set; apps find
// it through XMODIFIERS=@im=nickel. NICKEL_XIM_DEBUG=1 logs what happens.

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xkbcommon/xkbcommon.h>

#include "encoding.h"
#include "imdkit.h"
#include "text-input-crostini-unstable-v1-client-protocol.h"
#include "text-input-extension-unstable-v1-client-protocol.h"
#include "text-input-unstable-v1-client-protocol.h"

#define SERVER_NAME "nickel"
// The extension versions this was written against.
#define EXTENSION_VERSION 11
// How long to wait for the X display and sommelier's socket when the
// session starts.
#define CONNECT_TIMEOUT_S 30

static bool debug;

static void log_msg(const char* format, ...) {
  va_list args;
  va_start(args, format);
  fputs("nickel-xim: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}

#define DEBUG(...)       \
  do {                   \
    if (debug)           \
      log_msg(__VA_ARGS__); \
  } while (0)

// X11.
static xcb_connection_t* conn;
static xcb_screen_t* screen;
static xcb_atom_t wm_state_atom;
static xcb_im_t* im;
// Keysym to keycode, for keys the input method hands back as keysyms.
static xcb_keycode_t min_keycode;
static int keysyms_per_keycode;
static int keycode_count;
static xcb_keysym_t* keymap;

// Wayland, on sommelier's socket.
static struct wl_display* wl;
static struct wl_seat* seat;
static struct zwp_text_input_manager_v1* text_input_manager;
static struct zcr_text_input_extension_v1* text_input_extension;
static uint32_t extension_version;
static struct zcr_text_input_crostini_manager_v1* crostini_manager;

struct context;

// A text-input with its extensions, owned by an input context or free.
struct slot {
  struct zwp_text_input_v1* text_input;
  struct zcr_extended_text_input_v1* extended;
  struct zcr_text_input_crostini_v1* crostini;
  struct context* owner;
  struct slot* next_free;
  // Modifier names from the input method, by index (Shift, Control, ...).
  uint32_t modifier_masks[32];
  int modifier_count;
};

static struct slot* free_slots;

struct context {
  xcb_im_input_context_t* ic;
  struct slot* slot;
  bool active;      // the input method is activated for it
  xcb_window_t focus;     // the app's focus window then
  xcb_window_t toplevel;  // and its top-level window
  bool inline_preedit;  // the app draws the composition (XIMPreeditCallbacks)
  bool preediting;  // the app shows a composition
  uint32_t preedit_chars;  // its length in characters
  char* preedit;    // the input method's current composition (UTF-8)
  // Set by preedit_styling and preedit_cursor for the next preedit_string.
  struct {
    uint32_t index, length, style;
  } styles[16];
  int style_count;
  int32_t preedit_cursor;  // bytes
};

// Characters in a UTF-8 string, up to |bytes| bytes.
static uint32_t utf8_chars(const char* text, size_t bytes) {
  uint32_t chars = 0;
  for (size_t i = 0; i < bytes && text[i]; i++) {
    if ((text[i] & 0xc0) != 0x80)
      chars++;
  }
  return chars;
}

// The app's own top-level window: sommelier, the window manager, puts it in
// a frame and marks it with WM_STATE (ICCCM), and knows the app's windows by
// it (activate_x11).
static xcb_window_t find_toplevel(xcb_window_t window) {
  while (window && window != screen->root) {
    xcb_get_property_reply_t* property = xcb_get_property_reply(
        conn,
        xcb_get_property(conn, 0, window, wm_state_atom, XCB_ATOM_ANY, 0, 0),
        NULL);
    bool managed = property && property->type != XCB_ATOM_NONE;
    free(property);
    if (managed)
      return window;
    xcb_query_tree_reply_t* tree =
        xcb_query_tree_reply(conn, xcb_query_tree(conn, window), NULL);
    if (!tree)
      return XCB_WINDOW_NONE;
    window = tree->parent;
    free(tree);
  }
  return XCB_WINDOW_NONE;
}

static xcb_window_t focus_window(struct context* c) {
  xcb_window_t window = xcb_im_input_context_get_focus_window(c->ic);
  return window ? window : xcb_im_input_context_get_client_window(c->ic);
}

// Composition, as the app draws it (XIMPreeditCallbacks).

static void end_preedit(struct context* c) {
  if (!c->preediting)
    return;
  xcb_im_preedit_draw_fr_t frame = {0};
  frame.chg_length = c->preedit_chars;
  frame.status = 0x1 | 0x2;  // no string, no feedback
  xcb_im_preedit_draw_callback(im, c->ic, &frame);
  xcb_im_preedit_done_callback(im, c->ic);
  c->preediting = false;
  c->preedit_chars = 0;
}

static void draw_preedit(struct context* c, const char* text) {
  size_t bytes = strlen(text);
  uint32_t chars = utf8_chars(text, bytes);
  if (!chars) {
    end_preedit(c);
    return;
  }
  size_t ct_length = 0;
  char* ct = xcb_utf8_to_compound_text(text, bytes, &ct_length);
  if (!ct)
    return;
  uint32_t* feedback = calloc(chars, sizeof(*feedback));
  if (!feedback) {
    free(ct);
    return;
  }
  for (uint32_t i = 0; i < chars; i++)
    feedback[i] = XCB_XIM_UNDERLINE;
  for (int i = 0; i < c->style_count; i++) {
    uint32_t feedback_style;
    switch (c->styles[i].style) {
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_NONE:
        feedback_style = 0;
        break;
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_HIGHLIGHT:
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_SELECTION:
        feedback_style = XCB_XIM_REVERSE;
        break;
      default:
        feedback_style = XCB_XIM_UNDERLINE;
        break;
    }
    // Byte offsets in the composition.
    size_t start = c->styles[i].index;
    size_t end = start + c->styles[i].length;
    if (start > bytes)
      continue;
    if (end > bytes)
      end = bytes;
    uint32_t first = utf8_chars(text, start);
    uint32_t last = utf8_chars(text, end);
    for (uint32_t ch = first; ch < last && ch < chars; ch++)
      feedback[ch] = feedback_style;
  }
  uint32_t caret = chars;
  if (c->preedit_cursor >= 0 && (size_t)c->preedit_cursor <= bytes)
    caret = utf8_chars(text, c->preedit_cursor);

  if (!c->preediting) {
    xcb_im_preedit_start_callback(im, c->ic);
    c->preediting = true;
    c->preedit_chars = 0;
  }
  xcb_im_preedit_draw_fr_t frame = {0};
  frame.caret = caret;
  frame.chg_first = 0;
  frame.chg_length = c->preedit_chars;
  frame.status = 0;
  frame.length_of_preedit_string = ct_length;
  frame.preedit_string = (uint8_t*)ct;
  frame.feedback_array.size = chars;
  frame.feedback_array.items = feedback;
  xcb_im_preedit_draw_callback(im, c->ic, &frame);
  c->preedit_chars = chars;
  free(feedback);
  free(ct);
}

static void commit_text(struct context* c, const char* text) {
  end_preedit(c);
  if (!*text)
    return;
  size_t ct_length = 0;
  char* ct = xcb_utf8_to_compound_text(text, strlen(text), &ct_length);
  if (!ct)
    return;
  xcb_im_commit_string(im, c->ic, XCB_XIM_LOOKUP_CHARS, ct, ct_length, 0);
  free(ct);
}

// Keysym to keycode (and Shift), from the X server's keyboard mapping.
static void load_keymap(void) {
  const xcb_setup_t* setup = xcb_get_setup(conn);
  min_keycode = setup->min_keycode;
  keycode_count = setup->max_keycode - setup->min_keycode + 1;
  xcb_get_keyboard_mapping_reply_t* reply = xcb_get_keyboard_mapping_reply(
      conn, xcb_get_keyboard_mapping(conn, min_keycode, keycode_count), NULL);
  free(keymap);
  keymap = NULL;
  if (!reply)
    return;
  keysyms_per_keycode = reply->keysyms_per_keycode;
  int length = xcb_get_keyboard_mapping_keysyms_length(reply);
  keymap = malloc(length * sizeof(*keymap));
  if (keymap)
    memcpy(keymap, xcb_get_keyboard_mapping_keysyms(reply),
           length * sizeof(*keymap));
  free(reply);
}

static bool find_keycode(xcb_keysym_t keysym,
                         xcb_keycode_t* keycode,
                         bool* shift) {
  if (!keymap)
    return false;
  for (int column = 0; column < 2 && column < keysyms_per_keycode; column++) {
    for (int i = 0; i < keycode_count; i++) {
      if (keymap[i * keysyms_per_keycode + column] == keysym) {
        *keycode = min_keycode + i;
        *shift = column == 1;
        return true;
      }
    }
  }
  return false;
}

// Caret, for the candidate window: the spot (the composition's baseline,
// in the focus window), in the top-level window's coordinates; sommelier
// scales it to the surface's.
static void send_caret(struct context* c) {
  if (!c->active)
    return;
  uint32_t mask = xcb_im_input_context_get_preedit_attr_mask(c->ic);
  const xcb_im_preedit_attr_t* preedit =
      xcb_im_input_context_get_preedit_attr(c->ic);
  int x, y, height = 16;
  if (mask & XCB_XIM_XNSpotLocation_MASK) {
    x = preedit->spot_location.x;
    y = preedit->spot_location.y;
    if ((mask & XCB_XIM_XNLineSpace_MASK) && preedit->line_space > 0 &&
        preedit->line_space < 256)
      height = preedit->line_space;
  } else if (mask & XCB_XIM_XNArea_MASK) {
    x = preedit->area.x;
    y = preedit->area.y + preedit->area.height;
    if (preedit->area.height > 0 && preedit->area.height < 256)
      height = preedit->area.height;
  } else {
    // No spot: the bottom left of the focus window, when it is a text
    // field's own window; the top left of the top-level window otherwise.
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(conn, xcb_get_geometry(conn, c->focus), NULL);
    if (!geometry)
      return;
    x = 0;
    y = c->focus != c->toplevel && geometry->height < 200 ? geometry->height
                                                           : height;
    free(geometry);
  }
  if (c->focus != c->toplevel) {
    xcb_translate_coordinates_reply_t* translated =
        xcb_translate_coordinates_reply(
            conn,
            xcb_translate_coordinates(conn, c->focus, c->toplevel, x, y),
            NULL);
    if (translated) {
      x = translated->dst_x;
      y = translated->dst_y;
      free(translated);
    }
  }
  DEBUG("caret %d,%d height %d", x, y - height, height);
  zwp_text_input_v1_set_cursor_rectangle(c->slot->text_input, x, y - height, 1,
                                         height);
}

// The input method's events.

static struct context* owner(void* data) {
  return ((struct slot*)data)->owner;
}

static void on_enter(void* data,
                     struct zwp_text_input_v1* text_input,
                     struct wl_surface* surface) {}

static void on_leave(void* data, struct zwp_text_input_v1* text_input) {
  struct context* c = owner(data);
  DEBUG("leave");
  if (c)
    end_preedit(c);
}

static void on_modifiers_map(void* data,
                             struct zwp_text_input_v1* text_input,
                             struct wl_array* map) {
  struct slot* slot = data;
  static const struct {
    const char* name;
    uint32_t mask;
  } kModifiers[] = {
      {"Shift", XCB_MOD_MASK_SHIFT}, {"Lock", XCB_MOD_MASK_LOCK},
      {"Control", XCB_MOD_MASK_CONTROL}, {"Mod1", XCB_MOD_MASK_1},
      {"Mod2", XCB_MOD_MASK_2}, {"Mod3", XCB_MOD_MASK_3},
      {"Mod4", XCB_MOD_MASK_4}, {"Mod5", XCB_MOD_MASK_5},
  };
  slot->modifier_count = 0;
  const char* name = map->data;
  const char* end = name + map->size;
  while (name < end && slot->modifier_count < 32) {
    uint32_t mask = 0;
    for (size_t i = 0; i < sizeof(kModifiers) / sizeof(kModifiers[0]); i++) {
      if (!strcmp(name, kModifiers[i].name))
        mask = kModifiers[i].mask;
    }
    slot->modifier_masks[slot->modifier_count++] = mask;
    name += strlen(name) + 1;
  }
}

static void on_input_panel_state(void* data,
                                 struct zwp_text_input_v1* text_input,
                                 uint32_t state) {}

static void on_preedit_string(void* data,
                              struct zwp_text_input_v1* text_input,
                              uint32_t serial,
                              const char* text,
                              const char* commit) {
  struct context* c = owner(data);
  if (!c)
    return;
  DEBUG("preedit \"%s\"", text);
  free(c->preedit);
  c->preedit = strdup(text);
  if (c->inline_preedit)
    draw_preedit(c, text);
  c->style_count = 0;
  c->preedit_cursor = -1;
}

static void on_preedit_styling(void* data,
                               struct zwp_text_input_v1* text_input,
                               uint32_t index,
                               uint32_t length,
                               uint32_t style) {
  struct context* c = owner(data);
  if (!c || c->style_count >= 16)
    return;
  c->styles[c->style_count].index = index;
  c->styles[c->style_count].length = length;
  c->styles[c->style_count].style = style;
  c->style_count++;
}

static void on_preedit_cursor(void* data,
                              struct zwp_text_input_v1* text_input,
                              int32_t index) {
  struct context* c = owner(data);
  if (c)
    c->preedit_cursor = index;
}

static void on_commit_string(void* data,
                             struct zwp_text_input_v1* text_input,
                             uint32_t serial,
                             const char* text) {
  struct context* c = owner(data);
  if (!c)
    return;
  DEBUG("commit \"%s\"", text);
  free(c->preedit);
  c->preedit = NULL;
  commit_text(c, text);
}

static void on_cursor_position(void* data,
                               struct zwp_text_input_v1* text_input,
                               int32_t index,
                               int32_t anchor) {}

// Surrounding text is not supported (set_surrounding_text_support), so the
// input method does not delete any.
static void on_delete_surrounding_text(void* data,
                                       struct zwp_text_input_v1* text_input,
                                       int32_t index,
                                       uint32_t length) {}

// A key the input method hands back (InsertChar for a key it handled):
// characters as text, other keys as key events forwarded to the app.
static void on_keysym(void* data,
                      struct zwp_text_input_v1* text_input,
                      uint32_t serial,
                      uint32_t time,
                      uint32_t sym,
                      uint32_t state,
                      uint32_t modifiers) {
  struct context* c = owner(data);
  if (!c)
    return;
  bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
  uint32_t x_state = 0;
  for (int i = 0; i < c->slot->modifier_count; i++) {
    if (modifiers & (1u << i))
      x_state |= c->slot->modifier_masks[i];
  }
  DEBUG("keysym 0x%x %s state 0x%x", sym, pressed ? "pressed" : "released",
        x_state);
  char utf8[8];
  if (!(x_state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1)) &&
      xkb_keysym_to_utf8(sym, utf8, sizeof(utf8)) > 1 &&
      (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f) {
    if (pressed)
      commit_text(c, utf8);
    return;
  }
  xcb_keycode_t keycode;
  bool shift;
  if (!find_keycode(sym, &keycode, &shift)) {
    DEBUG("no keycode for keysym 0x%x", sym);
    return;
  }
  if (shift)
    x_state |= XCB_MOD_MASK_SHIFT;
  xcb_key_press_event_t event = {0};
  event.response_type = pressed ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
  event.detail = keycode;
  event.time = XCB_CURRENT_TIME;
  event.root = screen->root;
  event.event = c->focus;
  event.child = XCB_WINDOW_NONE;
  event.state = x_state;
  event.same_screen = 1;
  xcb_im_forward_event(im, c->ic, &event);
}

static void on_language(void* data,
                        struct zwp_text_input_v1* text_input,
                        uint32_t serial,
                        const char* language) {}

static void on_text_direction(void* data,
                              struct zwp_text_input_v1* text_input,
                              uint32_t serial,
                              uint32_t direction) {}

static const struct zwp_text_input_v1_listener text_input_listener = {
    .enter = on_enter,
    .leave = on_leave,
    .modifiers_map = on_modifiers_map,
    .input_panel_state = on_input_panel_state,
    .preedit_string = on_preedit_string,
    .preedit_styling = on_preedit_styling,
    .preedit_cursor = on_preedit_cursor,
    .commit_string = on_commit_string,
    .cursor_position = on_cursor_position,
    .delete_surrounding_text = on_delete_surrounding_text,
    .keysym = on_keysym,
    .language = on_language,
    .text_direction = on_text_direction,
};

static void on_set_preedit_region(void* data,
                                  struct zcr_extended_text_input_v1* extended,
                                  int32_t index,
                                  uint32_t length) {}

static void on_clear_grammar_fragments(
    void* data,
    struct zcr_extended_text_input_v1* extended,
    uint32_t start,
    uint32_t end) {}

static void on_add_grammar_fragment(void* data,
                                    struct zcr_extended_text_input_v1* extended,
                                    uint32_t start,
                                    uint32_t end,
                                    const char* suggestion) {}

static void on_set_autocorrect_range(
    void* data,
    struct zcr_extended_text_input_v1* extended,
    uint32_t start,
    uint32_t end) {}

static void on_set_virtual_keyboard_occluded_bounds(
    void* data,
    struct zcr_extended_text_input_v1* extended,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height) {}

// The input method keeps the composition as typed (focus moving away, for
// one): commit it.
static void on_confirm_preedit(void* data,
                               struct zcr_extended_text_input_v1* extended,
                               uint32_t selection_behavior) {
  struct context* c = owner(data);
  if (!c || !c->preedit)
    return;
  DEBUG("confirm \"%s\"", c->preedit);
  char* text = c->preedit;
  c->preedit = NULL;
  commit_text(c, text);
  free(text);
}

static const struct zcr_extended_text_input_v1_listener extended_listener = {
    .set_preedit_region = on_set_preedit_region,
    .clear_grammar_fragments = on_clear_grammar_fragments,
    .add_grammar_fragment = on_add_grammar_fragment,
    .set_autocorrect_range = on_set_autocorrect_range,
    .set_virtual_keyboard_occluded_bounds =
        on_set_virtual_keyboard_occluded_bounds,
    .confirm_preedit = on_confirm_preedit,
};

static struct slot* take_slot(struct context* c) {
  struct slot* slot = free_slots;
  if (slot) {
    free_slots = slot->next_free;
  } else {
    slot = calloc(1, sizeof(*slot));
    if (!slot)
      return NULL;
    slot->text_input =
        zwp_text_input_manager_v1_create_text_input(text_input_manager);
    zwp_text_input_v1_add_listener(slot->text_input, &text_input_listener,
                                   slot);
    if (text_input_extension) {
      slot->extended = zcr_text_input_extension_v1_get_extended_text_input(
          text_input_extension, slot->text_input);
      zcr_extended_text_input_v1_add_listener(slot->extended,
                                              &extended_listener, slot);
    }
    slot->crostini = zcr_text_input_crostini_manager_v1_get_text_input_crostini(
        crostini_manager, slot->text_input);
  }
  slot->owner = c;
  slot->next_free = NULL;
  return slot;
}

static void release_slot(struct slot* slot) {
  slot->owner = NULL;
  slot->next_free = free_slots;
  free_slots = slot;
}

// The app's input context.

static void focus_in(struct context* c) {
  c->focus = focus_window(c);
  c->toplevel = find_toplevel(c->focus);
  if (!c->toplevel) {
    DEBUG("focus: window 0x%x has no top-level window", c->focus);
    return;
  }
  if (!c->slot)
    c->slot = take_slot(c);
  if (!c->slot)
    return;
  uint32_t style = xcb_im_input_context_get_input_style(c->ic);
  c->inline_preedit = style & XCB_IM_PreeditCallbacks;
  DEBUG("focus: window 0x%x, style 0x%x", c->toplevel, style);
  zcr_text_input_crostini_v1_activate_x11(c->slot->crostini, seat,
                                          c->toplevel);
  c->active = true;
  if (c->slot->extended &&
      extension_version >=
          ZCR_EXTENDED_TEXT_INPUT_V1_SET_INPUT_TYPE_SINCE_VERSION) {
    zcr_extended_text_input_v1_set_input_type(
        c->slot->extended, ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_TYPE_TEXT,
        ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_MODE_DEFAULT,
        ZCR_EXTENDED_TEXT_INPUT_V1_INPUT_FLAGS_NONE,
        ZCR_EXTENDED_TEXT_INPUT_V1_LEARNING_MODE_ENABLED,
        c->inline_preedit
            ? ZCR_EXTENDED_TEXT_INPUT_V1_INLINE_COMPOSITION_SUPPORT_SUPPORTED
            : ZCR_EXTENDED_TEXT_INPUT_V1_INLINE_COMPOSITION_SUPPORT_UNSUPPORTED);
  }
  if (c->slot->extended &&
      extension_version >=
          ZCR_EXTENDED_TEXT_INPUT_V1_SET_SURROUNDING_TEXT_SUPPORT_SINCE_VERSION) {
    zcr_extended_text_input_v1_set_surrounding_text_support(
        c->slot->extended,
        ZCR_EXTENDED_TEXT_INPUT_V1_SURROUNDING_TEXT_SUPPORT_UNSUPPORTED);
  }
  send_caret(c);
}

static void focus_out(struct context* c) {
  DEBUG("unfocus");
  if (!c->active)
    return;
  zwp_text_input_v1_deactivate(c->slot->text_input, seat);
  c->active = false;
}

// A new focus window (and maybe top-level window), or caret.
static void values_changed(struct context* c) {
  if (!c || !c->active)
    return;
  if (focus_window(c) != c->focus)
    focus_in(c);
  else
    send_caret(c);
}

static void free_context(void* data) {
  struct context* c = data;
  if (c->slot) {
    if (c->active)
      zwp_text_input_v1_deactivate(c->slot->text_input, seat);
    release_slot(c->slot);
  }
  free(c->preedit);
  free(c);
}

static void xim_callback(xcb_im_t* im,
                         xcb_im_client_t* client,
                         xcb_im_input_context_t* ic,
                         const xcb_im_packet_header_fr_t* hdr,
                         void* frame,
                         void* arg,
                         void* user_data) {
  struct context* c = ic ? xcb_im_input_context_get_data(ic) : NULL;
  switch (hdr->major_opcode) {
    case XCB_XIM_CREATE_IC:
      c = calloc(1, sizeof(*c));
      if (!c)
        return;
      c->ic = ic;
      c->preedit_cursor = -1;
      xcb_im_input_context_set_data(ic, c, free_context);
      DEBUG("new input context, style 0x%x",
            xcb_im_input_context_get_input_style(ic));
      break;
    case XCB_XIM_SET_IC_FOCUS:
      if (c)
        focus_in(c);
      break;
    case XCB_XIM_UNSET_IC_FOCUS:
      if (c)
        focus_out(c);
      break;
    case XCB_XIM_SET_IC_VALUES:
      values_changed(c);
      break;
    case XCB_XIM_EXTENSION:
      // XIM_EXT_MOVE: Xlib's message for a caret that moved.
      if (hdr->minor_opcode == XCB_XIM_EXT_MOVE)
        values_changed(c);
      break;
    case XCB_XIM_RESET_IC:
      // The app drops its composition; the reply commits nothing.
      if (c && c->slot) {
        end_preedit(c);
        zwp_text_input_v1_reset(c->slot->text_input);
        free(c->preedit);
        c->preedit = NULL;
      }
      break;
    case XCB_XIM_FORWARD_EVENT:
      // A key the input method let through. Exo sent sommelier the commit
      // it made with it (if any) before the key, which went on to Xwayland
      // and then the app: once sommelier answers, that commit is here and
      // goes to the app first.
      if (ic && arg) {
        wl_display_roundtrip(wl);
        xcb_im_forward_event(im, ic, arg);
      }
      break;
    default:
      break;
  }
}

// Wayland.

static void on_global(void* data,
                      struct wl_registry* registry,
                      uint32_t name,
                      const char* interface,
                      uint32_t version) {
  if (!strcmp(interface, wl_seat_interface.name) && !seat) {
    seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
  } else if (!strcmp(interface, zwp_text_input_manager_v1_interface.name)) {
    text_input_manager = wl_registry_bind(
        registry, name, &zwp_text_input_manager_v1_interface, 1);
  } else if (!strcmp(interface, zcr_text_input_extension_v1_interface.name)) {
    extension_version =
        version < EXTENSION_VERSION ? version : EXTENSION_VERSION;
    text_input_extension =
        wl_registry_bind(registry, name, &zcr_text_input_extension_v1_interface,
                         extension_version);
  } else if (!strcmp(interface,
                     zcr_text_input_crostini_manager_v1_interface.name)) {
    crostini_manager = wl_registry_bind(
        registry, name, &zcr_text_input_crostini_manager_v1_interface, 1);
  }
}

static void on_global_remove(void* data,
                             struct wl_registry* registry,
                             uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove,
};

// sommelier's socket for X11 input methods appears once Xwayland is up.
static bool connect_wayland(const char* display_name) {
  char socket_name[64];
  snprintf(socket_name, sizeof(socket_name), "DISPLAY-%s-wl", display_name);
  for (int i = 0; i < CONNECT_TIMEOUT_S * 2 && !wl; i++) {
    wl = wl_display_connect(socket_name);
    if (!wl) {
      struct timespec half_second = {0, 500000000};
      nanosleep(&half_second, NULL);
    }
  }
  if (!wl) {
    log_msg("no Wayland socket %s", socket_name);
    return false;
  }
  struct wl_registry* registry = wl_display_get_registry(wl);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(wl);
  if (!seat || !text_input_manager || !crostini_manager) {
    log_msg("%s has no X11 text input", socket_name);
    return false;
  }
  return true;
}

// XSETTINGS (freedesktop.org's spec): the settings, in the manager window's
// _XSETTINGS_SETTINGS property; the manager owns _XSETTINGS_S<screen> and
// says so on the root window (MANAGER).
static void append(uint8_t** p, const void* data, size_t length) {
  memcpy(*p, data, length);
  *p += (length + 3) & ~3u;
}

static xcb_atom_t intern(const char* name) {
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
      conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
  xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return atom;
}

static void serve_xsettings(int screen_number) {
  char selection_name[32];
  snprintf(selection_name, sizeof(selection_name), "_XSETTINGS_S%d",
           screen_number);
  xcb_atom_t selection = intern(selection_name);
  xcb_atom_t settings_atom = intern("_XSETTINGS_SETTINGS");
  xcb_atom_t manager = intern("MANAGER");
  xcb_get_selection_owner_reply_t* owner = xcb_get_selection_owner_reply(
      conn, xcb_get_selection_owner(conn, selection), NULL);
  bool taken = owner && owner->owner != XCB_WINDOW_NONE;
  free(owner);
  if (taken) {
    log_msg("%s has a manager already: GTK on X11 picks its own module",
            selection_name);
    return;
  }

  static const char kName[] = "Gtk/IMModule";
  static const char kValue[] = "xim";
  uint8_t data[64] = {0};
  uint8_t* p = data;
  uint32_t one = 1, serial = 0, length;
  uint16_t name_length = sizeof(kName) - 1;
  uint8_t header[4] = {0};
  const uint16_t endian_test = 1;
  header[0] = *(const uint8_t*)&endian_test ? 0 /* LSBFirst */ : 1;
  append(&p, header, 4);
  append(&p, &serial, 4);  // the settings' serial
  append(&p, &one, 4);     // one setting
  uint8_t type_and_pad[2] = {1 /* string */, 0};
  memcpy(p, type_and_pad, 2);
  memcpy(p + 2, &name_length, 2);
  p += 4;
  append(&p, kName, name_length);
  append(&p, &serial, 4);  // last changed
  length = sizeof(kValue) - 1;
  append(&p, &length, 4);
  append(&p, kValue, length);

  xcb_window_t window = xcb_generate_id(conn);
  xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root, -1, -1,
                    1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
                    0, NULL);
  xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, settings_atom,
                      settings_atom, 8, p - data, data);
  xcb_set_selection_owner(conn, window, selection, XCB_CURRENT_TIME);
  xcb_client_message_event_t event = {0};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = screen->root;
  event.type = manager;
  event.data.data32[0] = XCB_CURRENT_TIME;
  event.data.data32[1] = selection;
  event.data.data32[2] = window;
  xcb_send_event(conn, 0, screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                 (const char*)&event);
  DEBUG("XSETTINGS: Gtk/IMModule=xim");
}

static volatile sig_atomic_t stopping;

static void on_signal(int signal) {
  stopping = 1;
}

int main(int argc, char** argv) {
  debug = getenv("NICKEL_XIM_DEBUG") && *getenv("NICKEL_XIM_DEBUG") == '1';

  // The job starts with sommelier, before Xwayland is up.
  int screen_number = 0;
  for (int i = 0; i < CONNECT_TIMEOUT_S * 2; i++) {
    conn = xcb_connect(NULL, &screen_number);
    if (!xcb_connection_has_error(conn))
      break;
    xcb_disconnect(conn);
    conn = NULL;
    struct timespec half_second = {0, 500000000};
    nanosleep(&half_second, NULL);
  }
  if (!conn) {
    log_msg("cannot open the X display");
    return 1;
  }
  screen = xcb_aux_get_screen(conn, screen_number);
  wm_state_atom = intern("WM_STATE");
  if (!screen || wm_state_atom == XCB_ATOM_NONE) {
    log_msg("cannot set up the X display");
    return 1;
  }

  const char* display = getenv("DISPLAY");
  if (!display || !connect_wayland(display))
    return 1;

  // The window clients find the server by (@server=nickel).
  xcb_window_t server_window = xcb_generate_id(conn);
  xcb_create_window(conn, XCB_COPY_FROM_PARENT, server_window, screen->root, 0,
                    0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                    XCB_COPY_FROM_PARENT, 0, NULL);

  // On-the-spot (the app draws the composition: GTK, Wine, Java's active
  // input context, SDL 3 when asked), over-the-spot (xterm, urxvt) and root
  // (SDL, Java's passive input context, which needs it): ChromeOS shows the
  // composition of the last two in its candidate window. Not off-the-spot
  // (PreeditArea), as fcitx5 leaves it out: nothing would show where the app
  // expects the composition.
  static uint32_t styles[] = {
      XCB_IM_PreeditCallbacks | XCB_IM_StatusCallbacks,
      XCB_IM_PreeditCallbacks | XCB_IM_StatusArea,
      XCB_IM_PreeditCallbacks | XCB_IM_StatusNothing,
      XCB_IM_PreeditCallbacks | XCB_IM_StatusNone,
      XCB_IM_PreeditPosition | XCB_IM_StatusArea,
      XCB_IM_PreeditPosition | XCB_IM_StatusNothing,
      XCB_IM_PreeditPosition | XCB_IM_StatusNone,
      XCB_IM_PreeditNothing | XCB_IM_StatusNothing,
      XCB_IM_PreeditNothing | XCB_IM_StatusNone,
      XCB_IM_PreeditNone | XCB_IM_StatusNone,
  };
  xcb_im_styles_t input_styles = {sizeof(styles) / sizeof(styles[0]), styles};
  static char compound_text[] = "COMPOUND_TEXT";
  static xcb_im_encoding_t encodings[] = {compound_text};
  xcb_im_encodings_t encoding_list = {1, encodings};

  xcb_compound_text_init();
  // Clients forward key presses and releases (releases too, or one would
  // overtake its press).
  im = xcb_im_create(conn, screen_number, server_window, SERVER_NAME,
                     XCB_IM_ALL_LOCALES, &input_styles, NULL, NULL,
                     &encoding_list,
                     XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE,
                     xim_callback, NULL);
  if (!im || !xcb_im_open_im(im)) {
    log_msg("cannot become the input method server %s (another running?)",
            SERVER_NAME);
    return 1;
  }
  load_keymap();
  serve_xsettings(screen_number);
  xcb_flush(conn);
  log_msg("serving @im=%s on %s", SERVER_NAME, display);

  struct sigaction action = {0};
  action.sa_handler = on_signal;
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);

  struct pollfd fds[2] = {
      {.fd = xcb_get_file_descriptor(conn), .events = POLLIN},
      {.fd = wl_display_get_fd(wl), .events = POLLIN},
  };
  while (!stopping) {
    // Events either side has queued: handling one side's can make round
    // trips to the other's, which queue more.
    if (wl_display_dispatch_pending(wl) < 0) {
      log_msg("lost the Wayland socket");
      return 1;
    }
    xcb_generic_event_t* event;
    while ((event = xcb_poll_for_event(conn))) {
      if ((event->response_type & ~0x80) == XCB_MAPPING_NOTIFY)
        load_keymap();
      xcb_im_filter_event(im, event);
      free(event);
    }
    if (xcb_connection_has_error(conn)) {
      log_msg("lost the X display");
      return 1;
    }
    if (wl_display_prepare_read(wl) != 0)
      continue;
    // Nothing queued: wait for either.
    xcb_flush(conn);
    if (wl_display_flush(wl) < 0 && errno != EAGAIN) {
      wl_display_cancel_read(wl);
      log_msg("lost the Wayland socket");
      return 1;
    }
    if (poll(fds, 2, -1) < 0) {
      wl_display_cancel_read(wl);
      if (errno == EINTR)
        continue;
      return 1;
    }
    if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
      if (wl_display_read_events(wl) < 0) {
        log_msg("lost the Wayland socket");
        return 1;
      }
    } else {
      wl_display_cancel_read(wl);
    }
  }
  xcb_im_close_im(im);
  xcb_im_destroy(im);
  xcb_disconnect(conn);
  return 0;
}
