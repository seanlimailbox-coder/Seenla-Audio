# Build from source

## Requirements

- macOS 11 or later, with Apple's C++ build tools and a macOS SDK.
- CMake 3.22 or later and Git.
- JUCE is included as a pinned Git submodule; clone it with the project.

## Get the source

```sh
git clone --recurse-submodules https://github.com/seanlimailbox-coder/Seenla-Audio.git
cd Seenla-Audio
```

If you already cloned the repository without dependencies:

```sh
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 6
```

By default the macOS plugins are universal Apple Silicon/Intel builds, in AU and VST3 formats. To build just one plugin, specify its targets; for example:

```sh
cmake --build build --config Release --target OpenClipper_AU OpenClipper_VST3 --parallel 6
```

The other target prefixes are `OpenCompressor`, `OpenGate`, and `OpenLimiter`.

For an Apple Silicon-only build:

```sh
cmake -S . -B build-arm64 -DCMAKE_BUILD_TYPE=Release \
  -DSEENLA_UNIVERSAL=OFF -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-arm64 --config Release --parallel 6
```

Use `x86_64` instead of `arm64` for an Intel-only build.

## Build output

Each plugin's bundles are under `build/<target>_artefacts/Release/`:

- `AU/` contains the `.component` bundle.
- `VST3/` contains the `.vst3` bundle.

Builds receive a local ad-hoc signature after their bundle resources are generated. They are not Developer ID signed or notarized.

Builds are not installed automatically. Quit your host before copying a complete bundle into the relevant [plugin folder](installation.md#plugin-folders).

## Dependencies

JUCE is pinned to 8.0.9. CMake applies the included compatibility patch once during configuration. It enables Clipper's timestamped VST3 automation and Limiter's popup transitions. Reconfiguring the same checkout does not reapply it. Git will show the patched JUCE submodule as modified after configuration.

Third-party components retain their own licensing terms; see [Third-party notices](../THIRD_PARTY_NOTICES.md).
