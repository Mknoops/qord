## What this does

<!-- What changes for the user, and why. For a plugin: which one, and how it compares to upstream. -->

## Checklist

- [ ] Builds, and `ctest` plus `python3 -m unittest discover -s tools/tests` pass
- [ ] `python3 tools/check_equicord_plugins.py --write-docs` was run (generated docs are current)
- [ ] Plugins: manifest permissions, `notes` and `gaps` are accurate; original authors credited; tests cover the behavior (no `QORD_SCAFFOLD_TEST` marker left)
- [ ] New hooks, services, permissions or deliberate differences from upstream have a `docs/decisions.md` entry
- [ ] No browser engine, JavaScript runtime, telemetry, or logged/stored tokens

## How it was checked

<!-- What you tried in the mock server or a client, and what you could not check (real Discord, Windows, ...). -->
