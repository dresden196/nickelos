# NickelOS build and test tooling

Everything here runs on the build host (`nickel-build`, a VM on the build server);
paths assume the layout used there: the ChromiumOS checkout in
`/srv/nickel/src`, mirrors in `/srv/nickel/mirror`, VM tests in
`/srv/nickel/vmtest`, build user `nickel`.

## buildhost/

Installed to `/usr/local/bin`, `/etc/systemd/system` and `/etc/default`.

- `nickel-build-stock` (unit `nickel-build@<board>`): setup_board, seed
  binpkgs (`NICKEL_SEED_FROM` in `/etc/default/nickel-build-<board>`),
  rebuild packages whose overlay hooks or patches changed, build-packages,
  build-image test, image_to_vm. Chrome is built from the existing checkout
  (`CHROME_ORIGIN=LOCAL_SOURCE`) when it is at the ebuild's pinned commit;
  see `profiles/base/bashrc/nickel-chrome.bashrc`.
- `nickel-chrome-dev`: Chrome patch loop without an image build: build
  chromeos-chrome and `cros deploy` it to the running test VM.
- `nickel-vmtest <image>` (run under `systemd-run --unit nickel-vmtest`):
  QEMU/OVMF test VM. `nickel-vmssh`, `nickel-vmctl` (QMP: shot, click, type,
  key), `nickel-vmshot`, `nickel-vmsettle` drive it; `NICKEL_VMTEST_DIR` and
  `NICKEL_VMTEST_SSHPORT` select a second VM.
- `nickel-binhost-sync`, `nickel-binhost.service`: the development Portage
  binhost (http://10.0.2.2:8090 as seen from a test VM).
- `nickel-mirror-*`: ChromiumOS and Chromium mirrors.
- `nickel-verify.sh`: overlay checks run inside a booted image.

## vmtest/

End-to-end tests, run as root on the build host against the latest image:
`launcher-test.sh` (first-boot Bazaar, session, app list, shelf, Terminal,
Bazaar GUI install), `flatpak-test.sh`, `portage-test.sh`,
`portage-persist-test.sh`, `home-exec-test.sh`, `accounts-test.sh`
(nickel-accounts over D-Bus), `local-account-test.sh` (create an account in
setup, sign out, sign in offline, reboot), `installer-ui-test.sh` (install
from a USB stick to a disk picked in the installer, boot it). `guest-login.sh`
signs in as guest from the setup screens.

## sdk/

Run inside `cros_sdk`, from `.nickel/` in the checkout:

- `symbolize.sh`: symbolizes a Chrome minidump from the test VM
  (`/home/chronos/crash/*.dmp`, copied to `.nickel/crash.dmp`).
- `chrome-compile.sh <src/path.cc...>`: compiles single Chrome objects in the
  last build's out dir, a quick check of a patch before a full build. Copy the
  patched files into the checkout first; `nickel-chrome.bashrc` removes files
  a patch creates before applying it.
