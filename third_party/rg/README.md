# Vendored ripgrep

This directory holds the static `rg` binary copied into the app bundle at
build time for the content-search feature.

## Layout

```
third_party/rg/
├── README.md                       (this file)
├── macos-arm64/
│   └── rg                          (executable, ~8 MB, MIT licensed)
├── macos-x86_64/
│   └── rg                          (optional; only needed for Intel builds)
└── SHA256SUM                       (sha256(rg-binary) per arch, plain text)
```

## Why checked into git, not downloaded

Decision (2026-05-19): bundling a checked-in binary makes the version
deterministic. CI runs and dev builds use the exact same `rg`. Upgrades are
a deliberate git commit.

## Refreshing

1. Download the official ripgrep release for macOS (Apple Silicon):
   ```
   curl -sL https://github.com/BurntSushi/ripgrep/releases/download/14.1.1/ripgrep-14.1.1-aarch64-apple-darwin.tar.gz | tar xz
   ```
2. Copy the unpacked `rg` binary to `macos-arm64/rg` here.
3. Append a line to `SHA256SUM`:
   ```
   shasum -a 256 macos-arm64/rg >> SHA256SUM
   ```
4. Commit. The CMake `install` rule below copies it into
   `<bundle>/Contents/Resources/rg` and ad-hoc-signs it on install.

## Runtime resolution

`RipgrepRunner::findBinary()` resolves in this order:

1. `RipgrepRunner::setBinaryOverride(...)` (test seam)
2. `<bundle>/Contents/Resources/rg` (the file from this directory)
3. `rg` on `$PATH`
4. `/opt/homebrew/bin/rg`, `/usr/local/bin/rg`

If the bundled binary is missing (e.g. you haven't run the refresh step
above on a fresh clone), content search degrades to using the system `rg`
when available. Tests skip when no `rg` is found anywhere.
