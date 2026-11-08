/* NickelOS: typing in X11 apps through XIM, as Xlib apps (Wine, Java, SDL,
 * xterm) do it. A small X11 window with an input context in one of the
 * three ways apps draw a composition, printing what the input method server
 * (nickel-xim) sends it. Run inside the session (nickel-session-exec, with
 * XMODIFIERS=@im=nickel); vmtest/xim-test.sh drives it.
 *   x-ime-test STYLE SECONDS
 *     STYLE callbacks  the app draws the composition (on-the-spot: GTK,
 *                      Wine, Java): prints preedit-start, "preedit=TEXT
 *                      caret=N" and preedit-done lines
 *           position   over-the-spot (xterm): the server places it at the
 *                      spot, 20,100 and, once focused, 60,120
 *           root       nothing drawn by the app (SDL)
 *   It prints "im=..." (or "no input method"), "style=...", focus-in and
 *   focus-out, "commit=TEXT" for each text the app receives and "key=NAME"
 *   for other keys, then "text=..." (all it received).
 * Build: vmtest/build-test-client.sh x-ime-test x11. */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char text[4096];

static int preedit_start(XIC ic, XPointer data, XPointer call_data) {
  printf("preedit-start\n");
  fflush(stdout);
  return -1;  // no length limit
}

static void preedit_done(XIC ic, XPointer data, XPointer call_data) {
  printf("preedit-done\n");
  fflush(stdout);
}

static void preedit_draw(XIC ic,
                         XPointer data,
                         XIMPreeditDrawCallbackStruct* draw) {
  const char* string = "";
  if (draw->text && !draw->text->encoding_is_wchar &&
      draw->text->string.multi_byte)
    string = draw->text->string.multi_byte;
  printf("preedit=%s caret=%d chg=%d+%d\n", string, draw->caret,
         draw->chg_first, draw->chg_length);
  fflush(stdout);
}

static void preedit_caret(XIC ic,
                          XPointer data,
                          XIMPreeditCaretCallbackStruct* caret) {}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: x-ime-test callbacks|position|root SECONDS\n");
    return 2;
  }
  const char* style_name = argv[1];
  int seconds = atoi(argv[2]);
  setlocale(LC_ALL, "");
  if (!XSupportsLocale())
    printf("locale not supported by Xlib\n");
  XSetLocaleModifiers("");
  Display* display = XOpenDisplay(NULL);
  if (!display) {
    printf("no display\n");
    return 1;
  }
  int screen = DefaultScreen(display);
  Window window = XCreateSimpleWindow(
      display, RootWindow(display, screen), 0, 0, 480, 240, 0,
      BlackPixel(display, screen), WhitePixel(display, screen));
  XStoreName(display, window, "x-ime-test");
  XClassHint class_hint = {"x-ime-test", "x-ime-test"};
  XSetClassHint(display, window, &class_hint);

  XIM im = XOpenIM(display, NULL, NULL, NULL);
  if (!im) {
    printf("no input method\n");
    return 1;
  }
  printf("im=%s\n", XLocaleOfIM(im));

  XIMStyle style;
  XVaNestedList preedit_attributes = NULL;
  XIMCallback start = {NULL, (XIMProc)preedit_start};
  XIMCallback done = {NULL, (XIMProc)preedit_done};
  XIMCallback draw = {NULL, (XIMProc)preedit_draw};
  XIMCallback caret = {NULL, (XIMProc)preedit_caret};
  XPoint spot = {20, 100};
  XFontSet font_set = NULL;
  if (!strcmp(style_name, "callbacks")) {
    style = XIMPreeditCallbacks | XIMStatusNothing;
    preedit_attributes = XVaCreateNestedList(
        0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done,
        XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret, NULL);
  } else if (!strcmp(style_name, "position")) {
    style = XIMPreeditPosition | XIMStatusNothing;
    char** missing;
    int missing_count;
    char* default_string;
    font_set = XCreateFontSet(display, "fixed,*", &missing, &missing_count,
                              &default_string);
    preedit_attributes = XVaCreateNestedList(0, XNSpotLocation, &spot,
                                             XNFontSet, font_set, NULL);
  } else {
    style = XIMPreeditNothing | XIMStatusNothing;
  }
  XIC ic = preedit_attributes
               ? XCreateIC(im, XNInputStyle, style, XNClientWindow, window,
                           XNFocusWindow, window, XNPreeditAttributes,
                           preedit_attributes, NULL)
               : XCreateIC(im, XNInputStyle, style, XNClientWindow, window,
                           XNFocusWindow, window, NULL);
  if (!ic) {
    printf("no input context for style %s\n", style_name);
    return 1;
  }
  printf("style=%s\n", style_name);
  long filter_events = 0;
  XGetICValues(ic, XNFilterEvents, &filter_events, NULL);
  XSelectInput(display, window,
               KeyPressMask | KeyReleaseMask | FocusChangeMask |
                   ExposureMask | StructureNotifyMask | filter_events);
  XMapWindow(display, window);
  fflush(stdout);

  time_t end = time(NULL) + seconds;
  struct pollfd fd = {ConnectionNumber(display), POLLIN, 0};
  while (time(NULL) < end) {
    if (!XPending(display)) {
      poll(&fd, 1, 200);
      continue;
    }
    XEvent event;
    XNextEvent(display, &event);
    if (XFilterEvent(&event, None))
      continue;
    switch (event.type) {
      case FocusIn:
        XSetICFocus(ic);
        printf("focus-in\n");
        if (font_set) {
          // The caret moves, as in a terminal: Xlib sends XIM_EXT_MOVE.
          XPoint moved = {60, 120};
          XVaNestedList moved_attributes =
              XVaCreateNestedList(0, XNSpotLocation, &moved, NULL);
          XSetICValues(ic, XNPreeditAttributes, moved_attributes, NULL);
          XFree(moved_attributes);
        }
        break;
      case FocusOut:
        XUnsetICFocus(ic);
        printf("focus-out\n");
        break;
      case KeyPress: {
        char buffer[256];
        KeySym keysym = NoSymbol;
        Status status;
        int length = Xutf8LookupString(ic, &event.xkey, buffer,
                                       sizeof(buffer) - 1, &keysym, &status);
        if (status == XLookupChars || status == XLookupBoth) {
          buffer[length] = 0;
          if (length && (unsigned char)buffer[0] >= 0x20) {
            printf("commit=%s\n", buffer);
            strncat(text, buffer, sizeof(text) - strlen(text) - 1);
            break;
          }
        }
        if (status == XLookupKeySym || status == XLookupBoth) {
          const char* name = XKeysymToString(keysym);
          printf("key=%s\n", name ? name : "?");
        }
        break;
      }
      default:
        break;
    }
    fflush(stdout);
  }
  printf("text=%s\n", text);
  XDestroyIC(ic);
  XCloseIM(im);
  if (font_set)
    XFreeFontSet(display, font_set);
  XCloseDisplay(display);
  return 0;
}
