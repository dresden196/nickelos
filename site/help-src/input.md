title: Typing and keyboard
order: 90
summary: Input methods in Linux apps on NickelOS (text-input-v3 on Wayland, the nickel-xim XIM server on X11), Chinese, Japanese and Korean, middle-click paste, global shortcuts and shortcut keys for older X11 apps.
---
# Typing and keyboard

Typing in other languages, pasting, and keyboard shortcuts in Linux apps.

## Input methods

ChromeOS's input methods (Chinese, Japanese, Korean, and every other language and layout ChromeOS has) work in Linux apps as well as in Chrome. You add them in Settings, the usual ChromeOS way, and switch between them with Ctrl + Space or from the shelf. Composing happens in place, and the candidate list shows up next to the text cursor.

How a Linux app gets them depends on how it draws:

| App | How it types | Covers |
|---|---|---|
| Wayland apps | `text-input-v3`, added to Exo by NickelOS | GTK 3 and 4, Qt 6, Electron 38 and newer |
| X11 apps | **nickel-xim**, NickelOS's XIM server (`XMODIFIERS=@im=nickel`) | Everything that speaks XIM: plain Xlib apps, Wine and Proton, Java, SDL 2 and 3, xterm, GTK 2 and 3, Chromium and Electron on X11 |

You don't need `GTK_IM_MODULE` or `QT_IM_MODULE`, and you shouldn't set them. GTK picks its input module per display. On Wayland it gets `gtk-im-module=wayland` from the settings portal, and on X11 it gets `Gtk/IMModule=xim` from XSETTINGS (nickel-xim is the X display's XSETTINGS manager too). Forcing `GTK_IM_MODULE=wayland` breaks GTK 3 apps on X11.

nickel-xim is built on xcb-imdkit, fcitx5's XIM library. It reaches ChromeOS's input methods through sommelier, the same way Crostini's apps do inside their VM.

### Korean

ChromiumOS's Korean input used a Native Client module, and Chrome doesn't run Native Client anymore, so Korean typing gave nothing. NickelOS replaces it with a JavaScript port of libhangul's input engine. It has all six layouts (2-set, 3-set 390, 3-set final, 3-set no-shift, Ahnmatae, Romaja) and Hanja conversion.

### Japanese

Japanese is Google's Mozc engine. ChromiumOS's Japanese input was Native Client too, and was never in ChromiumOS images. NickelOS builds Mozc's engine for WebAssembly and runs it inside ChromeOS's Japanese input method, dictionary included. What you type is learned and kept locally.

### Chinese

Pinyin, Zhuyin and Cangjie are ChromeOS's own and work as they do on any Chromebook.

## Middle-click paste

Select text in a Linux app and middle-click in another to paste it, as on any Linux desktop. Exo has the primary selection protocol for it.

## Copy and paste

The clipboard is shared between Linux apps and ChromeOS, text and files both (see [Your files](/help/files/#moving-files-between-files-and-apps)).

## Global shortcuts

An app that wants a key combination while you're working in other windows (push-to-talk, a screen recorder's start and stop) asks through the GlobalShortcuts portal:

- ChromeOS asks "Allow keyboard shortcuts?" and lists each shortcut with its keys. Select one to give it different keys.
- Keys must be allowed the way ChromeOS's own shortcuts are: with Ctrl, Alt or Search, or a function key. ChromeOS's own shortcuts and other apps' stay theirs.
- The answer is remembered for the app.
- A shortcut's press and release both reach the app, so push-to-talk works. The window you're typing in doesn't get that key. Nothing reaches the app while the screen is locked.

## Shortcut keys for older apps

Some X11 apps don't use the portal. They watch the keyboard themselves, the official Discord client among them. Under Xwayland they only see keys while one of their own windows has the focus, which kills push-to-talk.

**Settings > Apps > Linux apps > Shortcut keys for older Linux apps** fixes that, and it's on by default. While no X11 window has the focus, NickelOS passes these to Xwayland:

- modifier keys (Ctrl, Alt, Shift, Search; not AltGr),
- keys pressed together with Ctrl, Alt or Search,
- function keys.

It never passes plain typing, and nothing while the screen is locked. When an X11 window does have the focus, keys go to it the normal way, once. `nickel-x11-keys` does this, presenting the keys to Xwayland through XTest.
