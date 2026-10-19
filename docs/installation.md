# Installation and compatibility

The current download is **[Open Dynamics — Beta 1 · October 18, 2026](https://github.com/seanlimailbox-coder/Seenla-Audio/releases/tag/beta-1-2026-10-18)**.

## Requirements

- macOS 11 or later.
- Apple Silicon or Intel Mac. Every included AU and VST3 bundle is universal (`arm64` + `x86_64`).
- A host supporting Audio Units or VST3 effects.

The installer is unsigned and not notarized by Apple. The plugin bundles use local ad-hoc signatures. If macOS blocks installation or loading, note the exact message; see [Support](../.github/SUPPORT.md). These beta builds have not been tested in every host or workflow.

## Install the beta

1. Back up your existing plugins and important sessions, then quit your audio applications.
2. Download `Open-Dynamics-Beta-1-macOS-universal-2026-10-18.pkg` from the release above and open it.
3. On **Installation Type**, select the plugins you want. Expand each plugin to choose **Audio Unit (AU)**, **VST3**, or both. All eight components are initially selected.
4. Complete installation, then reopen your audio applications and rescan if needed.

AU is used by Logic Pro and other Audio Units hosts. Choose VST3 for VST3-compatible hosts, or both if you use both formats.

## Where files go

This installer works with the **current macOS account only**.

| Format | Destination |
| --- | --- |
| AU | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

A selected format replaces its existing copy in that user folder. Deselecting a format leaves any existing copy in place; it does not uninstall it. Presets and session files are preserved. Copies in the all-users `/Library/Audio/Plug-Ins/` folders are not changed.

Beta 1 contains only the versions listed in its release notes. The format choices are not a historical-version selector.

## Verify the download

Download `SHA256SUMS.txt` alongside the installer. In Terminal, change to that download folder and run:

```sh
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

The installer should report `OK`. The release also includes a text readme and a version manifest.

## Existing sessions and removal

Plugin names and IDs are preserved so existing sessions can find updated instances. Open a copy of an important session first and check its settings and playback.

To remove a format, quit your DAW and remove that plugin's complete `.component` or `.vst3` bundle from the relevant user folder above. Reopen and rescan your host. Keep a previous installer or plugin copy if you want to roll back.

## If a plugin does not appear

- Check that your host supports the selected format and macOS version.
- Check both user and all-users plugin folders for duplicate copies.
- Restart your host and inspect its plugin manager or validation errors.
- Include the exact error, plugin version, architecture, format, and host version in a [support report](../.github/SUPPORT.md).
