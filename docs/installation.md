# Installation

## Requirements

- macOS 11 or later.
- An Apple Silicon or Intel Mac.
- An audio application that supports Audio Units (AU) or VST3 effects.

Choose AU for Logic Pro or another Audio Units host. Choose VST3 for a VST3-compatible host, or install both if you use both formats.

## Install

1. Quit your audio applications.
2. Download the `.pkg` from [Open Dynamics — Beta 1 · October 18, 2026](https://github.com/seanlimailbox-coder/Seenla-Audio/releases/tag/beta-1-2026-10-18) and open it.
3. On **Installation Type**, select the plugins you want. Expand each plugin to choose **AU**, **VST3**, or both.
4. Complete installation, then reopen your audio application. Rescan its plugins if needed.

All plugins and both formats are selected initially. Uncheck anything you do not need.

The installer is **unsigned and not notarized by Apple**. If macOS blocks it, include the exact message when [asking for help](../.github/SUPPORT.md).

## Plugin folders

Plugins are installed for your current macOS account:

| Format | Folder |
| --- | --- |
| AU | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

In Finder, choose **Go → Go to Folder** and paste one of these paths to open it.

Installing a selected format replaces an existing copy in that user folder. Deselecting a format leaves an existing copy installed. Your presets and session files are not removed.

## If a plugin does not appear

- Check that your host supports the format you installed.
- Restart the host and use its plugin manager to rescan.
- Check for duplicate copies in both `~/Library/Audio/Plug-Ins/` and `/Library/Audio/Plug-Ins/`.
- Note any scan or validation error and include it in a [support report](../.github/SUPPORT.md).

## Remove a plugin

Quit your audio application, then remove the plugin's `.component` or `.vst3` bundle from the relevant folder above. Reopen and rescan your host.

## Check the download

The release includes `SHA256SUMS.txt` for anyone who wants to verify the downloaded files. Save it beside the installer, open Terminal in that folder, and run:

```sh
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

The installer should report `OK`.
