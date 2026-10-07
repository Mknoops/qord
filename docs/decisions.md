# Decisions

Decisions made while building Qord, newest last. Each says what was decided
and why, so it can be revisited deliberately.

## 2026-10-07: setup and batch 1

1. **Base pinned at Acheron `e0191a1696e72c800c629527dbec5e0671ff654e`** (2026-10-06), as surveyed. Upstream remote stays `origin`; Qord work lives on the `qord` branch.
2. **C++20, not C++23.** Acheron builds as C++20 and its CI covers MSVC, GCC on Ubuntu 22.04 (AppImage) and Apple Clang. Nothing in batch 1 needs C++23; raising the standard would be a portability risk for no gain. Revisit when a feature needs it.
3. **vcpkg tool on VS 2026.** The vcpkg tool pinned by the submodule (2025-06-02) does not recognize VS 2026's v145 toolset; the newest tool (2026-09-26) rejects the pinned scripts' schema. Locally, `vendor/vcpkg/vcpkg.exe` is vcpkg-tool `2025-12-16` (Microsoft-signed). The submodule and package baseline are unchanged; CI is unaffected (it bootstraps on its own image).
4. **Inventory facts are computed, and one stated fact was wrong.** The brief said 7 plugins are `required` + `enabledByDefault`. The pinned file has 7 `required`, 7 `enabledByDefault`, and 0 with both. `docs/equicord-source-version.md` reports the computed numbers. `target`, `dependencies` and `commands` are optional fields (present on 28, 89 and 15 entries).
5. **Manifest `dependencies` lists Qord plugin ids.** Equicord's dependencies on API plugins (`MessageEventsAPI`, `MessagePopoverAPI`, ...) are host features in Qord, not plugins, so they are not listed. The host enables a plugin's dependencies with it and disables dependents with their dependency.
6. **Permission grants.** Enabling a plugin in the Plugins page asks once for the permissions it declares but hasn't been granted; each can be revoked individually afterwards. Plugins marked `enabledByDefault` get their declared permissions on first run only (a user's later choice sticks). A hook is dispatched only if its permission is declared *and* granted; `PluginContext` hands out services (network, member roles) only when granted.
7. **Hook dispatch order is plugin id order**, so results don't depend on registration order.
8. **Plugin state** lives in `%APPDATA%/ouwou/Acheron/plugins/`: `host-state.json` (enabled, grants), `settings-<id>.json`, `data/<id>/` (caches). App name and data directory stay Acheron's for now so upstream changes merge cleanly; renaming the app to Qord is a branding decision for the project owner.
9. **Reply pings.** Acheron had no reply-ping control: every reply pinged. Qord adds Discord's `@ ON/OFF` toggle to the reply bar. Clicking Reply defaults to ping unless Shift is held (Discord's behavior), then `shouldMentionOnReply` plugins may decide. When the ping is off the send request carries `allowed_mentions: {parse: [users, roles, everyone], replied_user: false}`, the same shape the official client sends; when it is on the request is unchanged from Acheron's.
10. **Received-message transforms** (`transformIncomingContent`, used by TextReplace's "others' messages" scope) run in `MessageManager::parseMessageContent`, change only the rendered HTML, and never the stored message. Synthesized system texts are not passed to plugins. Messages already rendered update when the channel is re-rendered; upstream also only applies changes on re-render.
11. **Sending an empty result.** If `onBeforeSend` plugins reduce a message with no attachments to whitespace, nothing is sent (Discord would reject it).
12. **Fidelity testing.** Ported logic is checked against golden outputs produced by running the upstream plugin code under Node.js (`tests/plugins/data/generate-*.mjs`). Node is a development aid only; nothing at build or run time uses it.
13. **JavaScript semantics.** `src/Plugins/JsCompat` reproduces what ported code relies on: RegExp flags (`g i m s y`), `$&`/`$1`/`$<name>`/`` $` ``/`$'`/`$$` substitution, `\uXXXX`/`[^]`, end-of-input `$`, JS whitespace and `trim`.
14. **ClearURLs** fetches ClearURLs' `data.min.json` (no bundled copy), caches it, and refreshes at start when older than 7 days. The client's network service only allows HTTPS, caps responses at 16 MiB and allows at most 5 same-or-safer redirects. Providers are applied in file order like upstream (`QJsonObject` would sort them).
15. **NoReplyMention** matches ids exactly; upstream does a substring test on the raw text (`"12,34".includes("2")`), which can match partial ids. Default lists are empty instead of upstream's placeholder ids; behavior with empty lists is the same.
16. **NormalizeMessageLinks** is ported and wired, but Qord's Copy Message Link already produces `discord.com` links (there is no canary/ptb build to inherit a host from), so it has no visible effect today. Kept for parity and for any future host choice.
17. **Unindent's display half is a gap.** Upstream also unindents code blocks when rendering. Acheron's Markdown parser renders ``` blocks through the inline-code rule and has no code-block node to hook. This is recorded as a manifest `gap`, so Unindent cannot reach Complete until Qord has a fenced code-block renderer. That renderer is shared infrastructure many plugins will need.

## 2026-10-07: batch 2

18. **Two shared UI surfaces.** *Chat bar buttons* (`chatBarButtons` hook): plugins return buttons (id, tooltip, SVG icon, active) for the current channel; clicks go back to the plugin; a plugin calls `PluginContext::refreshChatBar()` when its buttons change. They sit to the right of the message box (`UI/Input/PluginChatButtons`) and are re-queried on channel switches. *User context menu items* (`userContextMenu` hook) are appended after "Copy User ID". Both need the new `add_ui_elements` permission; copying needs `write_clipboard` (`PluginContext::clipboard()`).
19. **`@silent` in the base client.** Discord's web client turns a leading `@silent ` into the `SUPPRESS_NOTIFICATIONS` flag and strips it; Acheron sent it as text. Qord's send path now does the same after plugins run, which SilentMessageToggle relies on.
20. **Hidden settings** (`"hidden": true`) hold plugin state that should persist but has no editor (SilentMessageToggle's last state).
21. **Randomness is injectable.** Plugins that use `Math.random()` (Ingtoninator, PolishWording) take a random source so tests replay upstream's exact draws.
22. **JS helpers** gained `split` (ES `@@split` semantics, captures spliced in), `test`, and `replace` with a callback.
23. **Behavior fixes kept deliberately small and noted per plugin**: TalkInReverse reverses by grapheme (upstream breaks emoji); WriteUpperCase ignores empty blocked words (upstream's empty setting blocks everything); SilentMessageToggle's "persist between restarts" restores the saved state (upstream starts every channel switched on); PolishWording leaves a hand-typed `__CODE_BLOCK_n__` alone (upstream writes "undefined"). PolishWording's sentence splitter, which upstream wrote so it can never split after a period, is ported unchanged because that is the behavior users get.
24. **DontRoundMyTimestamps deferred.** Acheron does not render Discord's `<t:…:R>` timestamps, so there is nothing to round yet. It needs a timestamp-markdown surface first.

## Core entries (planned, not yet evaluated in a batch)

| Equicord core entry | Planned Qord responsibility |
|---|---|
| Settings | The Plugins settings page (exists) |
| SupportHelper | Diagnostics export (versions, enabled plugins), no network |
| CrashHandler | Local-only crash log and recovery; nothing is uploaded |
| NoTrack | Confirm and document that Qord sends no telemetry. Note: Acheron downloads a Discord web asset to read the current build number (`CurlUtils.cpp`); that is a read, not tracking, but will be reviewed here |
| FixDiscordCss, ConcatenatedModules, ConcatenatedComponentExtractor | Expected Blocked: they exist for Discord's web bundle and CSS, which Qord does not have |
