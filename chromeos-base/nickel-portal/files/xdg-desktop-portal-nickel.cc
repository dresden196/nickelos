// Distributed under the terms of the GNU General Public License v2
//
// xdg-desktop-portal-nickel: NickelOS's backend for xdg-desktop-portal, so
// that Linux apps, Flatpak and native, fit into ChromeOS.
//
// Settings (org.freedesktop.impl.portal.Settings): ChromeOS's dark or light
// mode and accent color, which Chrome reports to nickel-appd
// (org.nickelos.Apps on the system bus), and the fonts of NickelOS's UI.
//   org.freedesktop.appearance   color-scheme (u: 1 dark, 2 light, 0 not
//                                known yet), accent-color ((ddd), sRGB 0-1;
//                                out of range while not known), contrast (u)
//   org.gnome.desktop.interface  color-scheme, gtk-theme, font-name,
//                                document-font-name, monospace-font-name,
//                                gtk-im-module, icon-theme, cursor-theme,
//                                font-antialiasing, font-hinting,
//                                font-rgba-order (s), cursor-size (i),
//                                text-scaling-factor (d), enable-animations,
//                                gtk-enable-primary-paste, overlay-scrolling,
//                                cursor-blink (b)
//   org.gnome.desktop.wm.preferences  button-layout (s)
// GTK 4/libadwaita and Qt apps follow org.freedesktop.appearance; GTK 3 apps
// read their theme and fonts from the gnome keys. GTK takes every setting it
// knows from the portal, and its fallbacks for keys the portal lacks are not
// GNOME's: the screen's DPI was 0 without text-scaling-factor (WebKitGTK then
// zoomed pages to almost nothing: GNOME Web, Sober's sign-in page) and the
// icon theme "gnome", which no runtime has (missing icons). gtk-im-module=wayland: GTK
// 3 and 4 on Wayland type through ChromeOS's input methods (Exo's
// text-input-v3); left to choose, GTK takes the ibus or fcitx module Flatpak
// runtimes ship, with no daemon behind it. A setting, not GTK_IM_MODULE:
// GTK 3 on X11 would load the Wayland module too and crash, and takes its
// own setting from XSETTINGS (nickel-xim: xim). Changes are announced with
// SettingChanged, so apps switch along with ChromeOS.
//
// FileChooser (org.freedesktop.impl.portal.FileChooser): Open and Save
// dialogs are ChromeOS's own file picker. The request goes to Chrome as
// vm_applications' SelectFile for VM "nickel" (the call Crostini's apps use),
// with the portal request's handle as token; Chrome (patch 0014) answers
// through nickel-appd's FileSelected signal with file: URLs, none if the user
// canceled. Filters become Chrome's extension lists; MIME type filters have
// no extension and are left out ("All files" is always offered).
//
// AppChooser (org.freedesktop.impl.portal.AppChooser): when an app opens a
// file or link (OpenURI portal) and the portal would ask which app to use,
// the answer is always org.nickelos.ChromeOS (nickel-open): ChromeOS opens it
// with the app it opens that kind of file with, the user's default, as the
// Files app does. (xdg-desktop-portal offers OpenURI only with an
// AppChooser; web and mail links go straight to their default handler,
// which nickelos-mimeapps.list makes the same.)
//
// ScreenCast (org.freedesktop.impl.portal.ScreenCast): the screen or a
// window, chosen in ChromeOS's own picker, as a PipeWire stream; and
// RemoteDesktop (org.freedesktop.impl.portal.RemoteDesktop): keyboard and
// pointer control, once the user allows it (screencast.cc).
//
// Notification (org.freedesktop.impl.portal.Notification), and for native
// apps org.freedesktop.Notifications, which this also owns (and is started
// for): notifications become ChromeOS's own (notifications.cc).
//
// GlobalShortcuts (org.freedesktop.impl.portal.GlobalShortcuts): shortcuts
// that work while another window has the focus, as push-to-talk, once the
// user allows them in ChromeOS's dialog (globalshortcuts.cc).
//
// Access (org.freedesktop.impl.portal.Access): whether an app may use the
// camera, asked in ChromeOS's dialog; with it xdg-desktop-portal offers the
// Camera portal (access.cc).
//
// Inhibit (org.freedesktop.impl.portal.Inhibit), and for native apps
// org.freedesktop.ScreenSaver, which this also owns (and is started for):
// keeping the screen on or the device awake, as ChromeOS wake locks
// (inhibit.cc).
//
// Print (org.freedesktop.impl.portal.Print): documents print through
// ChromeOS's print preview (print.cc).
//
// xdg-desktop-portal starts it on the session bus (D-Bus activation) for
// the interfaces nickelos-portals.conf assigns to it.

#include "access.h"
#include "account.h"
#include "background.h"
#include "dynamic_launcher.h"
#include "email.h"
#include "globalshortcuts.h"
#include "inhibit.h"
#include "notifications.h"
#include "print.h"
#include "screenshot.h"
#include "screencast.h"
#include "wallpaper.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gio/gio.h>
#include <glib.h>
#include <syslog.h>
#include <vm_applications/apps.pb.h>

#include <cctype>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr char kBusName[] = "org.freedesktop.impl.portal.desktop.nickel";
constexpr char kObjectPath[] = "/org/freedesktop/portal/desktop";
constexpr char kSettingsInterface[] = "org.freedesktop.impl.portal.Settings";
constexpr char kNotFound[] = "org.freedesktop.portal.Error.NotFound";

constexpr char kAppsName[] = "org.nickelos.Apps";
constexpr char kAppsPath[] = "/org/nickelos/Apps";

constexpr char kFileChooserInterface[] =
    "org.freedesktop.impl.portal.FileChooser";
constexpr char kRequestInterface[] = "org.freedesktop.impl.portal.Request";
constexpr char kAppChooserInterface[] =
    "org.freedesktop.impl.portal.AppChooser";
constexpr char kScreenCastInterface[] =
    "org.freedesktop.impl.portal.ScreenCast";
constexpr char kSessionInterface[] = "org.freedesktop.impl.portal.Session";
constexpr char kRemoteDesktopInterface[] =
    "org.freedesktop.impl.portal.RemoteDesktop";
constexpr char kNotificationInterface[] =
    "org.freedesktop.impl.portal.Notification";
constexpr char kGlobalShortcutsInterface[] =
    "org.freedesktop.impl.portal.GlobalShortcuts";
constexpr char kAccessInterface[] = "org.freedesktop.impl.portal.Access";
constexpr char kInhibitInterface[] = "org.freedesktop.impl.portal.Inhibit";
constexpr char kPrintInterface[] = "org.freedesktop.impl.portal.Print";
constexpr char kScreenshotInterface[] =
    "org.freedesktop.impl.portal.Screenshot";
constexpr char kBackgroundInterface[] =
    "org.freedesktop.impl.portal.Background";
constexpr char kAccountInterface[] = "org.freedesktop.impl.portal.Account";
constexpr char kEmailInterface[] = "org.freedesktop.impl.portal.Email";
constexpr char kDynamicLauncherInterface[] =
    "org.freedesktop.impl.portal.DynamicLauncher";
constexpr char kWallpaperInterface[] =
    "org.freedesktop.impl.portal.Wallpaper";
constexpr char kClipboardInterface[] =
    "org.freedesktop.impl.portal.Clipboard";
// The desktop file id the AppChooser answers with (nickel-appd).
constexpr char kChromeOSHandler[] = "org.nickelos.ChromeOS";
// Host apps' VM and container names in Chrome (nickel-appd).
constexpr char kHostVm[] = "nickel";
constexpr char kHostContainer[] = "host";
// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kFailed = 2;

constexpr char kAppearance[] = "org.freedesktop.appearance";
constexpr char kGnome[] = "org.gnome.desktop.interface";
constexpr char kGnomeWm[] = "org.gnome.desktop.wm.preferences";

// NickelOS's UI fonts (OFL), the ones ChromeOS's own UI uses.
constexpr char kUiFont[] = "Google Sans Flex 11";
constexpr char kMonospaceFont[] = "Google Sans Code 11";

constexpr char kIntrospection[] =
    "<node>"
    "  <interface name='org.freedesktop.impl.portal.Settings'>"
    "    <method name='ReadAll'>"
    "      <arg type='as' name='namespaces' direction='in'/>"
    "      <arg type='a{sa{sv}}' name='value' direction='out'/>"
    "    </method>"
    "    <method name='Read'>"
    "      <arg type='s' name='namespace' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/>"
    "      <arg type='v' name='value' direction='out'/>"
    "    </method>"
    "    <signal name='SettingChanged'>"
    "      <arg type='s' name='namespace'/>"
    "      <arg type='s' name='key'/>"
    "      <arg type='v' name='value'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.FileChooser'>"
    "    <method name='OpenFile'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='SaveFile'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='SaveFiles'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Request'>"
    "    <method name='Close'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.AppChooser'>"
    "    <method name='ChooseApplication'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='as' name='choices' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='UpdateChoices'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='as' name='choices' direction='in'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.ScreenCast'>"
    "    <method name='CreateSession'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='SelectSources'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='Start'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <property name='AvailableSourceTypes' type='u' access='read'/>"
    "    <property name='AvailableCursorModes' type='u' access='read'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.RemoteDesktop'>"
    "    <method name='CreateSession'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='SelectDevices'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='Start'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='NotifyPointerMotion'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='d' name='arg0' direction='in'/>"
    "      <arg type='d' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyPointerMotionAbsolute'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='arg0' direction='in'/>"
    "      <arg type='d' name='arg1' direction='in'/>"
    "      <arg type='d' name='arg2' direction='in'/>"
    "    </method>"
    "    <method name='NotifyPointerButton'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='i' name='arg0' direction='in'/>"
    "      <arg type='u' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyPointerAxis'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='d' name='arg0' direction='in'/>"
    "      <arg type='d' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyPointerAxisDiscrete'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='arg0' direction='in'/>"
    "      <arg type='i' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyKeyboardKeycode'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='i' name='arg0' direction='in'/>"
    "      <arg type='u' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyKeyboardKeysym'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='i' name='arg0' direction='in'/>"
    "      <arg type='u' name='arg1' direction='in'/>"
    "    </method>"
    "    <method name='NotifyTouchDown'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='arg0' direction='in'/>"
    "      <arg type='u' name='arg1' direction='in'/>"
    "      <arg type='d' name='arg2' direction='in'/>"
    "      <arg type='d' name='arg3' direction='in'/>"
    "    </method>"
    "    <method name='NotifyTouchMotion'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='arg0' direction='in'/>"
    "      <arg type='u' name='arg1' direction='in'/>"
    "      <arg type='d' name='arg2' direction='in'/>"
    "      <arg type='d' name='arg3' direction='in'/>"
    "    </method>"
    "    <method name='NotifyTouchUp'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='arg0' direction='in'/>"
    "    </method>"
    "    <method name='ConnectToEIS'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='h' name='fd' direction='out'/>"
    "    </method>"
    "    <property name='AvailableDeviceTypes' type='u' access='read'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Notification'>"
    "    <method name='AddNotification'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='id' direction='in'/>"
    "      <arg type='a{sv}' name='notification' direction='in'/>"
    "    </method>"
    "    <method name='RemoveNotification'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='id' direction='in'/>"
    "    </method>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='s' name='app_id'/>"
    "      <arg type='s' name='id'/>"
    "      <arg type='s' name='action'/>"
    "      <arg type='av' name='parameter'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.GlobalShortcuts'>"
    "    <method name='CreateSession'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='BindShortcuts'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a(sa{sv})' name='shortcuts' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='ListShortcuts'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <signal name='Activated'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='s' name='shortcut_id'/>"
    "      <arg type='t' name='timestamp'/>"
    "      <arg type='a{sv}' name='options'/>"
    "    </signal>"
    "    <signal name='Deactivated'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='s' name='shortcut_id'/>"
    "      <arg type='t' name='timestamp'/>"
    "      <arg type='a{sv}' name='options'/>"
    "    </signal>"
    "    <signal name='ShortcutsChanged'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='a(sa{sv})' name='shortcuts'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Access'>"
    "    <method name='AccessDialog'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='s' name='subtitle' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Session'>"
    "    <method name='Close'/>"
    "    <signal name='Closed'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Inhibit'>"
    "    <method name='Inhibit'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='window' direction='in'/>"
    "      <arg type='u' name='flags' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "    </method>"
    "    <method name='CreateMonitor'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='window' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "    </method>"
    "    <method name='QueryEndResponse'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='a{sv}' name='state'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Print'>"
    "    <method name='PreparePrint'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='a{sv}' name='settings' direction='in'/>"
    "      <arg type='a{sv}' name='page_setup' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='Print'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='h' name='fd' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Screenshot'>"
    "    <method name='Screenshot'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='PickColor'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Background'>"
    "    <method name='GetAppState'>"
    "      <arg type='a{sv}' name='apps' direction='out'/>"
    "    </method>"
    "    <method name='NotifyBackground'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='EnableAutostart'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='b' name='enable' direction='in'/>"
    "      <arg type='as' name='commandline' direction='in'/>"
    "      <arg type='u' name='flags' direction='in'/>"
    "      <arg type='b' name='result' direction='out'/>"
    "    </method>"
    "    <signal name='RunningApplicationsChanged'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Account'>"
    "    <method name='GetUserInformation'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Email'>"
    "    <method name='ComposeEmail'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.DynamicLauncher'>"
    "    <method name='PrepareInstall'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='v' name='icon_v' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "      <arg type='a{sv}' name='results' direction='out'/>"
    "    </method>"
    "    <method name='RequestInstallToken'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "    </method>"
    "    <property name='SupportedLauncherTypes' type='u' access='read'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Clipboard'>"
    "    <method name='RequestClipboard'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "    </method>"
    "    <method name='SetSelection'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "    </method>"
    "    <method name='SelectionWrite'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='u' name='serial' direction='in'/>"
    "      <arg type='h' name='fd' direction='out'/>"
    "    </method>"
    "    <method name='SelectionWriteDone'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='u' name='serial' direction='in'/>"
    "      <arg type='b' name='success' direction='in'/>"
    "    </method>"
    "    <method name='SelectionRead'>"
    "      <arg type='o' name='session_handle' direction='in'/>"
    "      <arg type='s' name='mime_type' direction='in'/>"
    "      <arg type='h' name='fd' direction='out'/>"
    "    </method>"
    "    <signal name='SelectionOwnerChanged'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='a{sv}' name='options'/>"
    "    </signal>"
    "    <signal name='SelectionTransfer'>"
    "      <arg type='o' name='session_handle'/>"
    "      <arg type='s' name='mime_type'/>"
    "      <arg type='u' name='serial'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Wallpaper'>"
    "    <method name='SetWallpaperURI'>"
    "      <arg type='o' name='handle' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='s' name='parent_window' direction='in'/>"
    "      <arg type='s' name='uri' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='u' name='response' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

struct Key {
  const char* ns;
  const char* key;
  bool follows_appearance;
};

constexpr Key kKeys[] = {
    {kAppearance, "color-scheme", true},
    {kAppearance, "accent-color", true},
    {kAppearance, "contrast", false},
    {kGnome, "color-scheme", true},
    {kGnome, "gtk-theme", true},
    {kGnome, "font-name", false},
    {kGnome, "document-font-name", false},
    {kGnome, "monospace-font-name", false},
    {kGnome, "gtk-im-module", false},
    {kGnome, "icon-theme", false},
    {kGnome, "cursor-theme", false},
    {kGnome, "cursor-size", false},
    {kGnome, "text-scaling-factor", false},
    {kGnome, "font-antialiasing", false},
    {kGnome, "font-hinting", false},
    {kGnome, "font-rgba-order", false},
    {kGnome, "enable-animations", false},
    {kGnome, "gtk-enable-primary-paste", false},
    {kGnome, "overlay-scrolling", false},
    {kGnome, "cursor-blink", false},
    {kGnomeWm, "button-layout", false},
};

// A file dialog Chrome shows for an app, by its portal request handle.
struct Dialog {
  GDBusMethodInvocation* invocation = nullptr;
  guint request_object = 0;
  // SaveFiles: the names of the files to save in the folder the user picks.
  std::vector<std::string> names;
};

struct State {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  bool known = false;
  bool dark = false;
  guint32 accent = 0;  // ARGB
  GDBusInterfaceInfo* request_info = nullptr;
  std::map<std::string, Dialog> dialogs;
};

// The value of a setting (floating reference), or nullptr for unknown keys.
GVariant* Value(const State& state, const char* ns, const char* key) {
  if (strcmp(ns, kAppearance) == 0) {
    if (strcmp(key, "color-scheme") == 0) {
      return g_variant_new_uint32(!state.known ? 0 : state.dark ? 1 : 2);
    }
    if (strcmp(key, "accent-color") == 0) {
      if (!state.known) {
        return g_variant_new("(ddd)", -1.0, -1.0, -1.0);
      }
      return g_variant_new("(ddd)", ((state.accent >> 16) & 0xff) / 255.0,
                           ((state.accent >> 8) & 0xff) / 255.0,
                           (state.accent & 0xff) / 255.0);
    }
    if (strcmp(key, "contrast") == 0) {
      return g_variant_new_uint32(0);
    }
  } else if (strcmp(ns, kGnome) == 0) {
    if (strcmp(key, "color-scheme") == 0) {
      return g_variant_new_string(state.dark ? "prefer-dark" : "default");
    }
    if (strcmp(key, "gtk-theme") == 0) {
      return g_variant_new_string(state.dark ? "Adwaita-dark" : "Adwaita");
    }
    if (strcmp(key, "font-name") == 0 ||
        strcmp(key, "document-font-name") == 0) {
      return g_variant_new_string(kUiFont);
    }
    if (strcmp(key, "monospace-font-name") == 0) {
      return g_variant_new_string(kMonospaceFont);
    }
    if (strcmp(key, "gtk-im-module") == 0) {
      return g_variant_new_string("wayland");
    }
    // GNOME's defaults, and NickelOS's cursors (x11-themes/adwaita-cursors,
    // the size ChromeOS's cursor has).
    if (strcmp(key, "icon-theme") == 0 || strcmp(key, "cursor-theme") == 0) {
      return g_variant_new_string("Adwaita");
    }
    if (strcmp(key, "cursor-size") == 0) {
      return g_variant_new_int32(24);
    }
    if (strcmp(key, "text-scaling-factor") == 0) {
      return g_variant_new_double(1.0);
    }
    if (strcmp(key, "font-antialiasing") == 0) {
      return g_variant_new_string("grayscale");
    }
    if (strcmp(key, "font-hinting") == 0) {
      return g_variant_new_string("slight");
    }
    if (strcmp(key, "font-rgba-order") == 0) {
      return g_variant_new_string("rgb");
    }
    // Middle-click paste works in NickelOS (Exo's primary selection).
    if (strcmp(key, "enable-animations") == 0 ||
        strcmp(key, "gtk-enable-primary-paste") == 0 ||
        strcmp(key, "overlay-scrolling") == 0 ||
        strcmp(key, "cursor-blink") == 0) {
      return g_variant_new_boolean(true);
    }
  } else if (strcmp(ns, kGnomeWm) == 0) {
    // The buttons ChromeOS's own windows have.
    if (strcmp(key, "button-layout") == 0) {
      return g_variant_new_string(":minimize,maximize,close");
    }
  }
  return nullptr;
}

// ReadAll's filter: an empty list or an empty string matches every
// namespace; a pattern ending in '*' matches by prefix.
bool Wanted(const char* ns, const char* const* patterns) {
  if (!patterns[0]) {
    return true;
  }
  for (const char* const* p = patterns; *p; ++p) {
    size_t n = strlen(*p);
    if (n == 0 || strcmp(*p, ns) == 0) {
      return true;
    }
    if ((*p)[n - 1] == '*' && strncmp(*p, ns, n - 1) == 0) {
      return true;
    }
  }
  return false;
}

void ReadAll(const State& state,
             GDBusMethodInvocation* invocation,
             GVariant* parameters) {
  g_autofree const gchar** patterns = nullptr;
  g_variant_get(parameters, "(^a&s)", &patterns);
  GVariantBuilder all;
  g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));
  for (const char* ns : {kAppearance, kGnome, kGnomeWm}) {
    if (!Wanted(ns, patterns)) {
      continue;
    }
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
    for (const Key& key : kKeys) {
      if (strcmp(key.ns, ns) == 0) {
        g_variant_builder_add(&values, "{sv}", key.key,
                              Value(state, key.ns, key.key));
      }
    }
    g_variant_builder_add(&all, "{sa{sv}}", ns, &values);
  }
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(a{sa{sv}})", &all));
}

void Read(const State& state,
          GDBusMethodInvocation* invocation,
          GVariant* parameters) {
  const gchar* ns = nullptr;
  const gchar* key = nullptr;
  g_variant_get(parameters, "(&s&s)", &ns, &key);
  GVariant* value = Value(state, ns, key);
  if (!value) {
    g_dbus_method_invocation_return_dbus_error(invocation, kNotFound,
                                               "unknown setting");
    return;
  }
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(v)", value));
}

// A path option (bytestring, "ay"), or "".
std::string PathOption(GVariant* options, const char* key) {
  g_autoptr(GVariant) value =
      g_variant_lookup_value(options, key, G_VARIANT_TYPE_BYTESTRING);
  return value ? g_variant_get_bytestring(value) : "";
}

// The extension a glob names: "*.png" and "*.[pP][nN][gG]" are "png";
// anything else (no extension, wildcards inside) is "".
std::string GlobExtension(const char* glob) {
  if (strncmp(glob, "*.", 2) != 0) {
    return "";
  }
  std::string ext;
  for (const char* c = glob + 2; *c;) {
    if (*c == '[') {
      const char* end = strchr(c, ']');
      if (!end || end == c + 1) {
        return "";
      }
      ext += static_cast<char>(tolower(c[1]));
      c = end + 1;
    } else if (strchr("*?]", *c)) {
      return "";
    } else {
      ext += *c++;
    }
  }
  return ext;
}

// Chrome's extension list: groups separated by '|', each "ext,ext:Name"; a
// leading ',' preselects a group; '*' adds "All files".
std::string Extensions(GVariant* options) {
  g_autoptr(GVariant) filters =
      g_variant_lookup_value(options, "filters", G_VARIANT_TYPE("a(sa(us))"));
  if (!filters) {
    return "";
  }
  g_autoptr(GVariant) current = g_variant_lookup_value(
      options, "current_filter", G_VARIANT_TYPE("(sa(us))"));
  const char* current_name = nullptr;
  if (current) {
    g_variant_get_child(current, 0, "&s", &current_name);
  }
  std::string list;
  GVariantIter filter_iter;
  g_variant_iter_init(&filter_iter, filters);
  const char* name = nullptr;
  GVariant* patterns = nullptr;
  while (g_variant_iter_next(&filter_iter, "(&s@a(us))", &name, &patterns)) {
    std::string exts;
    GVariantIter pattern_iter;
    g_variant_iter_init(&pattern_iter, patterns);
    guint32 type = 0;
    const char* pattern = nullptr;
    while (g_variant_iter_next(&pattern_iter, "(u&s)", &type, &pattern)) {
      std::string ext = type == 0 ? GlobExtension(pattern) : "";
      if (!ext.empty()) {
        exts += (exts.empty() ? "" : ",") + ext;
      }
    }
    g_variant_unref(patterns);
    if (exts.empty()) {
      continue;
    }
    std::string label = name;
    for (char& c : label) {
      if (c == '|' || c == ':' || c == ',') {
        c = ' ';
      }
    }
    list += list.empty() ? "" : "|";
    if (current_name && strcmp(current_name, name) == 0) {
      list += ",";
    }
    list += exts + ":" + label;
  }
  return list.empty() ? "" : list + "|*";
}

void FinishDialog(State* state,
                  const std::string& handle,
                  guint32 response,
                  GVariant* results) {
  auto it = state->dialogs.find(handle);
  if (it == state->dialogs.end()) {
    return;
  }
  if (it->second.request_object) {
    g_dbus_connection_unregister_object(state->session,
                                        it->second.request_object);
  }
  if (!results) {
    results = g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
  }
  g_dbus_method_invocation_return_value(
      it->second.invocation, g_variant_new("(u@a{sv})", response, results));
  state->dialogs.erase(it);
}

void OnRequestMethodCall(GDBusConnection*,
                         const gchar*,
                         const gchar* object_path,
                         const gchar*,
                         const gchar*,
                         GVariant*,
                         GDBusMethodInvocation* invocation,
                         gpointer user_data) {
  // The app gave up on the dialog; Chrome's stays open until the user
  // closes it, and its answer is then dropped.
  FinishDialog(static_cast<State*>(user_data), object_path, kCanceled,
               nullptr);
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

struct SelectFileCall {
  State* state;
  std::string handle;
};

void OnSelectFileSent(GObject* source, GAsyncResult* result, gpointer data) {
  SelectFileCall* call = static_cast<SelectFileCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  if (!reply) {
    syslog(LOG_WARNING, "SelectFile: %s", error->message);
    FinishDialog(call->state, call->handle, kFailed, nullptr);
  }
  delete call;
}

// OpenFile, SaveFile and SaveFiles: ask Chrome for its file dialog.
void ShowFileDialog(State* state,
                    const gchar* method,
                    GDBusMethodInvocation* invocation,
                    GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* app_id = nullptr;
  const gchar* parent_window = nullptr;
  const gchar* title = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s&s@a{sv})", &handle, &app_id,
                &parent_window, &title, &options);
  if (!state->system) {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(u@a{sv})", kFailed,
                      g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr,
                                          0)));
    return;
  }

  vm_tools::apps::SelectFileRequest request;
  request.set_vm_name(kHostVm);
  request.set_container_name(kHostContainer);
  request.set_select_file_token(handle);
  request.set_title(title);
  Dialog dialog;
  dialog.invocation = invocation;
  gboolean multiple = FALSE;
  gboolean directory = FALSE;
  g_variant_lookup(options, "multiple", "b", &multiple);
  g_variant_lookup(options, "directory", "b", &directory);
  if (strcmp(method, "OpenFile") == 0) {
    request.set_type(directory  ? "folder"
                     : multiple ? "open-multi-file"
                                : "open-file");
    request.set_default_path(PathOption(options, "current_folder"));
    if (!directory) {
      request.set_allowed_extensions(Extensions(options));
    }
  } else if (strcmp(method, "SaveFile") == 0) {
    request.set_type("saveas-file");
    std::string file = PathOption(options, "current_file");
    if (file.empty()) {
      const gchar* name = nullptr;
      g_variant_lookup(options, "current_name", "&s", &name);
      std::string folder = PathOption(options, "current_folder");
      if (name) {
        file = folder.empty() ? name : folder + "/" + name;
      } else {
        file = folder;
      }
    }
    request.set_default_path(file);
    request.set_allowed_extensions(Extensions(options));
  } else {
    // SaveFiles: the user picks a folder; the files go in it.
    request.set_type("folder");
    request.set_default_path(PathOption(options, "current_folder"));
    g_autoptr(GVariant) files =
        g_variant_lookup_value(options, "files", G_VARIANT_TYPE("aay"));
    if (files) {
      for (gsize i = 0; i < g_variant_n_children(files); ++i) {
        g_autoptr(GVariant) file = g_variant_get_child_value(files, i);
        g_autofree gchar* name =
            g_path_get_basename(g_variant_get_bytestring(file));
        dialog.names.push_back(name);
      }
    }
  }

  static const GDBusInterfaceVTable kRequestVTable = {OnRequestMethodCall,
                                                      nullptr, nullptr};
  dialog.request_object = g_dbus_connection_register_object(
      state->session, handle, state->request_info, &kRequestVTable, state,
      nullptr, nullptr);
  state->dialogs[handle] = std::move(dialog);

  std::string bytes;
  request.SerializeToString(&bytes);
  g_dbus_connection_call(
      state->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      vm_tools::apps::kVmApplicationsServiceSelectFileMethod,
      g_variant_new("(@ay)",
                    g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                              bytes.data(), bytes.size(), 1)),
      nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnSelectFileSent,
      new SelectFileCall{state, handle});
}

// Chrome's answer, through nickel-appd.
void OnFileSelected(GDBusConnection*,
                    const gchar*,
                    const gchar*,
                    const gchar*,
                    const gchar*,
                    GVariant* parameters,
                    gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  const gchar* token = nullptr;
  g_autofree const gchar** uris = nullptr;
  g_variant_get(parameters, "(&s^a&s)", &token, &uris);
  auto it = state->dialogs.find(token);
  if (it == state->dialogs.end()) {
    return;
  }
  if (!uris[0]) {
    FinishDialog(state, token, kCanceled, nullptr);
    return;
  }
  GVariantBuilder chosen;
  g_variant_builder_init(&chosen, G_VARIANT_TYPE("as"));
  if (it->second.names.empty()) {
    for (const gchar** uri = uris; *uri; ++uri) {
      g_variant_builder_add(&chosen, "s", *uri);
    }
  } else {
    for (const std::string& name : it->second.names) {
      g_autofree gchar* escaped =
          g_uri_escape_string(name.c_str(), nullptr, FALSE);
      std::string uri = std::string(uris[0]) + "/" + escaped;
      g_variant_builder_add(&chosen, "s", uri.c_str());
    }
  }
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "uris",
                        g_variant_builder_end(&chosen));
  FinishDialog(state, token, kSuccess, g_variant_builder_end(&results));
}

void OnMethodCall(GDBusConnection*,
                  const gchar*,
                  const gchar*,
                  const gchar*,
                  const gchar* method_name,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  if (g_strcmp0(method_name, "ReadAll") == 0) {
    ReadAll(*state, invocation, parameters);
  } else if (g_strcmp0(method_name, "Read") == 0) {
    Read(*state, invocation, parameters);
  } else if (g_strcmp0(method_name, "OpenFile") == 0 ||
             g_strcmp0(method_name, "SaveFile") == 0 ||
             g_strcmp0(method_name, "SaveFiles") == 0) {
    ShowFileDialog(state, method_name, invocation, parameters);
  } else if (g_strcmp0(method_name, "ChooseApplication") == 0) {
    // ChromeOS chooses: its default app for the file's type.
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&results, "{sv}", "choice",
                          g_variant_new_string(kChromeOSHandler));
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(ua{sv})", kSuccess, &results));
  } else if (g_strcmp0(method_name, "UpdateChoices") == 0) {
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnGetProperty(GDBusConnection*,
                        const gchar*,
                        const gchar*,
                        const gchar*,
                        const gchar* property_name,
                        GError** error,
                        gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}

// Takes on a new appearance and announces the settings that change with it.
void SetAppearance(State* state, bool dark, guint32 accent) {
  if (state->known && state->dark == dark && state->accent == accent) {
    return;
  }
  bool was_known = state->known;
  bool was_dark = state->dark;
  guint32 was_accent = state->accent;
  state->known = true;
  state->dark = dark;
  state->accent = accent;
  syslog(LOG_INFO, "appearance: %s, accent #%06x", dark ? "dark" : "light",
         accent & 0xffffff);
  for (const Key& key : kKeys) {
    if (!key.follows_appearance) {
      continue;
    }
    bool changed = !was_known ||
                   (strcmp(key.key, "accent-color") == 0
                        ? was_accent != accent
                        : was_dark != dark);
    if (!changed) {
      continue;
    }
    g_dbus_connection_emit_signal(
        state->session, nullptr, kObjectPath, kSettingsInterface,
        "SettingChanged",
        g_variant_new("(ssv)", key.ns, key.key,
                      Value(*state, key.ns, key.key)),
        nullptr);
  }
}

void OnAppearanceChanged(GDBusConnection*,
                         const gchar*,
                         const gchar*,
                         const gchar*,
                         const gchar*,
                         GVariant* parameters,
                         gpointer user_data) {
  gboolean dark = FALSE;
  guint32 accent = 0;
  g_variant_get(parameters, "(bu)", &dark, &accent);
  SetAppearance(static_cast<State*>(user_data), dark, accent);
}

void OnGotAppearance(GObject* source, GAsyncResult* result, gpointer data) {
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  if (!reply) {
    syslog(LOG_WARNING, "GetAppearance: %s", error->message);
    return;
  }
  gboolean known = FALSE;
  gboolean dark = FALSE;
  guint32 accent = 0;
  g_variant_get(reply, "(bbu)", &known, &dark, &accent);
  if (known) {
    SetAppearance(static_cast<State*>(data), dark, accent);
  }
}

// nickel-appd is (back) on the bus: ask for the appearance it has, in case
// Chrome reported it before this backend subscribed to changes.
void OnAppsAppeared(GDBusConnection* bus, const gchar*, const gchar*,
                    gpointer data) {
  g_dbus_connection_call(bus, kAppsName, kAppsPath, kAppsName,
                         "GetAppearance", nullptr, G_VARIANT_TYPE("(bbu)"),
                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnGotAppearance,
                         data);
}

void OnScreenCastCall(GDBusConnection*,
                      const gchar*,
                      const gchar*,
                      const gchar*,
                      const gchar* method_name,
                      GVariant* parameters,
                      GDBusMethodInvocation* invocation,
                      gpointer cast) {
  ScreenCastMethodCall(static_cast<ScreenCast*>(cast), method_name,
                       parameters, invocation);
}

GVariant* OnScreenCastProperty(GDBusConnection*,
                               const gchar*,
                               const gchar*,
                               const gchar*,
                               const gchar* property_name,
                               GError** error,
                               gpointer cast) {
  GVariant* value =
      ScreenCastGetProperty(static_cast<ScreenCast*>(cast), property_name);
  if (!value) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "unknown property %s", property_name);
  }
  return value;
}

void OnRemoteDesktopCall(GDBusConnection*,
                         const gchar*,
                         const gchar*,
                         const gchar*,
                         const gchar* method_name,
                         GVariant* parameters,
                         GDBusMethodInvocation* invocation,
                         gpointer cast) {
  RemoteDesktopMethodCall(static_cast<ScreenCast*>(cast), method_name,
                          parameters, invocation);
}

GVariant* OnRemoteDesktopProperty(GDBusConnection*,
                                  const gchar*,
                                  const gchar*,
                                  const gchar*,
                                  const gchar* property_name,
                                  GError** error,
                                  gpointer cast) {
  GVariant* value =
      RemoteDesktopGetProperty(static_cast<ScreenCast*>(cast), property_name);
  if (!value) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "unknown property %s", property_name);
  }
  return value;
}

void OnClipboardCall(GDBusConnection*,
                     const gchar*,
                     const gchar*,
                     const gchar*,
                     const gchar* method_name,
                     GVariant* parameters,
                     GDBusMethodInvocation* invocation,
                     gpointer cast) {
  ClipboardMethodCall(static_cast<ScreenCast*>(cast), method_name, parameters,
                      invocation);
}

GVariant* OnClipboardProperty(GDBusConnection*,
                              const gchar*,
                              const gchar*,
                              const gchar*,
                              const gchar* property_name,
                              GError** error,
                              gpointer cast) {
  GVariant* value =
      ClipboardGetProperty(static_cast<ScreenCast*>(cast), property_name);
  if (!value) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "unknown property %s", property_name);
  }
  return value;
}

void OnNameLost(GDBusConnection*, const gchar* name, gpointer loop) {
  syslog(LOG_WARNING, "lost or could not own %s", name);
  g_main_loop_quit(static_cast<GMainLoop*>(loop));
}

}  // namespace

int main() {
  openlog("xdg-desktop-portal-nickel", LOG_PID, LOG_USER);
  g_autoptr(GError) error = nullptr;
  State state;
  state.session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!state.session) {
    syslog(LOG_ERR, "no session bus: %s", error->message);
    return 1;
  }
  // Without the system bus (no nickel-appd), the settings stay at "not
  // known" and the fonts are still served.
  state.system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (state.system) {
    g_dbus_connection_signal_subscribe(
        state.system, kAppsName, kAppsName, "AppearanceChanged", kAppsPath,
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnAppearanceChanged, &state,
        nullptr);
    g_dbus_connection_signal_subscribe(
        state.system, kAppsName, kAppsName, "FileSelected", kAppsPath,
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnFileSelected, &state, nullptr);
    g_bus_watch_name_on_connection(state.system, kAppsName,
                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   OnAppsAppeared, nullptr, &state, nullptr);
  } else {
    syslog(LOG_WARNING, "no system bus: %s", error->message);
    g_clear_error(&error);
  }

  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnMethodCall, OnGetProperty,
                                               nullptr};
  state.request_info =
      g_dbus_node_info_lookup_interface(node, kRequestInterface);
  for (const char* name :
       {kSettingsInterface, kFileChooserInterface, kAppChooserInterface}) {
    if (!g_dbus_connection_register_object(
            state.session, kObjectPath,
            g_dbus_node_info_lookup_interface(node, name), &kVTable, &state,
            nullptr, &error)) {
      syslog(LOG_ERR, "cannot export %s: %s", name, error->message);
      return 1;
    }
  }
  ScreenCast* cast = ScreenCastNew(
      state.session, state.system,
      g_dbus_node_info_lookup_interface(node, kSessionInterface));
  static const GDBusInterfaceVTable kScreenCastVTable = {
      OnScreenCastCall, OnScreenCastProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kScreenCastInterface),
          &kScreenCastVTable, cast, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kScreenCastInterface,
           error->message);
    return 1;
  }
  static const GDBusInterfaceVTable kRemoteDesktopVTable = {
      OnRemoteDesktopCall, OnRemoteDesktopProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kRemoteDesktopInterface),
          &kRemoteDesktopVTable, cast, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kRemoteDesktopInterface,
           error->message);
    return 1;
  }
  // The remote desktop sessions' clipboard (screencast.cc).
  static const GDBusInterfaceVTable kClipboardVTable = {
      OnClipboardCall, OnClipboardProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kClipboardInterface),
          &kClipboardVTable, cast, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kClipboardInterface,
           error->message);
    return 1;
  }
  Notifications* notifications = NotificationsNew(state.session, state.system);
  static const GDBusInterfaceVTable kNotificationVTable = {
      OnNotificationPortalCall, OnNotificationPortalProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kNotificationInterface),
          &kNotificationVTable, notifications, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kNotificationInterface,
           error->message);
    return 1;
  }
  GlobalShortcuts* shortcuts = GlobalShortcutsNew(
      state.session, state.system,
      g_dbus_node_info_lookup_interface(node, kSessionInterface));
  static const GDBusInterfaceVTable kGlobalShortcutsVTable = {
      OnGlobalShortcutsCall, OnGlobalShortcutsProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kGlobalShortcutsInterface),
          &kGlobalShortcutsVTable, shortcuts, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kGlobalShortcutsInterface,
           error->message);
    return 1;
  }
  Access* access = AccessNew(state.system);
  static const GDBusInterfaceVTable kAccessVTable = {OnAccessCall,
                                                     OnAccessProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kAccessInterface),
          &kAccessVTable, access, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kAccessInterface, error->message);
    return 1;
  }
  Inhibit* inhibit = InhibitNew(state.session, state.system);
  static const GDBusInterfaceVTable kInhibitVTable = {
      OnInhibitCall, OnInhibitProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kInhibitInterface),
          &kInhibitVTable, inhibit, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kInhibitInterface, error->message);
    return 1;
  }
  Print* print = PrintNew(state.system);
  static const GDBusInterfaceVTable kPrintVTable = {OnPrintCall,
                                                    OnPrintProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kPrintInterface),
          &kPrintVTable, print, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kPrintInterface, error->message);
    return 1;
  }
  Screenshot* screenshot = ScreenshotNew(state.system);
  static const GDBusInterfaceVTable kScreenshotVTable = {
      OnScreenshotCall, OnScreenshotProperty, nullptr};
  if (!g_dbus_connection_register_object(
          state.session, kObjectPath,
          g_dbus_node_info_lookup_interface(node, kScreenshotInterface),
          &kScreenshotVTable, screenshot, nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kScreenshotInterface,
           error->message);
    return 1;
  }
  // Background, Account, Email, DynamicLauncher and Wallpaper.
  struct {
    const char* name;
    GDBusInterfaceVTable vtable;
    gpointer data;
  } more[] = {
      {kBackgroundInterface, {OnBackgroundCall, nullptr, nullptr},
       BackgroundNew()},
      {kAccountInterface, {OnAccountCall, nullptr, nullptr},
       AccountNew(state.system)},
      {kEmailInterface, {OnEmailCall, nullptr, nullptr}, EmailNew()},
      {kDynamicLauncherInterface,
       {OnDynamicLauncherCall, OnDynamicLauncherProperty, nullptr},
       DynamicLauncherNew(state.system)},
      {kWallpaperInterface, {OnWallpaperCall, OnWallpaperProperty, nullptr},
       WallpaperNew(state.system)},
  };
  for (auto& interface : more) {
    if (!g_dbus_connection_register_object(
            state.session, kObjectPath,
            g_dbus_node_info_lookup_interface(node, interface.name),
            &interface.vtable, interface.data, nullptr, &error)) {
      syslog(LOG_ERR, "cannot export %s: %s", interface.name,
             error->message);
      return 1;
    }
  }
  g_bus_own_name_on_connection(state.session, kBusName,
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr,
                               OnNameLost, loop, nullptr);
  g_main_loop_run(loop);
  return 0;
}
