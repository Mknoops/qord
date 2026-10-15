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

First release of Qord, a Discord client written in C++ and Qt. It's a fork of
[Acheron](https://github.com/ouwou/acheron) with Equicord's plugins built in.
No Electron, no browser engine.

### Downloads

- **Windows 10/11:** `Qord-0.1.0-windows-x64-setup.exe` (installer, no admin needed)
  or `Qord-0.1.0-windows-x64.zip` (portable)
- **Windows 7:** `Qord-0.1.0-windows7-x64.zip` (Qt 5 build, hasn't had much testing)
- **Linux:** `Qord-0.1.0-x86_64.AppImage`
- **macOS 13+ (Apple silicon):** `Qord-0.1.0-macos-arm64.dmg`

The builds aren't signed yet, so Windows and macOS will warn you the first time you
open Qord. Setup steps for each system are in the
[README](https://github.com/mknoops/qord#installing).

### What's in it

- Everything from Acheron: servers, DMs, threads, forums, voice (with DAVE
  encryption), Go Live, camera, multiple accounts and proxies.
- 64 Equicord plugins, like MessageLogger, ShowHiddenChannels, Translate, ReviewDB,
  KeywordNotify and FakeNitro. Each one asks for the permissions it needs when you
  turn it on. The full list is in the README.
- No telemetry. Your login is stored in the system keychain.

### Good to know

- Using a third-party client is against Discord's Terms of Service, so use it at
  your own risk.
- There's no auto-update yet. New versions show up on the Releases page.
- Coming from Acheron? Qord keeps its own settings, so you'll need to log in again.
