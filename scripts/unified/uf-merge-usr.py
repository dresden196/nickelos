#!/usr/bin/env python3
# NickelOS (docs/unified.md, step 3): the image's root made
# merged-/usr, Gentoo 23.0's layout and every current distribution's:
# /bin, /sbin and /usr/sbin lead to /usr/bin; /lib and /lib64 to /usr/lib and
# /usr/lib64. ChromeOS's own paths (/sbin/init, /bin/sh, /usr/sbin/cryptohomed)
# keep working through the links, and Gentoo's packages install as on any
# Gentoo.
#   - a name in two of the merged directories: the one that isn't a link to
#     the other wins (coreutils' and kmod's compatibility links)
#   - SELinux labels follow the real path: each file_contexts rule for
#     /bin, /sbin, /usr/sbin, /lib or /lib64 gets the same rule for its new
#     place, and the files moved keep the label they had
#   uf-merge-usr.py ROOT [FILE_CONTEXTS...]
import os, re, shutil, sys

ROOT = sys.argv[1]
FCS = sys.argv[2:]
MOVES = (('bin', 'usr/bin'), ('sbin', 'usr/bin'), ('usr/sbin', 'usr/bin'),
         ('lib', 'usr/lib'), ('lib64', 'usr/lib64'))
LINKS = {'bin': 'usr/bin', 'sbin': 'usr/bin', 'usr/sbin': 'bin', 'lib': 'usr/lib', 'lib64': 'usr/lib64'}


def label(p):
    try:
        return os.getxattr(p, 'security.selinux', follow_symlinks=False)
    except OSError:
        return None


def inroot(p, hops=40):
    """The real path of p inside ROOT: absolute link targets are the image's,
    not the build host's."""
    rel = os.path.relpath(p, ROOT).split('/')
    done = []
    while rel:
        part = rel.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if done:
                done.pop()
            continue
        cur = os.path.join(ROOT, *done, part)
        if os.path.islink(cur) and hops > 0:
            hops -= 1
            t = os.readlink(cur)
            if t.startswith('/'):
                done = []
                t = t.lstrip('/')
            rel = t.split('/') + rel
            continue
        done.append(part)
    return '/' + '/'.join(done)


def points_to(link, target):
    """True when link (a symlink) leads to target's real file."""
    return inroot(link) == inroot(target)


def ld_script(p):
    if os.path.islink(p) or not os.path.isfile(p):
        return False
    with open(p, 'rb') as f:
        return b'GNU ld script' in f.read(512)


def merge_dir(src, dst):
    moved = dup = 0
    for name in sorted(os.listdir(src)):
        s, d = os.path.join(src, name), os.path.join(dst, name)
        if not os.path.lexists(d):
            os.rename(s, d)
            moved += 1
            continue
        if os.path.isdir(s) and not os.path.islink(s) and os.path.isdir(d) and not os.path.islink(d):
            m, u = merge_dir(s, d)
            moved += m
            dup += u
            os.rmdir(s)
            continue
        # Both have it: keep the real one (or either, when both lead to the
        # same file or are the same file).
        if inroot(s) == inroot(d) or (
                os.path.isfile(s) and os.path.isfile(d) and not os.path.islink(s) and not os.path.islink(d)
                and open(s, 'rb').read() == open(d, 'rb').read()):
            if os.path.islink(d) and not os.path.islink(s):
                os.unlink(d)
                os.rename(s, d)
            else:
                os.unlink(s)
        elif ld_script(d) and os.path.islink(s):
            # Split /usr's linker script in /usr/lib64 naming /lib64: merged,
            # the plain development link is enough.
            os.unlink(d)
            os.rename(s, d)
        elif ld_script(s) and os.path.islink(d):
            os.unlink(s)
        elif os.path.islink(s) and points_to(s, d):
            os.unlink(s)
        elif os.path.islink(d) and points_to(d, s):
            os.unlink(d)
            os.rename(s, d)
        else:
            raise SystemExit('both %s and %s, and neither leads to the other' % (s, d))
        dup += 1
    return moved, dup


def main():
    for src, dst in MOVES:
        s, d = os.path.join(ROOT, src), os.path.join(ROOT, dst)
        if os.path.islink(s):
            continue
        lab = label(s)
        os.makedirs(d, exist_ok=True)
        moved, dup = merge_dir(s, d)
        os.rmdir(s)
        os.symlink(LINKS[src], s)
        if lab:
            os.setxattr(s, 'security.selinux', lab, follow_symlinks=False)
        print('/%s -> %s: %d moved, %d in both' % (src, LINKS[src], moved, dup))
    for fc in FCS:
        lines = open(fc).read().splitlines()
        have = {l.split()[0] for l in lines if l.strip() and not l.startswith('#')}
        add = []
        for l in lines:
            if not l.strip() or l.startswith('#'):
                continue
            path = l.split()[0]
            for old, new in (('/usr/sbin', '/usr/bin'), ('/sbin', '/usr/bin'), ('/bin', '/usr/bin'),
                             ('/lib64', '/usr/lib64'), ('/lib', '/usr/lib')):
                # Rules written for one place, also for where its files are now.
                for pre in ('', '(/usr)?'):
                    if path.startswith(pre + old + '/') or path.startswith(pre + old + '('):
                        np = new + path[len(pre + old):]
                        if np not in have:
                            add.append(np + l[len(path):])
                            have.add(np)
                        break
        if add:
            with open(fc, 'a') as f:
                f.write('# NickelOS: merged /usr (rules of /bin, /sbin, /usr/sbin, /lib and /lib64 for their files\' new place)\n')
                f.write('\n'.join(add) + '\n')
        print('%s: %d rules for the merged places' % (fc, len(add)))


if __name__ == '__main__':
    main()
