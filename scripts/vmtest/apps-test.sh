#!/bin/bash
# Runs on nickel-build, in the session local-account-test.sh leaves behind
# (signed in as tester). Apps installed the usual way, unmodified:
#   1. nickel-x11 runs sommelier and Xwayland as the session user; display :0
#      and its cookie are in the session environment
#   2. an X11 client works: GIMP from Flathub, forced onto X11
#   3. Wine (Flathub, X11) opens Notepad
#   4. audio: the session's PulseAudio server plays into CRAS
#   5. Steam (Flathub, X11 with GLX) starts and shows its sign-in window
#   6. AppImages run from the home directory: appimagetool (current static
#      runtime), Obsidian and balenaEtcher 1.7.9 (Electron apps, older
#      libfuse2 runtime, needing the desktop base: GTK 3, AT-SPI, X libs,
#      Gentoo's since Gentoo is at /)
# Screenshots go to $V/flow/x11-*.png.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }
shot() { nickel-vmctl click 1270 300; sleep 6; nickel-vmctl shot "$V/flow/x11-$1.png"; }

echo "== 1. sommelier and Xwayland"
vm 'initctl status nickel-x11; ps -o user=,args= -C sommelier,Xwayland | cut -c1-150
cat /run/user/5000/x11-display; ls -la /tmp/.X11-unix/
grep -E "^(DISPLAY|XAUTHORITY)=" /run/nickel/session/env'

echo "== 2. GIMP on X11"
vm 'nickel-session-exec sh -c "cd; flatpak install --system -y --noninteractive flathub org.gimp.GIMP 2>&1 | tail -1"'
vm 'nickel-session-exec sh -c "cd; nohup flatpak run --socket=x11 --nosocket=wayland --env=GDK_BACKEND=x11 org.gimp.GIMP >/tmp/gimp-x11.log 2>&1 &"'
sleep 120
shot gimp
vm 'tail -3 /tmp/gimp-x11.log; ps -o user=,args= -C gimp-3.2 | head -2'

echo "== 3. Wine"
vm 'nickel-session-exec sh -c "cd; flatpak install --system -y --noninteractive flathub org.winehq.Wine//stable-26.08 2>&1 | tail -1"'
vm 'nickel-session-exec sh -c "cd; nohup flatpak run org.winehq.Wine//stable-26.08 notepad >/tmp/wine.log 2>&1 &"'
sleep 150
shot wine
vm 'tail -5 /tmp/wine.log; ps -eo user,args | grep -iE "[n]otepad|[w]ineserver" | head -3 | cut -c1-120'

echo "== 4. audio through CRAS"
vm 'initctl status nickel-audio; ps -o user=,args= -C pulseaudio | cut -c1-120
nickel-session-exec pactl info | grep -E "Server Name|Default Sink|Default Source"
nickel-session-exec pactl list short sinks
(head -c 960000 /dev/urandom | nickel-session-exec pacat --format=s16le --rate=48000 --channels=2 &)
sleep 2; cras_test_client --dump_server_info | grep -iE "num_streams|stream|client" | head -8'

echo "== 5. Steam"
vm 'nickel-session-exec sh -c "cd; flatpak install --system -y --noninteractive flathub com.valvesoftware.Steam 2>&1 | tail -1"'
vm 'sudo -u tester dbus-send --system --print-reply --dest=org.nickelos.Apps /org/nickelos/Apps org.nickelos.Apps.LaunchApplication string:com.valvesoftware.Steam array:string: | tail -1'
sleep 300
shot steam
vm 'ps -eo user,args | grep -iE "[s]team" | head -4 | cut -c1-120'

echo "== 6. AppImages"
# They run on Gentoo's libraries: Gentoo's setup at the first startup first.
vm 'nickel-gentoo wait 1800 >/dev/null || echo "Gentoo is not set up"'
nickel-vmssh "nickel-session-exec sh -s" < "$V/appimage-guest.sh"
sleep 90
shot obsidian
vm 'tail -3 /tmp/obsidian.log | cut -c1-160; ps -eo user,args | grep -i "[o]bsidian" | head -2 | cut -c1-120'
sleep 30
shot etcher
vm 'tail -3 /tmp/etcher.log | cut -c1-160; ps -eo user,args | grep -i "[b]alena" | head -2 | cut -c1-120'
