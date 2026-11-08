# NickelOS

NickelOS is a fork of ChromiumOS that opens up the Linux underneath it. You
get ChromeOS's desktop, and Linux apps run right on it: Flatpak, AppImages,
Steam and anything in Gentoo, with no Crostini and no virtual machine.
Underneath, the whole system is a current Gentoo.

- Website and download: https://nickelos.dev
- The guide (installing, apps, Gentoo, troubleshooting): https://nickelos.dev/help/

## What's in this repository

This is NickelOS's ChromiumOS board overlay, `nickel`. It sits on top of
ChromiumOS's own tree and inherits `reven`, the public board ChromeOS Flex is
built from. Everything NickelOS adds or changes is here:

| Where | What |
|---|---|
| `chromeos-base/nickel-*` | NickelOS's own packages: the session for Linux apps, local accounts, Gentoo on the device, Flatpak, the update client, the offline guide |
| `patches/<category>/<package>/` | NickelOS's patches to Google's packages, applied when each builds. Chrome's are in `patches/chromeos-base/chromeos-chrome`, numbered, each explaining itself |
| The other categories | Packages ChromiumOS doesn't carry (PipeWire, Xwayland, GTK 4, Flatpak, elogind, polkit and more), mostly ported from Gentoo |
| `profiles/` | The board's profile: USE flags, firmware, kernel options, the hooks that apply the patches |
| `scripts/unified/` | How the image becomes Gentoo underneath: the image build's hook and the Gentoo root's package lists |
| `scripts/iso/`, `scripts/secureboot/` | The install ISO, and Secure Boot through shim |
| `scripts/update/`, `scripts/release/` | The update server, signing and releases |
| `scripts/vmtest/` | The VM tests every build goes through before release |
| `i18n/` | Translations of NickelOS's own wording in Chrome, 81 languages |
| `site/` | nickelos.dev and the guide |

## Building

NickelOS builds like any ChromiumOS board: a ChromiumOS checkout
(`release-R157-16852.B` for this release), this overlay in
`src/overlays/overlay-nickel`, then `setup_board`, `build-packages` and
`build-image` for `--board=nickel`. The Gentoo underneath comes from a Gentoo
root built beside the checkout (`scripts/unified/`). `scripts/README.md`
describes the build tooling. Fuller instructions for building it yourself
are still to come.

## License

NickelOS's own work is under the BSD 3-Clause License ([LICENSE](LICENSE)).
Ebuilds are under the GNU GPL v2, as their headers say, and patches to other
projects are under those projects' licenses; LICENSE lists what's what.

## Reporting problems

See [Getting help](https://nickelos.dev/help/community/) for where to ask,
and what a useful bug report includes.

NickelOS is a community project, not affiliated with or endorsed by Google.
ChromeOS, ChromiumOS and Chrome are trademarks of Google LLC.
