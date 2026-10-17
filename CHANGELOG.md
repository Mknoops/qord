# Changelog

Qord's releases. Each version's section becomes its GitHub Release notes; the
reasoning behind each change is in [docs/decisions.md](docs/decisions.md).

## [Unreleased]

### Added

- MessageClickActions confirms copies with a toast and warns when a permission is
  missing, its Open Thread action opens or starts the message's thread, deleting a
  message MessageLogger kept takes it off the screen, and its reaction settings
  have a Pick Emoji button. "Disable in system DMs" now works.
- PreviewMessage's preview shows the files you attached too, and works with files
  and no text.
- BetterSessions' rename dialog has a Reset Name button and shows the default name
  as a placeholder. CopyEmojiMarkdown's menu item has a copy icon, and
  FollowVoiceUser's settings show upstream's note.

- FakeNitro splits a message that's over Discord's length limit into several, and
  closes and reopens a code block cut in two. Turn it off with "Split long messages".
- FakeNitro shrinks an image that's over your upload limit until it fits, instead of
  refusing it. Turn it off with "Shrink large images".
- FakeNitro shrinks a video that's over your upload limit: it's re-encoded to an MP4
  at a size and quality that fit. Animated GIFs are shrunk too, staying animated.
  Turn it off with "Shrink large videos".
- The message box counts down the last 200 characters before Discord's limit and,
  past it, says how many messages FakeNitro will split the message into.
- FakeNitro can send emojis among other text at a smaller size ("Emoji size in
  sentences") and hide the link text so people without it see only the image
  ("Hide link text").
- FakeNitro's stream quality bypass now works: Go Live offers up to 4K at 60 FPS.
- Hovering a custom emoji in a message shows its name. FakeNitro emojis and
  stickers also say that they're FakeNitro ones.

- A user's right-click menu has Add Friend, Remove Friend, accepting or cancelling a
  friend request, Block (after asking) and Unblock, as in Discord.
- Plugins can add buttons to a message's hover bar and items to a role's right-click
  menu. DecodeBase64, Translate, UnitConverter and ViewRaw have their upstream hover
  buttons; Translate's and UnitConverter's results show their icon.
- ViewRaw shows the raw data of servers, channels, users, profiles (from the
  profile's new More button) and roles, as well as messages.
- Role names in a profile can be right-clicked (Copy Role ID, and plugin items).
- Clicking someone in the member list opens their profile.
- A plugin setting can show only while another setting has a given value; Translate
  shows the DeepL key only for DeepL and the Kagi session only for Kagi.
- A plugin's page says so when an update asks for a permission that hasn't been
  allowed yet.
- FakeNitro "Fake reactions" (off by default): the reaction picker offers every
  emoji, and reacting with one you can't use replies to the message with it.
- In a channel where you can't embed links but can attach files, FakeNitro sends
  fake emojis and stickers as image files ("Upload where links don't embed").
- A sticker is sent with what's in the message box, and shift-clicking stickers
  collects up to three (shown above the box, each removable) to send together.
  FakeNitro handles each of them, and sends an animated sticker you can't use as a
  GIF with the message rather than stopping the send.
- FakeNitro's GIFs (shrunk GIFs and animated stickers) are smaller and much faster
  to make: frames are encoded in parallel, each frame after the first stores only
  what changed, repeated frames are merged, and colour tables fit the palette.
- Shrinking a video shows a progress bar with Cancel. FakeNitro can keep sharpness
  (15 FPS) or smoothness (up to 60 FPS) instead of a balance, keep only the first
  minute, 30 or 15 seconds, and cuts a video too long to fit at all to the part
  that fits.
- FakeProfileThemes: profile theme colours hidden in bios (as invisible text) are
  shown on profiles; pick yours in its settings, copy the text for your bio, and
  preview it. Qord now also shows Nitro profile themes, and plugin settings can be
  colours.

### Changed

- FakeNitro sends an emoji kept to roles you don't have as a link, so other people
  see the emoji rather than its name.

### Fixed

- ImplicitRelationships no longer logs an error for an account Discord has no
  affinities for. Discord answers 404 then, and the older affinity list is used.
- An error or notice about an attachment (such as "exceeds the upload limit") was
  never shown while the message box had no other files.
- MessageClickActions took every click on a message as a double click, so a single
  click could start an edit or a reply, and each action ran twice.
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
- UnitConverter no longer converts a number and unit inside a word, such as the
  "8g" in a Base64 string or the "5 g" of "5 great".
- An "&" in a plugin's settings button showed as an underline.

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
