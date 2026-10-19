# Seenla Audio

Audio tools for shaping dynamics, transients, and level.

## Open Dynamics — Beta 1 · October 18, 2026

**[Download the Beta 1 installer](https://github.com/seanlimailbox-coder/Seenla-Audio/releases/tag/beta-1-2026-10-18)**

A single macOS installer for **Open Clipper**, **Open Compressor**, **Open Gate**, and **Open Limiter**. Select any plugins and choose **Audio Unit (AU)**, **VST3**, or both for each one.

| Plugin | Beta 1 version | Formats |
| --- | --- | --- |
| [Open Clipper](docs/plugins.md#open-clipper) | 2.4.23 Beta 1 | AU + VST3 |
| [Open Compressor](docs/plugins.md#open-compressor) | 2.10.15 Beta 1 | AU + VST3 |
| [Open Gate](docs/plugins.md#open-gate) | 1.8.37 Beta 1 | AU + VST3 |
| [Open Limiter](docs/plugins.md#open-limiter) | 7.3.48 Beta 1 | AU + VST3 |

**Compatibility:** macOS 11 or later; universal Apple Silicon and Intel builds. The installer places plugins in the current account's `~/Library/Audio/Plug-Ins/` folders.

**Beta status:** this is a prerelease. The installer is unsigned and is not notarized by Apple; the plugin bundles use ad-hoc signatures. Keep copies of your current plugins and important sessions before updating. Quit your DAW before installation.

## Getting started

1. Download the `.pkg` from the [Beta 1 release](https://github.com/seanlimailbox-coder/Seenla-Audio/releases/tag/beta-1-2026-10-18). Checksums and a version manifest are attached there too.
2. Review the plugin and AU/VST3 checkboxes on the Installation Type page.
3. Reopen your DAW and rescan its plugins if needed.

Selected formats replace the existing copies in your user plugin folders. Unselected formats, presets, and session files are left in place.

- [Meet the plugins](docs/plugins.md)
- [Installation and compatibility](docs/installation.md)
- [Release notes and versioning](docs/releases.md)
- [Get help or report a problem](.github/SUPPORT.md)

## Feedback

Use [Issues](https://github.com/seanlimailbox-coder/Seenla-Audio/issues) for reproducible bugs and feature requests, and [Discussions](https://github.com/seanlimailbox-coder/Seenla-Audio/discussions) for usage questions. Include the plugin's version, format, macOS version, processor, and DAW when reporting a problem.

This repository is the collection's documentation and release hub. Downloadable installers are attached to GitHub Releases; plugin source code is not included in this repository.
