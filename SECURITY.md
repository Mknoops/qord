# Security policy

Qord holds Discord account tokens and runs plugins, so we take security reports
seriously. Thank you for helping keep its users safe.

## Supported versions

Qord has no numbered releases yet. Fixes go to the `qord` branch; please check
that a problem still exists there before reporting it.

## Reporting a vulnerability

**Please don't open a public issue for a security problem.** Report it privately
through GitHub instead: on the repository's **Security** tab, choose **Report a
vulnerability**. Include:

- what an attacker can do, and what they need first (a malicious plugin, a
  crafted message, access to the machine, ...);
- the steps to reproduce it, ideally against the mock server
  (`tools/mock_discord.py`), so nobody has to use a real account;
- the commit you tested and your platform.

**Never include a real Discord token, password or API key in a report**, even
your own. If the problem involves one, describe where it appears instead.

You can expect an acknowledgement within a week. Once a fix is merged we'll
credit you in the fix's pull request and `docs/decisions.md`, unless you'd
rather stay anonymous.

## What we consider in scope

- **Tokens and secrets**: a Discord token, password or plugin API key written to
  a log, a file, the settings, the clipboard or the network anywhere other than
  the system keychain and the service it belongs to.
- **Plugin permissions**: a plugin reaching data or actions its granted
  permissions don't cover, or one plugin reaching another's settings or secrets.
- **Untrusted content**: messages, embeds, profiles, attachments or server data
  from Discord (or a plugin's outside service, such as ReviewDB or a translator)
  that can crash Qord, run code, read or write local files, or make requests
  the user didn't ask for.
- **Network**: a request sent without TLS, or a plugin request that isn't
  HTTPS.
- **Telemetry**: anything that reports usage or identifying data anywhere.
  Qord promises not to.

## Out of scope

- Discord's own servers and API, and its Terms of Service: third-party clients
  are against them, and that risk stays with the user who chooses Qord.
- Attacks that need an attacker already in control of the user's account on the
  machine (they can read the keychain directly).
- Problems in the upstream Equicord or Vencord JavaScript plugins; Qord ports
  their behavior, not their code.
