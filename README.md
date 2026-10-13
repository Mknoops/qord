# Qord

**A native Discord client with Equicord's plugins, and no browser inside.**

[![Qord inventory](https://github.com/Mknoops/qord/actions/workflows/qord.yml/badge.svg?branch=qord)](https://github.com/Mknoops/qord/actions/workflows/qord.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE.txt)
[![Contributions welcome](https://img.shields.io/badge/contributions-welcome-brightgreen.svg)](CONTRIBUTING.md)

Qord is a desktop Discord client written in C++ with Qt Widgets. It is a fork of
[Acheron](https://github.com/ouwou/acheron), and it re-implements the behavior of
[Equicord](https://github.com/Equicord/Equicord) (a Vencord fork) plugins as native
plugins. Qord ships no Electron, Chromium, QtWebEngine or WebView, and has no
JavaScript runtime. Equicord's TypeScript is used only as a reference for how
each plugin should behave. Qord never runs or embeds it.

> **Status: early development.** Plugins and features are added in small batches.
> Most of the work has been checked against Qord's local mock server, and much of it
> has not yet been verified against real Discord. See [Project status](#project-status)
> for details.
>
> **Want to help?** Porting a plugin is the easiest way in: pick one from the
> [roadmap](docs/roadmap.md) (the *good first* ones need no client changes), run
> `python3 tools/new_plugin.py <EquicordName>` and follow [CONTRIBUTING.md](CONTRIBUTING.md).
> You can build and test everything against the local mock server, without a Discord
> account.

---

## Contents

- [Why Qord](#why-qord)
- [Features](#features)
  - [Discord client](#discord-client)
  - [Voice, screen sharing and video](#voice-screen-sharing-and-video)
  - [Look and feel](#look-and-feel)
  - [Plugin system](#plugin-system)
  - [Plugins](#plugins)
- [Privacy and safety](#privacy-and-safety)
- [Project status](#project-status)
- [Building](#building)
- [Trying it without a Discord account](#trying-it-without-a-discord-account)
- [Contributing](#contributing)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [License and credits](#license-and-credits)

---

## Why Qord

| | Discord / Equicord | Qord |
|---|---|---|
| Runtime | Electron (Chromium + Node.js) | Native C++ / Qt 6 |
| Plugins | JavaScript that patches Discord's webpack/React internals | Native C++ plugins called through typed hooks |
| Idle memory | Hundreds of MB | About 50 MB working set (see [performance](docs/performance.md)) |
| Startup | Seconds | About 130 ms to the main window (warm, no account) |
| Plugin cost when disabled | Code still loaded | Never constructed, and no hook-table entries |

Qord aims to:

1. Work as a normal Discord client: messages, DMs, servers, threads, forums, voice,
   uploads, reactions and embeds.
2. Bring back the user-visible behavior of every Equicord plugin that can be done
   natively, as a real, tested native plugin.
3. Stay light: low idle memory, fast startup, no background polling, and plugins
   that start only when enabled.

---

## Features

### Discord client

From Acheron, extended by Qord:

- **Messaging:** send, edit, delete, reply (with a reply-ping toggle), pin, react, and
  upload files. Embeds, image viewer, animated emojis and Lottie stickers.
- **Full Discord markdown:** code blocks with highlighting, quotes, headings, subtext,
  lists, spoilers, timestamps, and user, role, channel, `@everyone` and command mentions.
- **DMs and group DMs, threads and forums.**
- **Servers:** guild folders, unread and mention indicators, a red **NEW** divider at
  the first unread message, date dividers, and server management.
- **Friends page:** Online / All / Pending / Blocked. You can add a friend, accept or
  ignore a request, remove a friend, unblock someone, or send a message. Plugins can
  add tabs (ImplicitRelationships adds *Implicit*).
- **Add a Server:** create a server or join one with an invite.
- **Emoji, GIF and sticker pickers:** frequently used emojis, per-server emojis, every
  Unicode emoji, and search. The same picker adds reactions.
- **Slash commands:** a command menu with options, application interactions, and the
  "used /name" line.
- **Channel header:** pinned messages, a member list toggle, message search, and the
  mentions inbox.
- **User panel:** status, custom status, mute and deafen.
- **Devices** (Settings): every device logged in to your account, and logging them out.
- **Typing indicators:** both shown and sent.
- **Notifications:** desktop notifications for DMs and mentions (tray and taskbar
  flash), a message chime, and per-server, per-channel and per-DM mute and
  notification settings. Do Not Disturb is respected.
- **Multiple accounts** side by side, **QR code login**, and per-channel tabs.
- **Browser impersonation** (curl-impersonate) to avoid Discord's spam filters.
- **Cross-platform:** Windows (7+ with the Qt 5 build), Linux and macOS.

### Voice, screen sharing and video

- **Voice chat** with Discord's end-to-end encryption (DAVE, through libdave/mlspp),
  Opus, and RNNoise noise suppression.
- **Click to join:** clicking a voice channel joins it, as in Discord.
- **Go Live (screen sharing):**
  - Discord's picker, with Applications and Screens tabs and live thumbnails.
  - Resolution and frame rate limited to what your account allows: 720p30 without
    Nitro; up to 1080p60, 1440p and 4K with Nitro or a boosted server.
  - Hardware H.264 encoding (NVENC, AMF, Quick Sync), falling back to OpenH264 or
    Media Foundation.
  - Windows capture: DXGI desktop duplication for screens and Windows.Graphics.Capture
    for windows, with fallbacks.
  - **Stream sound:** on Windows, a shared window streams that program's sound and a
    shared screen streams everything except Qord, so the call never echoes. On Linux it
    streams the PulseAudio/PipeWire output, and the call is included.
  - A red **LIVE** badge in the channel list. Click a live user to watch their stream
    in a viewer window (double-click or F11 for full screen). The stream's sound plays
    with the call.
- **Camera video in calls:** turn your camera on from the voice panel (and pick a camera
  when you have several). Everyone's camera appears in a video grid with your own
  mirrored self view, and a camera icon marks who has video on. 720p30. Cameras are
  captured through Media Foundation on Windows and V4L2 on Linux.

### Look and feel

- Discord's dark palette and layout by default: server rail, channel sidebar, member
  list, round avatars, uppercase categories, rounded highlights, red mention badges,
  a gold highlight on messages that mention you, and a rounded message box with an
  upload button.
- A theme manager. Acheron's own colour theming can be turned off.
- **Role colors** as Discord's accessibility setting offers them: on names, as a dot
  next to names, or not at all (Settings → Appearance).
- Hover highlights on channel rows.

### Plugin system

Qord has its own plugin host (`src/Plugins`), designed to be safe and cheap:

- **Manifests:** each plugin has a `plugin.json` with its name, description,
  Equicord origin, original authors, settings, hooks and permissions.
- **Permissions:** declared in the manifest and enforced at dispatch. Examples are
  `modify_outgoing_messages`, `network`, `write_clipboard` and `read_presences`.
  A plugin can't reach a hook it hasn't declared.
- **Typed settings,** edited in **Settings → Plugins**.
- **Lazy:** a disabled plugin is never constructed, and with no plugin on a hook,
  dispatch costs one branch (benchmarked in the tests).
- **Hooks** cover sending, editing, rendering, reply pings, copying links, chat bar
  buttons, context menus, typing, notifications, message clicks, voice states,
  reactions, expressions, clickable icons after names, profile tabs, Friends page
  tabs, DM list grouping, attachments and keyboard shortcuts.
- **Services** give plugins what they ask permission for: the network, the clipboard,
  servers and their permissions, relationships, DMs, voice states, messages,
  translations' secret keys (kept in the system keychain), toasts, notifications and dialogs.
- **Tested:** every plugin has its own Qt Test (`tests/plugins/tst_<id>.cpp`), and
  `tools/check_equicord_plugins.py` derives each plugin's status from evidence in the
  repository rather than from hand-edited labels.

### Plugins

<!-- BEGIN GENERATED: plugin-table -->
These 56 native plugins are available now (out of the 369 in the pinned Equicord inventory). All are off by default; turn them on in **Settings → Plugins**, where each one lists the permissions it asks for.

| Plugin | What it does | Permissions |
|---|---|---|
| **AlwaysTrust** | Removes the untrusted-domain popup | trust_links |
| **BetterFolders** | Open server folders on a second rail, and rules for when folders open and close | modify_displayed_messages |
| **BetterRoleDot** | Role dots (Appearance → Role colors → next to names) copy their color when clicked; optionally keep colored names too | modify_displayed_messages |
| **BetterSessions** | Names for your devices in Settings → Devices, exact times, NEW badges, and a notification when a device logs in | read_sessions, add_ui_elements |
| **CallTimer** | Call timers for every user in voice channels and in the connection status | read_voice_states |
| **ClearURLs** | Removes tracking parameters from URLs you send | modify_outgoing_messages, network |
| **CopyFileContents** | A copy button on text file previews | add_ui_elements, write_clipboard |
| **CopyUserMention** | "Copy Mention" in the user context menu | add_ui_elements, write_clipboard |
| **CopyUserURLs** | "Copy User URL" in the user context menu | add_ui_elements, write_clipboard |
| **DontRoundMyTimestamps** | Rounds relative timestamps down (7.6y shows as 7y, not 8y) | modify_displayed_messages |
| **FakeNitro** | Sends emojis and stickers you can't use as links; Nitro-quality streaming | modify_outgoing_messages, modify_displayed_messages, read_expressions, network, add_ui_elements |
| **FavoriteEmojiFirst** | Favorite emojis first in the emoji autocomplete, and your own aliases for emojis | add_ui_elements, modify_outgoing_messages |
| **ForceOwnerCrown** | The owner crown in the member list even in large servers | modify_displayed_messages |
| **GameActivityToggle** | A button next to mute and deafen that turns your game activity on and off; right-click for Spotify | add_ui_elements, control_activity |
| **ImageZoom** | A magnifying lens in the image viewer (hold, scroll to zoom, Shift+scroll to resize) | modify_displayed_messages, add_ui_elements |
| **ImplicitRelationships** | An "Implicit" tab on the Friends page: people you talk to who aren't your friends | read_relationships |
| **Ingtoninator** | Adds "ington" to a random word in your message | modify_outgoing_messages, add_ui_elements |
| **MemberCount** | Online, total and in-voice member counts in the member list | add_ui_elements |
| **MessageClickActions** | Double-click to edit or reply, hold Backspace and click to delete, and more | add_ui_elements, edit_messages, write_clipboard, open_links |
| **MessageLinkEmbeds** | A preview under messages that link other messages | modify_displayed_messages, fetch_messages |
| **MessageLogger** | Keeps deleted and edited messages visible for the session | log_messages, add_ui_elements |
| **MutualGroupDMs** | A "Mutual Groups" tab in profiles with the group DMs you share | add_ui_elements, read_dms |
| **NoBlockedMessages** | Hides messages from blocked and ignored users completely | hide_messages, read_relationships |
| **NoReplyMention** | Turns reply pings off by default | reply_mentions, read_guild_members |
| **NormalizeMessageLinks** | Strips canary/ptb from message links | modify_message_links |
| **OnePingPerDM** | Only one notification sound per DM until you read it | control_notifications |
| **PermissionsViewer** | "View Permissions" for members, channels and servers, and a member's permissions with the role granting each | add_ui_elements, read_server_permissions |
| **PinDMs** | Pin DMs into colored, collapsible categories at the top of the DM list | add_ui_elements |
| **PlatformIndicators** | Desktop, mobile and web indicators next to users | read_presences, modify_displayed_messages |
| **PolishWording** | Tidies capitalisation and punctuation in your messages | modify_outgoing_messages |
| **QuickReply** | Reply (Ctrl+Up/Down) and edit (Ctrl+Shift+Up/Down) messages from the keyboard | add_ui_elements, read_relationships |
| **ReadAllNotificationsButton** | A button that marks every server as read | add_ui_elements, mark_read |
| **RelationshipNotifier** | A notification when a friend, group DM or server removes you, even while Qord was closed | read_memberships, read_relationships, add_ui_elements |
| **ReverseImageSearch** | "Search Image" in image context menus | add_ui_elements, open_links |
| **ReviewDB** | Reviews of users and servers from ReviewDB: read them in profiles, and write, vote on and report them | add_ui_elements, network, authorize_apps, open_links, read_relationships |
| **RoleColorEverywhere** | Role colors in mentions, member list headers, voice users and (optionally) message text | modify_displayed_messages |
| **SendTimestamps** | A chat-bar button and text shortcuts for Discord timestamps | modify_outgoing_messages, add_ui_elements |
| **ServerInfo** | A server's details, and your friends, mutual users, blocked and ignored users in it | add_ui_elements, read_server_permissions, read_relationships |
| **ShowConnections** | Connected accounts in profiles as a row of icons, with GitHub organizations | modify_displayed_messages, network |
| **ShowHiddenChannels** | Shows channels you can't see, with a lock screen listing their settings and who can see them | show_hidden_channels, read_server_permissions, add_ui_elements |
| **ShowMeYourName** | Shows any combination of nicknames, display names and usernames | modify_displayed_messages, add_ui_elements |
| **SilentMessageToggle** | Chat-bar toggle for sending silent messages | modify_outgoing_messages, add_ui_elements |
| **SilentTyping** | Hides that you're typing | control_typing, add_ui_elements |
| **TalkInReverse** | Reverses your message before sending | modify_outgoing_messages, add_ui_elements |
| **TextReplace** | Find-and-replace rules (plain text and regex) for your messages | modify_outgoing_messages, modify_displayed_messages |
| **Translate** | Translate messages (shown under them) and, optionally, your own before sending, with Google, DeepL or Kagi | add_ui_elements, network, modify_outgoing_messages, modify_displayed_messages |
| **TypingIndicator** | Who is typing, next to channels in the channel list | add_ui_elements, read_relationships, read_server_permissions |
| **Unindent** | Trims leading indentation from code blocks | modify_outgoing_messages, modify_displayed_messages |
| **UnsuppressEmbeds** | Brings back suppressed embeds | add_ui_elements, edit_messages |
| **UserVoiceShow** | A speaker next to people in voice; click to open their channel, double-click to join | read_presences, read_voice_states, add_ui_elements |
| **ValidUser** | Hover an "@unknown-user" mention to look the user up and show their name | fetch_users |
| **ViewIcons** | Click avatars and banners to open them full size; "View Icon/Banner/Avatar" menu items | add_ui_elements |
| **ViewRaw** | View and copy the raw content and JSON of messages, channels and servers | add_ui_elements, write_clipboard |
| **VolumeBooster** | User volume sliders go past 200% (up to 1,000%) | control_audio |
| **WhoReacted** | Avatars of the people who reacted, in each reaction (click one for their profile) | read_reactions |
| **WriteUpperCase** | Capitalises the first letter of each sentence | modify_outgoing_messages |
<!-- END GENERATED: plugin-table -->

<!-- BEGIN GENERATED: up-next -->
**Up next:** BlockKeywords, FollowVoiceUser, CopyEmojiMarkdown, ServerListIndicators, PreviewMessage, DecodeBase64. The whole list, most useful first, is in [docs/roadmap.md](docs/roadmap.md).

**Good first plugins** (the plugin API already covers them, so no client changes are needed): BlockKeywords, FollowVoiceUser, CopyEmojiMarkdown, DecodeBase64, UnitConverter. See [CONTRIBUTING.md](CONTRIBUTING.md).
<!-- END GENERATED: up-next -->

The status of every Equicord plugin (Not Started, In Progress, Tested, Complete or
Blocked, with notes and known gaps) is in
[docs/equicord-plugin-compatibility.md](docs/equicord-plugin-compatibility.md).

---

## Privacy and safety

- **No telemetry.** Qord does not collect or send usage data.
- **Tokens and passwords are never logged** and never written outside the system
  keychain (QtKeychain).
- **Plugins declare their permissions,** and the host enforces them. Plugins that use
  the network (ClearURLs rule updates, FakeNitro) say so in their manifest.
- **No polling timers.** Background work happens only in response to events.
- **Mock mode** runs only against `127.0.0.1`, with a separate profile, so testing can
  never touch a real account.

> Third-party Discord clients and client modifications are against Discord's Terms of
> Service. Use Qord at your own risk.

---

## Project status

| Area | State |
|---|---|
| Base client (Acheron features) | Works |
| Qord UI and core Discord features | Works against the mock server; partly checked on real Discord |
| Go Live | Picture and sound verified against the mock server; not yet tested on real Discord |
| Camera video | Verified against the mock server on Linux; not yet tested with a real camera or on real Discord |
| Windows build | Primary platform (MSVC, Qt 6.10) |
| Linux build | Builds and passes all tests with CMake against Qt 6.10 and system libraries |
| Qt 5 build | Kept compiling (voice and ffmpeg off); not regularly tested |

<!-- BEGIN GENERATED: plugin-status -->
**Plugins:** 56 of 369 ported and tested (each has its own test), 0 of them verified in a logged-in client (Complete); 0 can't be done natively (Blocked).
<!-- END GENERATED: plugin-status -->

Design decisions are recorded in [docs/decisions.md](docs/decisions.md).

---

## Building

### Windows

See [docs/building.md](docs/building.md) for the full steps. In short:

```bash
git submodule update --init --recursive
vendor\vcpkg\bootstrap-vcpkg.bat
pwsh tools/build-windows.ps1
```

You need Visual Studio 2022 or 2026 (C++ workload), Qt 6 for `msvc2022_64` with
`qtimageformats`, and the prebuilt curl-impersonate and ffmpeg in `deps\`. The script
configures `build\`, builds the client and all tests, and runs ctest.

### Linux

Install Qt 6.10 or newer: the code uses 64-bit `QFlags` (Qt 6.9) and
`QSortFilterProxyModel::endFilterChange` (Qt 6.10). Your distribution's package or
conda-forge's `qt6-main` will do. libcurl must support WebSockets (`ws`/`wss` in
`curl --version`), because the gateway needs them; some distributions' libcurl
doesn't (Ubuntu 24.04's doesn't). curl-impersonate does. Then run:

```bash
# Debian/Ubuntu package names; use your distribution's equivalents
sudo apt install cmake ninja-build pkg-config libcurl4-openssl-dev libssl-dev zlib1g-dev \
    libopus-dev libsodium-dev nlohmann-json3-dev libsecret-1-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev

git submodule update --init vendor/emoji-segmenter vendor/libdave vendor/miniaudio \
    vendor/mlspp vendor/qrcodegen vendor/qtkeychain vendor/rnnoise

cmake -B build -G Ninja -DUSE_VCPKG=OFF -DCMAKE_BUILD_TYPE=Release -DALLOW_PLAIN_CURL=ON \
    -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build
ctest --test-dir build
```

Useful options: `-DENABLE_VOICE=OFF`, `-DENABLE_FFMPEG=OFF` (this also turns off
Go Live and video), `-DENABLE_LOTTIE=OFF`, `-DENABLE_RNNOISE=OFF` and
`-DBUILD_TESTS=OFF`. Build against curl-impersonate rather than plain libcurl if you
can.

### Dependencies

- Qt 6.10+ (Core, Gui, Widgets, Svg, Network, Sql, Concurrent), or Qt 5.15 without voice and video
- libcurl-impersonate (plain libcurl with WebSockets works with `ALLOW_PLAIN_CURL=ON`), OpenSSL, zlib
- QtKeychain (vendored if the system has none)
- ffmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample): media playback, Go Live and video
- libsodium, libopus, libdave and mlspp (vendored), miniaudio (vendored): voice
- rnnoise (vendored): noise suppression; rlottie (optional): Lottie stickers
- emoji-segmenter and nayuki QR-Code-generator (vendored)

---

## Trying it without a Discord account

`tools/mock_discord.py` is a local stand-in for Discord's gateway, REST API, CDN and
voice servers. It has sample servers, channels, messages, friends and a voice channel
where "Ava" is streaming a test picture and has her camera on. Setting `QORD_MOCK_DISCORD` makes the
client use it, with a separate `Qord-Mock` profile, and it only accepts loopback
addresses:

```bash
pwsh -File tools/run-mock.ps1 -Reset          # Windows: start the mock and the client
# Linux/macOS: python3 tools/mock_discord.py --port 8765, then
#   QORD_MOCK_DISCORD=http://127.0.0.1:8765 ./build/acheron
python tools/mock_discord.py say --channel general --user ava "hello"
python tools/mock_discord.py edit --channel general "changed text"
python tools/mock_discord.py delete --channel general
```

---

## Contributing

Contributions are welcome, and you don't need a Discord account or Windows to make
one: everything builds on Linux and runs against the local mock server.

- **Port a plugin.** [docs/roadmap.md](docs/roadmap.md) lists the next plugins, most
  useful first; the *good first* ones need no client changes. `tools/new_plugin.py`
  creates the manifest, source, test and build entries for you.
- **Extend the client.** Many plugins wait on a hook, service or display option; the
  roadmap says which. [docs/writing-plugins.md](docs/writing-plugins.md) explains how
  hooks, permissions and services fit together.
- **Report bugs or ask for a plugin** with the issue templates.

Start with [CONTRIBUTING.md](CONTRIBUTING.md): building, running the mock, testing,
and what a pull request needs.

---

## Repository layout

| Path | What's there |
|---|---|
| `src/` | The client: Acheron's code plus Qord's additions (`src/Qord`, `src/UI/Qord`) |
| `src/Plugins/` | The plugin host: manifests, permissions, settings and dispatch (no UI) |
| `src/Plugins/Client/`, `src/UI/Settings/PluginsPage.*` | Client glue and the Plugins settings page |
| `src/Qord/Video/` | Go Live and video: capture, H.264 encoding and decoding, RTP |
| `plugins/<category>/<id>/` | Native plugins (`plugin.json` plus C++) |
| `tests/`, `tests/qord/`, `tests/plugins/` | Acheron's tests, Qord's tests, and one test per plugin |
| `data/equicord/` | Pinned Equicord inventory, source version and status overrides |
| `tools/` | Mock server, inventory and status tools, Windows build and measurement scripts |
| `qord.cmake` | Everything Qord adds to Acheron's build, in one `include()` |
| `data/equicord/roadmap.json` | The plugins to port next, in order (renders `docs/roadmap.md`) |
| `docs/` | Decisions, build steps, performance, plugin status rules, compatibility matrix, roadmap, plugin guide |

## Documentation

- [CONTRIBUTING.md](CONTRIBUTING.md): how to contribute
- [docs/writing-plugins.md](docs/writing-plugins.md): how to write a native plugin
- [docs/roadmap.md](docs/roadmap.md): the plugins to port next
- [docs/building.md](docs/building.md): building and testing on Windows
- [docs/decisions.md](docs/decisions.md): architecture and design decisions
- [docs/performance.md](docs/performance.md): startup and memory measurements, and budgets
- [docs/plugin-status.md](docs/plugin-status.md): how plugin statuses are derived
- [docs/equicord-plugin-compatibility.md](docs/equicord-plugin-compatibility.md): every Equicord plugin and its Qord status
- [docs/equicord-source-version.md](docs/equicord-source-version.md): the pinned Equicord inventory
- [QORD.md](QORD.md): a short map of the code

---

## License and credits

Qord is licensed under the **GPL-3.0**, like Acheron, Equicord and Vencord. See
[LICENSE.txt](LICENSE.txt).

- [Acheron](https://github.com/ouwou/acheron) by ouwou and contributors: the client
  Qord is built on.
- [Equicord](https://github.com/Equicord/Equicord) and
  [Vencord](https://github.com/Vendicated/Vencord): the plugins whose behavior Qord
  re-implements. Each Qord plugin credits its original authors in its manifest and
  source headers.

Qord is not affiliated with or endorsed by Discord Inc.
