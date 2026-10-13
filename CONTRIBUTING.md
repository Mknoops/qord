# Contributing to Qord

Thanks for helping! Qord is a native C++/Qt Discord client that brings Equicord's
plugins back as native code. There is plenty to do at every size, from a
twenty-line plugin to new parts of the client, and **you don't need a Discord
account, Windows or a paid anything**: the whole project builds on Linux and runs
against a local mock of Discord.

- [Ways to help](#ways-to-help)
- [Set up in ten minutes](#set-up-in-ten-minutes)
- [Your first plugin](#your-first-plugin)
- [Trying things in the mock](#trying-things-in-the-mock)
- [What a pull request needs](#what-a-pull-request-needs)
- [House rules](#house-rules)
- [Getting help](#getting-help)

## Ways to help

| You'd like to... | Start here |
|---|---|
| Port a plugin that needs no client changes | The **good first** plugins in [docs/roadmap.md](docs/roadmap.md) |
| Port a bigger plugin | The rest of the [roadmap](docs/roadmap.md): each one says what the client is missing |
| Work on the client (UI, voice, Discord features) | Open issues, or anything that bothers you as a user |
| Improve the plugin API | The *Needs* column of the roadmap: one new hook often unlocks several plugins |
| Fix or report a bug | The *Bug report* issue template |
| Ask for a plugin | The *Plugin request* issue template |
| Write docs | Anything here that confused you is worth fixing |

## Set up in ten minutes

On Linux (Windows: see [docs/building.md](docs/building.md)):

```bash
git clone --recursive https://github.com/Mknoops/qord.git && cd qord
# Qt 6.10+, plus the libraries listed under "Building" in the README
cmake -B build -G Ninja -DUSE_VCPKG=OFF -DALLOW_PLAIN_CURL=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build -j8
python3 -m unittest discover -s tools/tests   # the repository tooling (Python 3.10+)
```

If Qt 6.10 isn't packaged for your distribution, conda-forge's `qt6-main` works
well. `-DENABLE_VOICE=OFF -DENABLE_FFMPEG=OFF` gives a quicker first build if you
won't touch voice or video.

## Your first plugin

1. **Pick one** from the *good first* plugins in [docs/roadmap.md](docs/roadmap.md),
   and say in an issue or draft PR that you're on it, so nobody doubles up.
2. **Read upstream.** Every plugin links to its Equicord source in
   [docs/equicord-plugin-compatibility.md](docs/equicord-plugin-compatibility.md).
   Qord ports the *behavior* (what the user sees and can do), never the code: no
   JavaScript runs in Qord.
3. **Scaffold it:**

   ```bash
   python3 tools/new_plugin.py BlockKeywords --hook messageFilter
   ```

   This writes the manifest (with upstream's name, path, description and authors),
   a plugin class with `hooks()` filled in, a test file, and the CMake entries, and
   adds the permissions your hooks need.
4. **Write it.** [docs/writing-plugins.md](docs/writing-plugins.md) explains hooks,
   permissions, services and settings, with the plugins to copy from for each.
5. **Test it** in `tests/plugins/tst_<id>.cpp` through the fake services in
   `tests/plugins/PluginTestUtils.hpp`, then delete the `QORD_SCAFFOLD_TEST` marker.
   Until you do, the status tool reports the plugin as *Implemented*, not *Tested*.
6. **Try it in the mock** (below), then refresh the generated docs:

   ```bash
   python3 tools/check_equicord_plugins.py --write-docs
   ```

   This updates the compatibility matrix, the roadmap and the plugin table in the
   README from your manifest.

## Trying things in the mock

`tools/mock_discord.py` stands in for Discord's gateway, REST API, CDN and voice
servers, with servers, channels, friends, DMs and someone streaming in a voice
channel. The client only talks to it on `127.0.0.1` and keeps a separate
`Qord-Mock` profile, so it can never touch a real account.

```bash
python3 tools/mock_discord.py --port 8765 &
QORD_MOCK_DISCORD=http://127.0.0.1:8765 ./build/acheron
# Make the mock do things while the client runs:
python3 tools/mock_discord.py say --channel general --user ava "hello"
```

Turn your plugin on in **Settings → Plugins**. Starting the mock with
`QORD_MOCK_LARGE=1` makes the second server report 1,500 members, and with
`QORD_MOCK_FOLDERS=1` puts it in a server folder.
If your plugin needs data the mock lacks (a profile field, an endpoint), add it to
`tools/mock_discord.py` in the same pull request. Plugins that call outside
services (translation, GitHub...) must be answered by the mock too: add the host to
the rewrite list in `src/Qord/DevMock.cpp`.

## What a pull request needs

- **It builds and every test passes:** `ctest` and the tooling tests above.
- **Plugins:** a manifest with every permission it uses and honest `notes`; `gaps`
  listing what doesn't match upstream yet; tests of what the plugin does (not just
  that it loads); the original authors credited in the manifest and source header.
- **Generated docs are current:** run `tools/check_equicord_plugins.py --write-docs`
  (CI fails otherwise).
- **A decision entry** in [docs/decisions.md](docs/decisions.md) when you add a hook,
  service or permission, change behavior on purpose, or differ from upstream.
- **Checked in the mock** for anything visible, and say what you checked in the
  pull request. Say plainly what you could *not* check (real Discord, Windows...).
- Small, focused pull requests are reviewed fastest. One plugin per pull request is
  ideal; client changes a plugin needs can go in the same one.

## House rules

These keep Qord what it is; pull requests that break them can't be merged.

- **No browser engines or JavaScript runtimes**: no Electron, Chromium, QtWebEngine,
  WebView or JS interpreter, not even for one plugin. CI checks this.
- **No telemetry.** Nothing reports usage anywhere.
- **Never log or store tokens, passwords or API keys** outside the system keychain.
  Plugin settings marked `"secret": true` live in the keychain.
- **Permissions are honest.** A plugin declares what it touches; the host enforces it.
- **Don't test against real Discord with someone's account without their consent.**
  Use the mock. Third-party clients are against Discord's Terms of Service; keep that
  risk with the user who chooses it.
- **Credit and license.** Qord is GPL-3.0, like Acheron, Equicord and Vencord. Ported
  plugins keep their original authors' credit.
- Match the surrounding code: Qt containers and strings, Qt 5.15 compatibility where
  the client already keeps it, no needless dependencies.

## Getting help

Open an issue or a draft pull request with what you're stuck on, even early. Good
places to read code first:

- `plugins/chat/copyusermention/`: the smallest complete plugin (a menu item).
- `plugins/chat/noblockedmessages/`: hiding messages (`messageFilter`).
- `plugins/utility/translate/`: network, secret settings and accessories under messages.
- `src/Plugins/Plugin.hpp`: every hook, with comments.
- [QORD.md](QORD.md): a short map of the code.
