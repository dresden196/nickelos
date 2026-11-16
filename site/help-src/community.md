title: Getting help
order: 130
summary: Where to ask for help with NickelOS (the forum at forum.nickelos.dev, Discord, GitHub, email), and how to write a bug report we can actually act on.
---
# Getting help

NickelOS is a community project. Ask in the places below; someone may have hit the same problem.

## Where to ask

| Where | What it's for |
|---|---|
| **[The NickelOS forum](https://forum.nickelos.dev/)** | Questions, how-tos, hardware reports, and longer discussions that should stay findable. It has a place for each (Installing, Help, Gentoo and Portage, Steam and games, Hardware reports, Feedback and ideas), and its Help and Hardware reports ask for what's needed to answer. |
| **Discord** | Chatting, quick questions, and seeing what's being worked on. Coming soon. |
| **GitHub** | Bug reports and the source code, at [github.com/dresden196/nickelos](https://github.com/dresden196/nickelos). |
| **Email** | Anything you'd rather not post in public, security problems included: {{EMAIL}} |

> Nobody from NickelOS will ever ask for your password, or ask to connect to your computer remotely. If someone offering help does, it isn't us.

## Writing a bug report

What helps us fix a bug:

1. **Your NickelOS version.** It's in Settings > About NickelOS, or run `grep CHROMEOS_RELEASE_VERSION /etc/lsb-release`. Mention your update channel too.
2. **Your hardware.** The computer's make and model, and its GPU: `lspci -nn | grep -iE 'vga|3d|display'`.
3. **What you did, what you expected, and what happened.** Exact steps, and the exact text of any error message.
4. **How the app got there**, if it's about an app: Bazaar or Flatpak (with its app id), Gentoo (with the package), or an AppImage.
5. **Logs.** The relevant part, not the whole file. [Troubleshooting](/help/troubleshooting/#where-to-look) says which log covers what. For a crash, the crash report files.

> Read logs before you post them anywhere public. They can include your username, file names, and the names of nearby Wi-Fi networks.

## Helping out

If you know your way around Linux, Gentoo or ChromiumOS, there's plenty to do: answering questions on the forum, testing Beta and Dev builds on your own hardware, improving this guide, and, once the code is public, sending patches.
