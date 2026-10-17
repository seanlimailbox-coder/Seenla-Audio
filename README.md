# Seenla Audio

**Audio tools for shaping dynamics, transients, and level.**

Seenla Audio is the home of **Open Clipper**, **Open Compressor**, **Open Gate**, and **Open Limiter**: a collection of audio effects for macOS with visual feedback and detailed control over your sound.

This repository is the collection's documentation and release hub. It currently contains documentation only. Plugin source code and downloadable builds have not been published here.

## The collection

| Plugin | What it does | Development version¹ |
| --- | --- | --- |
| [Open Clipper](docs/plugins.md#open-clipper) | Shape transients and control peaks with clipping, oversampling, and adjustable protection. | 2.4.15 |
| [Open Compressor](docs/plugins.md#open-compressor) | Shape dynamics with flexible detection, sidechain controls, and gain-reduction history. | 2.10.8 |
| [Open Gate](docs/plugins.md#open-gate) | Control spill, noise, and sustain with gating, expansion, and external sidechain routing. | 1.8.33 |
| [Open Limiter](docs/plugins.md#open-limiter) | Control final levels with lookahead limiting, selectable styles, and loudness metering. | 7.3.36 |

¹ Development versions recorded on **October 17, 2026**. These identify the current development snapshot, not published GitHub downloads. Each plugin has its own version number.

## Downloads and compatibility

When builds are published, download them from [GitHub Releases](https://github.com/seanlimailbox-coder/Seenla-Audio/releases). Each release will identify its plugin versions, formats, processor support, minimum macOS version, and signing/notarization status.

The current project targets **VST3** and **Audio Units (AU)** on macOS. Availability is determined by the actual assets attached to each release. A development version listed above does not imply that every format or processor build is ready to download.

- [Meet the plugins](docs/plugins.md)
- [Installation and compatibility](docs/installation.md)
- [Release status and versioning](docs/releases.md)
- [Get help or report a problem](.github/SUPPORT.md)

## Feedback and contributions

Use [Issues](https://github.com/seanlimailbox-coder/Seenla-Audio/issues) for reproducible bugs and feature requests. Use [Discussions](https://github.com/seanlimailbox-coder/Seenla-Audio/discussions) for usage questions, workflows, and general feedback.

Documentation corrections are welcome. See [Contributing](.github/CONTRIBUTING.md) for the information that makes reports and suggestions useful.

## Repository guide

| Location | Contents |
| --- | --- |
| `docs/` | Plugin descriptions, installation instructions, and release guidance |
| `.github/` | Support guidance, contribution notes, issue templates, and a release-note template |
| GitHub Releases | Future downloadable packages, release notes, and checksums |

Source availability and software licensing will be documented explicitly if source code or software packages are published. The current repository does not include plugin source code or a software license.
