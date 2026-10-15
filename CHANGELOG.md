# Changelog

Qord's releases. Each version's section becomes its GitHub Release notes; the
reasoning behind each change is in [docs/decisions.md](docs/decisions.md).

## [Unreleased]

### Fixed

- Behind a system or environment proxy, avatars, images and media now load for an
  account without a proxy of its own, like its messages do.
- When no account connects at startup, the accounts window opens instead of an
  empty window.
- The user panel no longer shows a disconnected account as online.
- Leftover "Acheron" wording in settings, dialogs and the keyring message.
- Joining voice where the connection can't be set up (UDP blocked) now leaves the
  channel, instead of staying listed in it with no audio.
- Plugin buttons in the message box sit in the same row as the GIF and emoji buttons.
- A selected or hovered DM keeps its avatar visible, and the name no longer touches it.

## [0.1.0]

The first release of Qord: a native Discord client in C++ and Qt, based on
[Acheron](https://github.com/ouwou/acheron), with Equicord's plugins rewritten in C++.
No Electron, web engine or JavaScript runtime.

### Install

Download the file for your system below; nothing has to be built. See
[Installing](https://github.com/mknoops/qord#installing) for each system's steps.

- **Windows 10/11:** `Qord-0.1.0-windows-x64-setup.exe` (installer, no administrator
  rights needed) or `Qord-0.1.0-windows-x64.zip` (portable).
- **Windows 7 SP1+:** `Qord-0.1.0-windows7-x64.zip` (Qt 5 build, less tested).
- **Linux:** `Qord-0.1.0-x86_64.AppImage` (glibc 2.35+).
- **macOS 13+, Apple silicon:** `Qord-0.1.0-macos-arm64.dmg`.

These builds are not code-signed yet, so Windows SmartScreen and macOS Gatekeeper
warn on the first start.

### Highlights

- Acheron's client: servers, DMs, threads and forums, voice with DAVE end-to-end
  encryption, Go Live and camera video, multiple accounts, proxies.
- A plugin host with declared, enforced permissions, and 64 Equicord plugins ported
  and tested, among them ReviewDB, Translate, KeywordNotify, MessageLogger, ShowHiddenChannels and FakeNitro. The full list is in the README.
- No telemetry. Logins are kept in the system keychain, never in files or logs.

### Known limitations

- Most features are checked against a local mock of Discord; parts are not yet
  verified on real Discord.
- Third-party clients are against Discord's Terms of Service. Use at your own risk.
- No automatic updates: new versions are published on the Releases page.
- If you used Acheron, Qord keeps separate settings and asks you to log in again.
