# Building Qord on Windows

## Prerequisites

- Visual Studio 2022 or 2026 with **Desktop development with C++** (MSVC, CMake, Ninja).
- Qt 6.7+ for `msvc2022_64` with `qtimageformats`, e.g.

  ```bash
  python -m pip install --user aqtinstall
  python -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -m qtimageformats -O C:\Qt
  ```

- Submodules: `git submodule update --init --recursive`, then `vendor\vcpkg\bootstrap-vcpkg.bat`.
- The vcpkg tool pinned by the submodule (2025-06-02) does not recognize VS 2026.
  With VS 2026, replace `vendor\vcpkg\vcpkg.exe` with vcpkg-tool `2025-12-16`
  (newer tools reject the pinned scripts' tool-data schema). Not needed with VS 2022.
- Prebuilt dependencies in `deps\` (same as Acheron's CI):
  - `deps\curl-impersonate`: lexiforest/curl-impersonate `v2.0.0`, `libcurl-impersonate-v2.0.0.x86_64-win32.tar.gz`
  - `deps\ffmpeg`: BtbN `autobuild-2026-07-31-14-10`, `ffmpeg-n8.1.2-34-g9b6c8969e0-win64-lgpl-shared-8.1.zip`
    (SHA256 `c222a490dde4e7059f45495deef6bfb98dbcacc2b43df5b607546252037aa95c`)

## Build and test

```bash
pwsh tools/build-windows.ps1
```

This configures `build\` (Ninja, MinSizeRel), builds the client and all tests,
and runs ctest with JUnit output in `build\ctest-results.xml`. Useful options:
`-Targets TestPlugin_clearurls` (build and run only plugin tests), `-NoTests`,
`-ConfigureOnly`, `-Config RelWithDebInfo`.

Run the client with Qt and ffmpeg on `PATH`:

```bash
pwsh -c '$env:PATH="C:\Qt\6.10.2\msvc2022_64\bin;$PWD\deps\ffmpeg\bin;$env:PATH"; .\build\qord.exe'
```

## Trying the UI without a Discord account

`tools/mock_discord.py` is a local stand-in for Discord's gateway, API and CDN with sample servers, channels and messages. `tools/run-mock.ps1` starts it and launches the client in mock mode, which uses a separate `Qord-Mock` profile and talks only to `127.0.0.1`:

```bash
pwsh -File tools/run-mock.ps1 -Reset
```

While it runs, you can play the other users:

```bash
python tools/mock_discord.py say --channel general --user ava "hello"
```

```bash
python tools/mock_discord.py edit --channel general "changed text"
```

```bash
python tools/mock_discord.py delete --channel general
```

## Inventory and status checks

```bash
python tools/check_equicord_plugins.py --test-results build/ctest-results.xml -v
```

```bash
python -m unittest discover -s tools/tests
```
