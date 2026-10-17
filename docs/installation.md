# Installation and compatibility

Download packages only when they appear on the [Releases page](https://github.com/seanlimailbox-coder/Seenla-Audio/releases). No downloadable builds are currently published in this repository.

## Choose the right package

Read the release notes before downloading. Check the plugin versions, minimum macOS version, processor support, plugin formats, and signing/notarization status.

| Package label | Processor support |
| --- | --- |
| `universal` | Apple silicon and Intel; the host loads the appropriate architecture |
| `arm64` | Apple silicon, using a compatible native host |
| `x86_64` | Intel; use on Apple silicon depends on the host and Rosetta |

Universal support must be confirmed for the actual release asset. It should not be inferred from the product name or version.

Choose **VST3** if your DAW supports VST3 effects. Choose **AU** for Logic Pro, GarageBand, or another Audio Units host. The format available in a release is shown by its attached assets.

## If a macOS installer is provided

1. Open the downloaded `.pkg` file.
2. Follow its instructions and review the selected plugins and destination.
3. Restart your DAW and run its plugin rescan if necessary.

A collection installer may contain plugins with different version numbers. Refer to the release's version table for the exact contents. An installer is not currently published here.

## Install VST3 manually

1. Extract the ZIP and locate the `.vst3` bundle.
2. In Finder, choose **Go → Go to Folder** and enter `~/Library/Audio/Plug-Ins/`.
3. Create a `VST3` folder there if necessary, then copy the complete `.vst3` bundle into it.
4. Restart your DAW and rescan VST3 plugins if necessary.

The per-user destination is `~/Library/Audio/Plug-Ins/VST3/`. A package designed for all users may instead install to `/Library/Audio/Plug-Ins/VST3/` and require administrator approval.

Keep the `.vst3` bundle intact. Do not copy only the executable from inside it.

## Install Audio Units manually

If the release supplies an AU build, copy its complete `.component` bundle to `~/Library/Audio/Plug-Ins/Components/`. Create the `Components` folder if needed, restart your host, and use its Audio Units scan or validation controls if the plugin does not appear.

An all-users installer may use `/Library/Audio/Plug-Ins/Components/` instead.

## Update or remove a plugin

Quit your DAW before replacing or removing plugin bundles. Back up important sessions and note the previous version before updating. Read release notes for changes that can affect saved sessions, automation, processing, or latency.

To remove a manually installed plugin, remove its `.vst3` or `.component` bundle from the installation folder and rescan your DAW. If a future installer supplies additional removal instructions, follow those as well.

## If a plugin does not appear

- Confirm that the package supports your macOS version and processor.
- Check that your DAW supports the installed format and is running in a compatible architecture.
- Check both per-user and all-users folders for duplicate versions.
- Restart the DAW and use its plugin manager to rescan or inspect validation errors.
- If macOS blocks loading, record the exact message and check the release's signing/notarization notes.

See [Support](../.github/SUPPORT.md) for the details to include when asking for help.
