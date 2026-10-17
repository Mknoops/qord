# Writing a native plugin

A Qord plugin re-creates what an Equicord plugin does for the user, in C++, through
a small, typed API. It never patches the client: it implements **hooks** the client
calls at fixed points, and uses **services** for anything it needs to reach. Every
hook and service needs a **permission**, declared in the plugin's manifest and
granted by the user. Start a plugin with `tools/new_plugin.py` (see
[CONTRIBUTING.md](../CONTRIBUTING.md)); this page explains what it generates and how
the pieces fit.

- [Anatomy](#anatomy)
- [The manifest](#the-manifest)
- [The plugin class](#the-plugin-class)
- [Hooks](#hooks)
- [Services](#services)
- [Settings](#settings)
- [Async work](#async-work)
- [Testing](#testing)
- [When the API doesn't have what you need](#when-the-api-doesnt-have-what-you-need)

## Anatomy

```
plugins/<category>/<id>/
    plugin.json        the manifest: who, what, which hooks and permissions, settings
    <Name>.cpp         the plugin class and its registration function
    <Name>.hpp         (optional) when the test needs the class or helpers
tests/plugins/tst_<id>.cpp   its Qt Test
```

`plugins/CMakeLists.txt` adds the plugin with `qord_add_plugin(<id> <category>
SOURCES ...)`, and `tests/plugins/CMakeLists.txt` its test with
`qord_add_plugin_test(<id>)`. The manifest is compiled in, and the client registers
every plugin through its `qord_register_<id>()` function. Plugin sources must not use
Qt Widgets (so they can be tested headless); a settings widget goes in `UI_SOURCES`.

## The manifest

```json
{
  "id": "blockkeywords",
  "equicordName": "BlockKeywords",
  "equicordPath": "src/equicordplugins/blockKeywords",
  "name": "BlockKeywords",
  "description": "Upstream's description",
  "summary": "Hides messages with words you choose",
  "categories": ["privacy"],
  "tags": ["Privacy"],
  "authors": ["catcraft", "secp192k1"],
  "permissions": ["hide_messages"],
  "hooks": ["messageFilter"],
  "settings": [ ... ],
  "enabledByDefault": false,
  "target": "",
  "dependencies": [],
  "commands": [],
  "notes": "How it behaves in Qord.",
  "gaps": ["What doesn't match upstream yet."],
  "notApplicable": ["Upstream behavior for a Discord feature Qord doesn't have at all."]
}
```

- `equicordName` and `equicordPath` must match the pinned inventory
  (`data/equicord/plugins.json`); `tools/new_plugin.py` fills them in.
- `authors` credits the **original** authors. Keep them.
- `contributors` (optional) credits whoever added to the Qord port beyond upstream;
  the settings page shows "Qord additions by ...".
- `settingsNote` (optional) is upstream's note shown above the plugin's settings.
- `summary` is the README's one-liner; `description` stays upstream's.
- `notes` and `gaps` end up in the
  [compatibility matrix](equicord-plugin-compatibility.md). Be honest in `gaps`: a
  plugin with gaps stays *Tested* and never becomes *Complete* until they're gone.
- `notApplicable` is narrower: upstream behavior aimed at a Discord feature Qord
  doesn't have at all (FakeNitro unlocking soundboard sounds, when Qord has no
  soundboard). It's listed in the matrix but doesn't hold the plugin back. If Qord
  has the feature, or the plugin could reasonably add what's missing, it's a gap.
- The host refuses to start a plugin whose `hooks()` differ from the manifest, and
  `tools/check_equicord_plugins.py` rejects unknown hooks or permissions.

## The plugin class

```cpp
class BlockKeywordsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageFilter; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override { ... }

    bool shouldHideMessage(const MessageContext &message) override { ... }

private:
    PluginContext *context = nullptr;
};
```

A plugin is constructed when the user enables it and destroyed when they disable
it, so a disabled plugin costs nothing. Allocate in `start()`, release in `stop()`.
Hook functions (in `src/Plugins/Plugin.hpp`, each with a comment) have do-nothing
defaults; override the ones in your `hooks()`. An exception thrown from a hook is
caught and logged by the host, so a broken plugin can't take the client down (but
don't rely on it: Qt code doesn't expect exceptions).

## Hooks

Every hook, the permission it needs, and the plugins to read as examples. Each
hook's functions and their exact meaning are in `src/Plugins/Plugin.hpp`.

<!-- BEGIN GENERATED: hooks -->
| Hook | What it's for | Permission | Plugins using it |
|---|---|---|---|
| `onBeforeSend` | Change or hold back a message before it is sent | `modify_outgoing_messages` | ClearURLs, FakeNitro, FavoriteEmojiFirst, Ingtoninator, PolishWording, SendTimestamps, SilentMessageToggle, TalkInReverse, TextReplace, Translate, Unindent, WriteUpperCase |
| `onBeforeEdit` | Change a message edit before it is sent | `modify_outgoing_messages` | ClearURLs, FakeNitro, Unindent |
| `shouldMentionOnReply` | Whether a reply pings its author | `reply_mentions` | NoReplyMention |
| `transformCopiedMessageLink` | Rewrite message links you copy | `modify_message_links` | NormalizeMessageLinks |
| `transformIncomingContent` | Change how a received message's text is rendered | `modify_displayed_messages` | FakeNitro, TextReplace, Unindent |
| `chatBarButtons` | Buttons next to the message box | `add_ui_elements` | Ingtoninator, PreviewMessage, SendTimestamps, SilentMessageToggle, SilentTyping, TalkInReverse, Translate |
| `userContextMenu` | Items in a user's right-click menu | `add_ui_elements` | CopyUserMention, CopyUserURLs, FollowVoiceUser, PermissionsViewer, ReviewDB, ShowMeYourName, ViewIcons, ViewRaw |
| `messageContextMenu` | Items in a message's right-click menu | `add_ui_elements` | DecodeBase64, ImageZoom, MessageLogger, ReverseImageSearch, Translate, UnitConverter, UnsuppressEmbeds, ViewRaw |
| `messageHistory` | Keep deleted and edited messages on screen | `log_messages` | MessageLogger |
| `messageFilter` | Hide messages, or keep them out of unread counts | `hide_messages` | BlockKeywords, NoBlockedMessages |
| `typing` | Whether to send, and show, typing | `control_typing` | SilentTyping |
| `linkWarnings` | Skip the "are you sure?" before opening a link | `trust_links` | AlwaysTrust |
| `notifications` | Whether a message notifies you | `control_notifications` | OnePingPerDM |
| `authorNames` | The name shown for a message's author | `modify_displayed_messages` | ShowMeYourName |
| `channelContextMenu` | Items in a server's or channel's right-click menu | `add_ui_elements` | MessageLogger, PermissionsViewer, PinDMs, ReviewDB, ServerInfo, ViewIcons, ViewRaw |
| `serverListButtons` | Buttons above the servers in the server list | `add_ui_elements` | ReadAllNotificationsButton, ServerListIndicators |
| `memberCounts` | Numbers above the member list and in server tooltips | `add_ui_elements` | MemberCount |
| `userDecorations` | Icons after user names (clickable), in messages, the member list and profiles | `read_presences` | PlatformIndicators, UserVoiceShow |
| `profileImages` | Clicks on avatars and banners in profiles | `add_ui_elements` | ViewIcons |
| `voiceStates` | Voice joins, moves and leaves; labels for people in voice | `read_voice_states` | CallTimer, FollowVoiceUser |
| `messageClicks` | Clicks on messages (double-click to edit, ...) | `add_ui_elements` | MessageClickActions |
| `reactionUsers` | Who is shown inside a reaction | `read_reactions` | WhoReacted |
| `profileRoleButtons` | Small buttons next to "Roles" in profiles | `add_ui_elements` | PermissionsViewer |
| `hiddenChannels` | Show channels you can't see, and their lock screen | `show_hidden_channels` | ShowHiddenChannels |
| `attachmentButtons` | Buttons on text file previews | `add_ui_elements` | CopyFileContents |
| `channelTyping` | An indicator next to channels someone is typing in | `add_ui_elements` | TypingIndicator |
| `unknownMentions` | Hovering a mention of a user the client doesn't know | `fetch_users` | ValidUser |
| `keyboardShortcuts` | Key presses in the chat | `add_ui_elements` | QuickReply |
| `messageAccessories` | Text, links and embeds shown under a message | `modify_displayed_messages` | MessageLinkEmbeds, Translate, UnitConverter |
| `profileTabs` | Tabs in user profiles | `add_ui_elements` | MutualGroupDMs, ReviewDB |
| `dmPins` | How the DM list is grouped, with its headers and menus | `add_ui_elements` | PinDMs |
| `emojiSuggestions` | The emoji autocomplete's order, and items in the emoji picker's menu | `add_ui_elements` | CopyEmojiMarkdown, FavoriteEmojiFirst |
| `friendsSections` | Tabs on the Friends page | `read_relationships` | ImplicitRelationships |
| `userPanelButtons` | Buttons next to mute and deafen, with a right-click menu | `add_ui_elements` | GameActivityToggle |
| `deviceSessions` | How devices look in Settings → Devices, with buttons; new and changed sessions | `read_sessions` | BetterSessions |
| `memberships` | Your friends, incoming requests, servers and group DMs, and when one of them removes you | `read_memberships` | RelationshipNotifier |
| `highlights` | Treat a message as if it mentioned you: the gold highlight and a mention's notification | `control_notifications` | KeywordNotify |
| `inboxTabs` | Tabs of messages in the inbox next to Mentions, with remove and Clear All | `add_ui_elements` | KeywordNotify |
| `messagePopover` | Buttons on a message's hover bar, before the client's own; left and right click | `add_ui_elements` | DecodeBase64, Translate, UnitConverter, ViewRaw |
| `roleContextMenu` | Items in a role's right-click menu (server settings' role list, PermissionsViewer) | `add_ui_elements` | ViewRaw |
| `profileTheme` | The theme colours a profile is shown with (FakeProfileThemes) | `modify_displayed_messages` | FakeProfileThemes |
<!-- END GENERATED: hooks -->

## Services

A plugin reaches the client through `PluginContext`. Each accessor returns null
unless the plugin has the permission, so check before use.

| Accessor | Permission | What it gives |
|---|---|---|
| `network()` | `network` | HTTPS GET, POST and any other method (`request`); requests are cancelled when the plugin stops |
| `clipboard()` | `write_clipboard` | Copy text |
| `ui()` | `add_ui_elements` (`openUrl` also needs `open_links`, `authorizeApp` needs `authorize_apps`) | Toasts, notifications (with an icon), dialogs, forms and list windows, opening channels, profiles, images and settings, replying and editing, joining and leaving voice, local messages only you can see, logging in to a Discord app (OAuth2) |
| `relationships()` | `read_relationships` | Friends, blocked and ignored users, user affinities |
| `members()` | `read_guild_members` | A member's roles |
| `guilds()` | `read_server_permissions` | Servers, channels, roles, permission overwrites, members |
| `users()` | `fetch_users` | Whether a user is known; fetching one |
| `expressions()` | `read_expressions` | Emojis, stickers, Nitro and channel permissions |
| `reactions()` | `read_reactions` | Who reacted |
| `messages()` | `log_messages` or `hide_messages` | Re-rendering and annotating messages |
| `messageActions()` | `edit_messages` | Message flags on Discord (such as suppressing embeds) |
| `messageLookup()` | `fetch_messages` | Messages and channels that links point to |
| `readStates()` | `mark_read` | Marking channels read |
| `privateChannels()` | `read_dms` | Your DMs and group DMs |
| `voiceStates()` | `read_voice_states` | Who is in voice where, muted or deafened |
| `audio()` | `control_audio` | Voice volume limits |
| `activity()` | `control_activity` | Whether Discord shares your game activity; your connections' activity sharing |
| `sessions()` | `read_sessions` | The devices logged in to the account; refreshing Settings → Devices |
| `presences()` | `read_presences` | Whether someone is online, idle, do not disturb or offline |
| `displayOptions()` | `modify_displayed_messages` | Switches for how the client draws things (role colors, image lens, folders...) |

`settings()` is always there. `refreshChatBar()`, `refreshDmList()`,
`refreshFriends()`, `refreshUserPanel()`, `refreshServerList()`, `refreshInbox()` and `refreshProfileTabs()` ask the client to query your hooks again after your state
changed. The full interfaces are in `src/Plugins/Services.hpp`.

## Settings

Settings are declared in the manifest and edited in **Settings → Plugins**; read
them with `context->settings().boolValue("key")` (or `intValue`, `numberValue`,
`stringValue`, `stringListValue`), and react to changes in `onSettingChanged()`.

| `type` | Value | Options |
|---|---|---|
| `bool` | true/false | |
| `int`, `number` | a number | `min`, `max` |
| `string` | text | `multiline`; `secret` keeps it in the system keychain (API keys) |
| `choice` | one of `choices` (`[{ "value": "...", "label": "..." }]`) | |
| `stringlist` | a list of strings | |
| `json` | anything, edited by a widget the plugin provides (`UI_SOURCES`) | |
| `action` | none: a button that calls `onSettingAction(key)` | |

Every setting needs `key`, `type`, `default` and `label`; `description` is shown
under it, and `hidden` keeps it out of the page (state the plugin saves itself).
Keep upstream's keys and defaults so the behavior matches.

## Async work

Network replies and other callbacks can arrive after your plugin stopped. Services
that say so drop them for you; for the rest, guard with a token that dies in
`stop()`:

```cpp
std::weak_ptr<bool> weak = alive; // alive = std::make_shared<bool>(true) in start()
service->fetch(..., [this, weak](...) {
    if (weak.expired())
        return; // the plugin stopped
    ...
});
```

Never block: no waiting loops and no polling timers. Work happens in response to
hooks and replies.

## Testing

`tests/plugins/PluginTestUtils.hpp` gives you `SinglePluginHost`: your plugin,
loaded through its real manifest, with fake services you can fill and inspect
(`h.network`, `h.clipboard`, `h.ui`, `h.relationships`, `h.voiceStates`, ...).
Drive it through the host, as the client does:

```cpp
SinglePluginHost h(&qord_register_blockkeywords);
QVERIFY(h.enableWithAllPermissions());
h.host->settings(h.id)->setValue("keywords", "spoiler");
MessageContext message;
message.content = "big spoiler ahead";
QVERIFY(h.host->dispatchShouldHideMessage(message));
```

Test what the user would notice: what is shown, sent, hidden or copied, what the
settings change, and that nothing happens without the permissions. When upstream
has exact rules (a regex, a sort order, a text format), compare against them. Then
check it by hand in the mock server (see CONTRIBUTING.md).

## When the API doesn't have what you need

Many plugins need one thing the client doesn't offer yet. Adding it is welcome; keep
it general enough that other plugins can use it too, and record it in
`docs/decisions.md`.

**A new hook:**
1. Add it to `enum class Hook` in `src/Plugins/Permission.hpp` (the next free bit)
   and bump `HookCount`.
2. Add it to the table in `src/Plugins/Permission.cpp` with its manifest name and the
   permission it needs.
3. Add its virtual functions to `src/Plugins/Plugin.hpp`, with a comment.
4. Add a `dispatch...()` to `PluginHost` and call it from the client where the
   behavior belongs (check `hasHook()` first so it costs nothing when unused).
5. Add it to `HOOK_DISPATCH` and `HOOK_DOCS` in `tools/qord_inventory.py`.

**A new permission:** add it to `enum class Permission` and the table in
`Permission.cpp` (with the description users see), and to `PERMISSIONS` in
`tools/qord_inventory.py`.

**A new service:** declare it in `src/Plugins/Services.hpp`, add it to `Services`
and to a `PluginContext` accessor gated by its permission, implement it in
`src/Plugins/Client/ClientPlugins.*`, and add a fake to `PluginTestUtils.hpp`.
