title: Learning Gentoo
nav: Learning Gentoo
order: 32
summary: A walkthrough of Gentoo for people new to it: what Portage's words mean, a first session with emerge, changing how a package is built, what to do when emerge says no, and pacman and apt commands with their Gentoo equivalents.
---
# Learning Gentoo

Most of what you know from another Linux carries over to NickelOS. The shell is bash and `sudo` works as usual. What's new is the package manager: Gentoo's is Portage, and its command is `emerge`.

This page is the walkthrough. [Gentoo and Portage](/help/gentoo/) is the reference for how Gentoo is set up on NickelOS, and the [Gentoo wiki](https://wiki.gentoo.org) has the rest. You can skip the Handbook's installation chapters (partitions, stage3, building a kernel, a boot loader): NickelOS did that, and the kernel comes with the system.

## The words

**Package names have a category.** GIMP is `media-gfx/gimp`, htop is `sys-process/htop`. You can usually leave the category off, and `eix` shows it when you search.

**An ebuild is a package's recipe**, like a PKGBUILD on Arch. The **tree** (or repository) is all of them, in `/var/db/repos/gentoo`. `sudo emerge --sync` updates it.

**USE flags are build options.** A package's flags say what it's built with: `pulseaudio`, `X`, `wayland`, `vaapi`. A flag that's on is compiled in, `-flag` means without. You set them per package (or for everything), and Portage builds the package to match. Some flags come in groups with their own names, like `PYTHON_TARGETS` (which Pythons a package is built for), `VIDEO_CARDS` and `L10N` (languages).

**Keywords say how tested a version is.** `amd64` is stable, `~amd64` is testing. Gentoo's own tree installs stable versions only, unless you allow testing ones for a package. GURU and steam-overlay are accepted at `~amd64` already, since that's all they have.

**A mask blocks a version on purpose.** Gentoo masks broken or dangerous versions. NickelOS also masks newer versions of the system's own packages, since those come with OS updates (see [Your packages and the system's](/help/gentoo/#your-packages-and-the-system-s)).

**@world is what you asked for.** Every package you install by name goes in `/var/lib/portage/world`. Updates start from that list, and anything nothing on it needs anymore can be cleaned up.

**Binary or source.** `emerge` downloads a built package when one matches your settings, and compiles when none does, for example after you change a package's USE flags ([Binary packages](/help/gentoo/#binary-packages) has where they come from).

## A first session

Open the Terminal and try it with htop.

**Search.**

```
eix htop
```

eix shows the full name, the versions in the tree, which one is installed (if any) and the package's USE flags, with `+` marking the ones on by default. `eix -I` lists what's installed.

**Install.**

```
sudo emerge --ask sys-process/htop
```

`--ask` shows the plan before doing anything. Each line is one package:

```
[binary  N g   ] sys-process/htop-3.5.3-1::gentoo  USE="filecaps unicode -bfd -caps -debug ..." 200 KiB
```

`binary` means it downloads a built package, `ebuild` means it compiles. The letters say what happens to it:

| Letter | Meaning |
|---|---|
| `N` | New, not installed yet |
| `U` | Upgrade |
| `D` | Downgrade |
| `R` | Reinstall, same version |
| `r` | Reinstall because a library it uses changed |
| `S` | A new slot: installs beside the version you have |

In the `USE="..."` part, a flag in parentheses is fixed by the profile, `*` marks one that changed since the installed build and `%` one that's new.

Say yes, and run `htop`. It's in the launcher too, if the package has a desktop entry.

If a plan wants to rebuild far more than the package you asked for, something in your settings changed. Read it before saying yes; `--pretend` (`-p`) shows a plan without asking.

**Remove.**

```
sudo emerge --ask --deselect sys-process/htop
sudo emerge --ask --depclean
```

`--deselect` takes it off your world list, and `--depclean` removes it with anything only it needed. `emerge --unmerge` (`-C`) removes a package without checking whether something else needs it: use it only when you know nothing does.

**Install without adding to world.** `--oneshot` (`-1`) installs or rebuilds a package without putting it on your world list, which is what you want for a library you're rebuilding by hand: `--depclean` then removes it once nothing needs it.

**Update everything.**

```
sudo emerge --sync
sudo emerge --ask --update --deep --newuse @world
```

The second line is usually written `emerge -auDN @world`: update what's on your world list, the packages they depend on, and anything whose USE flags changed. Once a week is plenty.

A long update stops at the first package that fails to build. `--keep-going` carries on with the rest, and after a stop, `sudo emerge --resume --skipfirst` picks up where it left off without the failed one. If an update ends with "existing preserved libs found", run `sudo emerge @preserved-rebuild`: some programs still use an old library, and that rebuilds them against the new one.

**Clean up.** Portage keeps downloaded sources and packages. `sudo eclean-dist --deep` and `sudo eclean-pkg --deep` remove the ones for versions you no longer have installed.

## Changing how a package is built

Say you want mpv with VA-API video decoding. See its flags first:

```
equery uses media-video/mpv
```

Then add a line to a file of your own in `/etc/portage/package.use/`, and rebuild:

```
echo "media-video/mpv vaapi" | sudo tee -a /etc/portage/package.use/mine
sudo emerge --ask --changed-use media-video/mpv
```

The next `emerge -auDN @world` would pick it up as well. Flags for every package go in your own file in `/etc/portage/make.conf/` (see [Changing USE flags, keywords and licenses](/help/gentoo/#changing-use-flags-keywords-and-licenses)); those rebuild a lot more, so start per package.

**A testing version** of one package goes in `/etc/portage/package.accept_keywords/`:

```
echo "media-video/mpv ~amd64" | sudo tee -a /etc/portage/package.accept_keywords/mine
```

**A license** that isn't free (Steam's, unrar's) goes in `/etc/portage/package.license/`:

```
echo "app-arch/unrar unRAR" | sudo tee -a /etc/portage/package.license/mine
```

**What emerge does for you.** When a package needs another package built with a USE flag, or a newer version of one of the system's packages, emerge adds that itself and carries on. Testing keywords and licenses are your call, so emerge stops and says which ones a package needs. Add those lines yourself, or let emerge propose them all at once; [Changing USE flags, keywords and licenses](/help/gentoo/#changing-use-flags-keywords-and-licenses) has the command.

When emerge ends with "config files need updating", run `sudo dispatch-conf`. It shows each change and asks before saving it.

## When emerge says no

**"There are no ebuilds to satisfy"** means emerge can't find that name. Search with `eix` to get the category right. If `nickel-gentoo status` says the repository isn't there yet, the first download after setup hasn't finished. If the package is in a repository you haven't turned on, `eselect repository` turns it on (see [Repositories and profile](/help/gentoo/#repositories-and-profile)).

**"masked by: ~amd64 keyword"** means the version emerge wants is testing-only, often because another package needs a newer one than the stable version. Accept the keyword for that package (above), or wait for a stable one.

**"masked by: package.mask"** means it's masked on purpose, and emerge prints the reason below that line. If it's one of the system's own packages, see [A newer system package](/help/gentoo/#your-packages-and-the-system-s).

**"masked by: missing keyword"** means nobody has tested that version on amd64. You can accept it with `=category/name-1.2 **` (one version, so you don't also get the live `9999` one), but it may not build.

**"masked by: ... license"** means a license you haven't accepted. Add it to `package.license` (above).

**Blockers** show up on their own lines in the plan. `[blocks b]` means emerge will sort it out itself, usually by replacing an old package with a new one. `[blocks B]` means you have to act, and the message says which package to remove first.

**A slot conflict** is a long message about two packages wanting different versions of a third. Updating everything first (`emerge -auDN @world`) fixes most of them. Adding `--backtrack=50` gives emerge more tries to find a plan.

**A build that fails** ends with the path to its build log. The error is near the end of it, often a few screens up from the last line. Search [Gentoo's bug tracker](https://bugs.gentoo.org) for the package name. A different version, or turning off the flag the error names, often gets around it. When you ask for help, include the error and the output of `emerge --info`.

**A very long build.** Browsers, LLVM and Rust take an hour or more to compile. Before you start one, look for a `-bin` package (`www-client/firefox-bin`, `dev-lang/rust-bin`), which installs upstream's build, or the app on Flathub.

**"IMPORTANT: N news items need reading"** is Gentoo telling you about changes that may need you to act. A new install starts with some. `eselect news list` shows them and `eselect news read` reads them.

## From Arch, Debian or Ubuntu

| Task | Arch | Debian, Ubuntu | NickelOS (Gentoo) |
|---|---|---|---|
| Search | `pacman -Ss name` | `apt search name` | `eix name` |
| Details | `pacman -Si name` | `apt show name` | `eix -e name`, `equery meta name` |
| Install | `pacman -S name` | `apt install name` | `emerge --ask name` |
| Remove with what it needed | `pacman -Rns name` | `apt autoremove --purge name` | `emerge --deselect name`, then `emerge --depclean` |
| Update everything | `pacman -Syu` | `apt update && apt upgrade` | `emerge --sync`, then `emerge -auDN @world` |
| Why is this installed | `pacman -Qi name` (Required By) | `apt rdepends --installed name` | `equery depends name`, `emerge -pv --depclean name` |
| Which package has this file | `pacman -Qo /path` | `dpkg -S /path` | `equery belongs /path` or `qfile /path` |
| A package's files | `pacman -Ql name` | `dpkg -L name` | `equery files name` or `qlist name` |
| Everything installed | `pacman -Q` | `apt list --installed` | `qlist -I` or `eix -I` |
| What you installed yourself | `pacman -Qe` | `apt-mark showmanual` | `cat /var/lib/portage/world` |
| Clear the download cache | `pacman -Sc` | `apt clean` | `eclean-dist --deep`, `eclean-pkg --deep` |
| Community packages | the AUR | PPAs | GURU (already on); others with `eselect repository` |
| A package's recipe | PKGBUILD | `debian/rules` | ebuild |
| Build options | (rebuild it yourself) | (rebuild it yourself) | USE flags |
| Start a service at boot | `systemctl enable --now sshd` | `systemctl enable --now ssh` | `rc-update add sshd default`, then `rc-service sshd start` |

Commands that change things need `sudo`, as on the others. One difference worth knowing: settings you'd pass to `makepkg` or `apt` once go in files under `/etc/portage/` on Gentoo, so every later update builds the same way.

## Learning more

- The Handbook's [Working with Portage](https://wiki.gentoo.org/wiki/Handbook:AMD64/Working/Portage) chapter, and the [USE flags](https://wiki.gentoo.org/wiki/Handbook:AMD64/Working/USE) chapter after it.
- The wiki's [Gentoo Cheat Sheet](https://wiki.gentoo.org/wiki/Gentoo_Cheat_Sheet), and its pages on [eix](https://wiki.gentoo.org/wiki/Eix), [Gentoolkit](https://wiki.gentoo.org/wiki/Gentoolkit) (`equery`, `eclean`) and [GURU](https://wiki.gentoo.org/wiki/Project:GURU).
- `man emerge`, `man portage` and `man make.conf`, in the Terminal.
- Gentoo's [forums](https://forums.gentoo.org) and `#gentoo` on Libera.Chat, for Gentoo questions. For anything NickelOS does differently, see [Getting help](/help/community/).
