#!/bin/bash
# Runs on nickel-build, signed in as tester in the test VM. NickelOS
# (docs/unified.md): the OS is Gentoo underneath, and what you install goes
# to your layer (nickel-gentoo).
#   1. the OS's Gentoo: Portage, every package recorded, your layer's overlays
#      on /usr and /etc, Gentoo's glibc; at the first startup Gentoo's
#      repository was fetched with no step of the user's, and a setup typed
#      afterwards finds it there
#   2. `emerge x11-terms/xterm` installs XTerm from a binhost (no build), and
#      it shows up in the launcher without signing in again
#   3. launched the way the launcher does (nickel-appd), XTerm runs from /usr
#      through Xwayland
#   4. uninstalling it (nickel-appd, as the launcher's "Uninstall") asks for
#      an administrator's password (polkit) and removes the package: it
#      leaves the launcher
#   5. Gentoo's commands as Gentoo users type them: one emerge and python3,
#      the same for the OS's services; looking needs no password; one-command
#      settings reach Portage; tab completion of package names
#   6. one world: ChromeOS's services and Chrome run on Gentoo's glibc
#   7. Portage on the read-only root: a build from source (no sandbox
#      noise), an OS package rebuilt into your layer and the OS's version
#      put back (nickel-gentoo restore), a 32-bit program built and run, a
#      program built against a library that stays the OS's own (GTK 3: its
#      headers and pkg-config files are in the image), the source-built
#      package removed again; emerge --depclean leaves the OS alone (it's in
#      your world, @nickel-os) and a world update resolves, leaving the
#      OS's packages at the OS's versions, while one line in package.unmask
#      takes a newer one (src/world-policy-check.py); SELinux labels:
#      the OS's files
#      keep theirs, yours get their paths'
# Root's commands here run as from a login shell (/etc/profile). Polkit's
# password dialog (1280x800) takes the typed password. Screenshots go to
# $V/flow/gentoo-*.png.
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
PASS=nickel-test-pw
vm() { nickel-vmssh "$@" </dev/null; }
shot() { nickel-vmsettle "$V/flow/gentoo-$1.png" 4 >/dev/null 2>&1; }
apps() {
  vm 'python3 -c "import glob, json
h = glob.glob(\"/home/chronos/u-*/\")[0]
r = json.load(open(h + \"Preferences\"))[\"crostini\"][\"registry\"]
print(sorted(v.get(\"desktop_file_id\") for v in r.values()))"'
}
appd() {
  vm "nickel-session-exec dbus-send --system --print-reply --dest=org.nickelos.Apps \
    /org/nickelos/Apps org.nickelos.Apps.$1" | tail -n +2
}
# Login shells print ChromeOS's cursor-on sequence (/etc/profile.d/cursor.sh).
as_tester() { vm "sudo -u tester bash -lc 'cd; $1' 2>&1" | sed 's/\x1b\[?25h\x1b\[?0c//g'; }
root() { vm ". /etc/profile >/dev/null 2>&1; echo 0 > /proc/self/oom_score_adj; cd /root; $1"; }

echo "== 1. the OS's Gentoo"
vm 'nickel-gentoo wait 1800; echo "repository: $?"; nickel-gentoo setup 2>&1; nickel-gentoo status | sed -E "s/: [0-9.]+[KMG]? on/: SIZE on/"'
vm 'findmnt -n -o TARGET,FSTYPE /usr /etc | sort; /lib64/ld-linux-x86-64.so.2 --version | head -1 | grep -o "Gentoo [0-9.]*" | sed "s/[0-9.]*$/VERSION/"
echo "records: $(ls -d /var/db/pkg/*/* | wc -l | sed -E "s/^[0-9]{4}$/1000+/") ($(ls /var/db/pkg/nickel-base))"
type -P emerge; getent passwd portage | cut -d: -f1,6,7'

echo "== 2. emerge XTerm (binary), launcher"
root 'emerge --getbinpkg --usepkg x11-terms/xterm 2>&1 | grep -E ">>> (Emerging binary|Completed)|!!!" | sed -E "s/\([0-9]+ of [0-9]+\)/(N)/; s/-[0-9][^:]*::/::/" | sort -u'
sleep 15
apps

echo "== 3. launch"
appd 'LaunchApplication string:xterm array:string:'
sleep 5
vm 'p=$(pgrep -u tester -x xterm | head -1); echo "xterm: $(readlink /proc/$p/exe); libc $(grep -o "/[^ ]*/libc\.so\.6" /proc/$p/maps | sort -u)"'
shot 1-xterm
nickel-vmctl click 640 400; nickel-vmctl type exit; nickel-vmctl key ret; sleep 2

echo "== 4. uninstall"
appd 'UninstallApplication string:xterm'
sleep 6
shot 2-password
nickel-vmctl type "$PASS"; nickel-vmctl key ret
sleep 40
vm 'ls /usr/share/applications/ | grep -c "^xterm"; ls /var/db/pkg/x11-terms 2>&1 | sed "s/-[0-9].*//"'
apps

echo "== 5. Gentoo commands"
as_tester 'for c in emerge python3 bash equery; do printf "%s: %s\n" $c "$(type -P $c)"; done'
vm 'for c in python3 bash; do printf "OS services: %s: %s\n" $c "$(env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin sh -c "command -v $c")"; done'
as_tester 'python3 --version | sed "s/[0-9.]*$/VERSION/"'
as_tester 'emerge -pv --nodeps sys-apps/portage 2>/dev/null | grep -c "sys-apps/portage"'
as_tester 'USE=-native-extensions emerge -pv --nodeps sys-apps/portage 2>/dev/null | grep -q -- "-native-extensions\*" && echo "USE= as a user: reaches Portage"'
vm 'cd /root; MAKEOPTS=-j1 portageq envvar MAKEOPTS'
as_tester 'bash -ic "_comp_load emerge 2>/dev/null; complete -p emerge
COMP_WORDS=(emerge sys-apps/portag); COMP_CWORD=1; COMP_LINE=\"emerge sys-apps/portag\"; COMP_POINT=\${#COMP_LINE}; _emerge emerge sys-apps/portag emerge; echo \"emerge sys-apps/portag<Tab>: \${COMPREPLY[*]}\"" 2>/dev/null'

echo "== 6. one world"
vm 'for n in chrome shill powerd session_manager cryptohomed; do p=$(pgrep -x $n | head -1); echo "$n: $(grep -o "/[^ ]*/libc\.so\.6" /proc/$p/maps | sort -u)"; done
echo "glibc: $(/usr/lib64/libc.so.6 | head -1 | grep -o "version [0-9.]*" | sed "s/[0-9.]*$/VERSION/")"'

echo "== 7. Portage on the read-only root"
root 'emerge --oneshot --usepkg=n --getbinpkg=n --quiet app-text/tree > /tmp/g7-tree.log 2>&1; echo "tree from source: $? $(grep -c "ld.so: object" /tmp/g7-tree.log) sandbox complaints; $(/usr/bin/tree --version 2>/dev/null | cut -d" " -f1-2 | sed "s/[0-9.]*$/VERSION/")"
emerge --oneshot --usepkg=n --getbinpkg=n --quiet app-arch/gzip > /tmp/g7-gzip.log 2>&1; echo "gzip rebuilt: $? in your layer: $(ls "$(findmnt -n -o OPTIONS /usr | tr , "\n" | sed -n "s/^upperdir=//p")/bin/gzip" 2>/dev/null | wc -l)"
nickel-gentoo restore app-arch/gzip 2>&1 | sed -E "s/gzip-[0-9][^ ]*/gzip-VERSION/"
cd /usr/local/tmp && echo "int main(void){return sizeof(void*) == 4 ? 0 : 1;}" > g7.c && gcc -m32 -o g7 g7.c && ./g7 && echo "a 32-bit program built and run"; rm -f g7 g7.c
printf "#include <gtk/gtk.h>\nint main(void){return gtk_get_major_version() == 3 ? 0 : 1;}\n" > g7g.c && gcc -o g7g g7g.c $(pkg-config --cflags --libs gtk+-3.0) && ./g7g && echo "a program built against the image GTK 3 and run"; rm -f g7g g7g.c; cd /root
emerge --depclean --quiet app-text/tree > /tmp/g7-depclean.log 2>&1; echo "tree removed: $? $(ls /var/db/pkg/app-text | grep -c ^tree-)"
emerge -p --depclean > /tmp/g7-dc.log 2>&1; echo "emerge --depclean would remove: $(grep -o "Number to remove: *[0-9]*" /tmp/g7-dc.log | grep -o "[0-9]*$") (world sets: $(cat /var/lib/portage/world_sets 2>/dev/null))"
emerge -puDN --quiet @world > /tmp/g7-world.log 2>&1; echo "emerge -uDN @world resolves: $?"'
# Who updates what (vision decision 1): a world update leaves the OS's
# packages at the OS's versions, and package.unmask takes a newer one.
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/world-policy-check \
  < "$(dirname "$0")"/src/world-policy-check.py
root '/usr/local/bin/world-policy-check'
vm 'for f in /usr/bin/fusermount3 /usr/sbin/cryptohomed /usr/lib64/libc.so.6 /usr/bin/emerge /etc/portage/make.conf; do echo "$f: $(ls -Z -d $f | cut -d" " -f1)"; done'
