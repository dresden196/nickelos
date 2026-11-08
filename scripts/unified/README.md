# NickelOS: the image build's hook and the Gentoo root's tools

`docs/unified.md` says what each step found. These run as root on
nickel-build, in `/srv/nickel/unified`, except `nickel-unify`, which the
image build runs:

- `nickel-unify` (with `nickel-unify-*.py`): the build_image hook. It puts
  the Unified root's packages into the image, merges /usr
  (`uf-merge-usr.py`), adds the sandbox rules' system calls
  (`uf-policies.sh`, `seccomp-additions.txt`), checks every library
  (`uf-check-libs.py`) and packs Portage's records of the image.
- `uf-root-build.sh`, `uf-root-candidates.sh`: the Unified Gentoo root
  (`root/`). It is synced to today's Gentoo, updated, and given every
  package the image has that Gentoo has too. `nickel-gentoo-run` runs
  commands in it.
- `uf-google-use.py`: the OS's Gentoo copies get Google's USE flags
  (`gentoo/etc/portage/profile/package.use/zz-unified-google`).
- `uf-device-repo.sh`: NickelOS's own Gentoo packages (`gentoo/device-repo.txt`)
  copied into nickel-gentoo's repository.
- `uf-binpkg-sync.py`: after any emerge in the root, the binary packages
  the image and the binhost take are the root's.
- `uf-dryrun.sh`: the hook's dependency resolution (emerge --pretend),
  in minutes, without an image build.
- `uf-inventory.py`: the image's libraries and packages against the root.
- `uf-mesa-check.sh`, `uf-vm-audit.sh`: Mesa's files, and what the release
  check doesn't look at (glibc per service, sandbox kills, crashes, Upstart
  jobs not running) in a test VM.

Gentoo's glibc carries three of Google's patches, from
`gentoo/etc/portage/patches/sys-libs/glibc`.

The step 1 proof's tools (`uf-swap.py`, `uf-build-proof.sh` and the step 3
prototype's), which worked on Original's 16836.3.36 image, were removed on
2026-10-23 with that image; they are in git history before then.
