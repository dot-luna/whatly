# WhatsApp

Personal fork of [Whatly](https://github.com/shakaran/whatly) for Luna.

All credit goes to Whatly's maintainer, Ángel Guzmán Maeso ([@shakaran](https://github.com/shakaran)), the original [WhatSie](https://github.com/keshavbhatt/whatsie) author Keshav Bhatt, and all upstream contributors. See [upstream](https://github.com/shakaran/whatly) for features, documentation, and project details.

Install natively on Arch Linux from this checkout:

```sh
bash scripts/install.sh --prefix "$HOME/.local" --non-interactive
```

The installer adds missing dependencies and removes newly added, unused build dependencies after installation. Existing packages and runtime libraries are kept.

Open WhatsApp's Settings from the bottom navigation, then choose **Whatly Settings** above **Log out**. The app settings appear in the right pane and follow WhatsApp's font and light/dark appearance. The Settings shortcut and tray action open the same form, including before signing in.

Upstream copyright notices and the [MIT license](LICENSE) are preserved. WhatsApp branding belongs to WhatsApp/Meta; this fork is unofficial.
