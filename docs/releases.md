# Releases and versioning

## Current status

This repository is being prepared as the Seenla Audio documentation and distribution hub. No plugin downloads or collection installer have been published here yet.

The [README](../README.md) records the development snapshot from October 17, 2026. A development version is not a published release. Actual download availability and compatibility will be stated in [GitHub Releases](https://github.com/seanlimailbox-coder/Seenla-Audio/releases).

## Collection releases

Use one collection release to group a set of plugin builds. Title it **Open Collection — YYYY-MM-DD** and use a date-based tag such as `collection-YYYY.MM.DD`. The date identifies the collection; each plugin retains its own version number.

If multiple collection releases are needed on the same day, append a revision to the tag, such as `collection-YYYY.MM.DD.2`. Publish experimental or incomplete builds as prereleases and state their limitations.

Every release should include:

- A table with every included plugin's exact embedded version, format, and processor support.
- A short description of the changes since the previous collection.
- Minimum macOS requirements and a link to [installation instructions](installation.md).
- Signing and notarization status for each downloadable package.
- Any known issues or changes affecting saved sessions, automation, sound, or latency.
- SHA-256 checksums for the final uploaded assets.

Use the [release-note template](../.github/RELEASE_TEMPLATE.md) when drafting a release.

## Download organization

Attach packages to GitHub Releases rather than committing them to the documentation branch. Offer a collection `.pkg` installer when it is ready, along with individual plugin archives when useful.

Use filenames that state what the asset contains. For example:

- `Open-Collection-macOS-universal-YYYY-MM-DD.pkg`
- `Open-Clipper-macOS-universal-vX.Y.Z.zip`
- `SHA256SUMS.txt`

These are naming examples, not currently available downloads. A filename is descriptive metadata; verify the bundle contents and architecture before publishing.

## Preparing a release

1. Select the intended source version of each plugin and prepare the required builds.
2. Confirm that each bundle's embedded version matches its filename and release table. In particular, do not reuse an older ZIP simply because a newer development build exists.
3. Verify each included processor architecture and plugin format in a compatible host. Record validation results and known limitations.
4. Assemble the installer or archives, complete any signing and notarization, then calculate checksums from the final files.
5. Review the release notes and asset list together before publishing.
6. Update the README's release status and link to the newly published release. Keep development snapshots clearly distinct from available downloads.

Retain previous release assets so users can identify and return to a specific version. Describe any withdrawn build and its replacement in the release notes.
