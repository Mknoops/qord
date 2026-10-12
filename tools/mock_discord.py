#!/usr/bin/env python3
"""Local mock of the Discord gateway, REST API and CDN for developing Qord.

Run the server, then start the client in mock mode:

    python tools/mock_discord.py --port 8765
    set QORD_MOCK_DISCORD=http://127.0.0.1:8765 && build\\acheron.exe

The client then uses a separate "Qord-Mock" profile with one fake account
and talks only to this server (loopback only, see src/Qord/DevMock.cpp).
Nothing here is real Discord data and no real token is involved.

Simulate other users while the client is connected:

    python tools/mock_discord.py say   --channel general "hello from mock"
    python tools/mock_discord.py edit  --channel general --message last "edited text"
    python tools/mock_discord.py delete --channel general --message last
    python tools/mock_discord.py typing --channel general

Standard library only. Development tool, not shipped with the client.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import datetime as dt
import hashlib
import json
import random
import os
import struct
import sys
import urllib.request
import zlib
from typing import Any
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mock_voice  # noqa: E402  (voice gateway, media and Go Live)

DISCORD_EPOCH = 1420070400000
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# ---------------------------------------------------------------------------
# Fixture data
# ---------------------------------------------------------------------------

_counter = 0


def snowflake(ms: int | None = None) -> str:
    global _counter
    if ms is None:
        ms = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
    _counter = (_counter + 1) & 0xFFF
    return str(((ms - DISCORD_EPOCH) << 22) | _counter)


def iso(ms: int) -> str:
    return dt.datetime.fromtimestamp(ms / 1000, dt.timezone.utc).isoformat()


def ms_of(sf: str) -> int:
    return (int(sf) >> 22) + DISCORD_EPOCH


NOW = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
HOUR = 3600 * 1000

SELF_ID = "100000000000000001"  # matches Qord::DevMock::AccountId


def user(uid: str, username: str, global_name: str | None, avatar: str | None = None, bot: bool = False) -> dict:
    u = {
        "id": uid,
        "username": username,
        "global_name": global_name,
        "discriminator": "0",
        "avatar": avatar,
        "public_flags": 0,
    }
    if bot:
        u["bot"] = True
    return u


USERS = {
    "self": user(SELF_ID, "qord_tester", "Qord Tester", "a1"),
    "ava": user("200000000000000001", "ava", "Ava", "a2"),
    "ben": user("200000000000000002", "benji", "Ben", "a3"),
    "cleo": user("200000000000000003", "cleo.dev", "Cleo", "a4"),
    "dan": user("200000000000000004", "dan", None, None),
    "eve": user("200000000000000005", "eve_blocked", "Eve", "a5"),
    "bot": user("200000000000000006", "Qordbot", None, "a6", bot=True),
    "fay": user("200000000000000007", "fay", "Fay", "a7"),
    # Discord's own account: official notices, no replies
    "system": dict(user("643945264868098049", "discord", "Discord", None), system=True),
}
U = {k: v["id"] for k, v in USERS.items()}
# Someone the client isn't told about at startup (ValidUser's "@unknown-user")
STRANGER = user("200000000000000099", "gus", "Gus", "a8")

STATUS = {"self": "online", "ava": "online", "ben": "idle", "cleo": "dnd", "dan": "offline",
          "eve": "online", "bot": "online", "fay": "online"}
ACTIVITIES = {
    "ava": [{"type": 0, "name": "Factorio", "id": "a"}],
    "cleo": [{"type": 4, "name": "Custom Status", "state": "shipping code", "id": "custom"}],
    "fay": [{"type": 2, "name": "Spotify", "details": "Midnight City", "state": "M83", "id": "spotify:1"}],
}

G1 = "300000000000000001"
G2 = "300000000000000002"

# Permission bits
VIEW = 1 << 10
SEND = 1 << 11
HISTORY = 1 << 16
REACT = 1 << 6
ATTACH = 1 << 15
EMBED = 1 << 14
EXT_EMOJI = 1 << 18
CONNECT = 1 << 20
SPEAK = 1 << 21
EVERYONE_PERMS = VIEW | SEND | HISTORY | REACT | ATTACH | EMBED | EXT_EMOJI | CONNECT | SPEAK | (1 << 31)


def role(rid: str, name: str, color: int, position: int, perms: int, hoist: bool = False) -> dict:
    return {"id": rid, "name": name, "color": color, "position": position, "permissions": str(perms),
            "hoist": hoist, "managed": False, "mentionable": True, "flags": 0, "icon": None,
            "unicode_emoji": None, "colors": {"primary_color": color, "secondary_color": None,
                                               "tertiary_color": None}}


def channel(cid: str, gid: str | None, ctype: int, name: str, position: int, parent: str | None = None,
            topic: str | None = None, overwrites: list | None = None, **extra) -> dict:
    c = {"id": cid, "type": ctype, "name": name, "position": position, "parent_id": parent,
         "topic": topic, "permission_overwrites": overwrites or [], "nsfw": False,
         "rate_limit_per_user": 0, "flags": 0}
    if gid:
        c["guild_id"] = gid
    c.update(extra)
    return c


CH = {
    # Guild 1: owned by the tester
    "g1_info": "400000000000000001",
    "rules": "400000000000000002",
    "announcements": "400000000000000003",
    "g1_text": "400000000000000004",
    "general": "400000000000000005",
    "dev": "400000000000000006",
    "media": "400000000000000007",
    "g1_voice": "400000000000000008",
    "lounge": "400000000000000009",
    "gaming": "400000000000000010",
    "forum": "400000000000000011",
    # Guild 2: regular member
    "g2_text": "410000000000000001",
    "g2_general": "410000000000000002",
    "staff": "410000000000000003",
    "staff_voice": "410000000000000005",
    "g2_voice": "410000000000000004",
    # DMs
    "dm_ava": "420000000000000001",
    "dm_system": "420000000000000009",
    "group": "420000000000000002",
}

ROLES = {
    G1: [
        role(G1, "@everyone", 0, 0, EVERYONE_PERMS),
        role("500000000000000001", "Admin", 0xE91E63, 3, 8, hoist=True),
        dict(role("500000000000000002", "Moderator", 0x3498DB, 2, EVERYONE_PERMS | (1 << 13) | (1 << 1), hoist=True),
             unicode_emoji="🛡️"),
        role("500000000000000003", "Bots", 0x9B59B6, 1, EVERYONE_PERMS, hoist=True),
    ],
    G2: [
        role(G2, "@everyone", 0, 0, EVERYONE_PERMS),
        role("510000000000000001", "Staff", 0x1ABC9C, 1, EVERYONE_PERMS, hoist=True),
    ],
}

GUILD_CHANNELS = {
    G1: [
        channel(CH["g1_info"], G1, 4, "Information", 0),
        channel(CH["rules"], G1, 0, "rules", 0, CH["g1_info"], "Read these first.",
                overwrites=[{"id": G1, "type": 0, "allow": "0", "deny": str(SEND)},
                            {"id": "500000000000000002", "type": 0, "allow": str(SEND | (1 << 13)), "deny": "0"},
                            {"id": U["ben"], "type": 1, "allow": str(1 << 6), "deny": "0"}]),
        channel(CH["announcements"], G1, 5, "announcements", 1, CH["g1_info"], "News about the project"),
        channel(CH["g1_text"], G1, 4, "Text Channels", 1),
        channel(CH["general"], G1, 0, "general", 0, CH["g1_text"],
                "General chat — be nice. Docs: https://example.com/docs"),
        channel(CH["dev"], G1, 0, "development", 1, CH["g1_text"], "Talk about the code"),
        channel(CH["media"], G1, 0, "media", 2, CH["g1_text"], "Screenshots and clips"),
        channel(CH["forum"], G1, 15, "help-forum", 3, CH["g1_text"], "Ask for help",
                available_tags=[], default_reaction_emoji=None),
        channel(CH["g1_voice"], G1, 4, "Voice Channels", 2),
        channel(CH["lounge"], G1, 2, "Lounge", 0, CH["g1_voice"], bitrate=64000, user_limit=0),
        channel(CH["gaming"], G1, 2, "Gaming", 1, CH["g1_voice"], bitrate=64000, user_limit=5),
    ],
    G2: [
        channel(CH["g2_text"], G2, 4, "Chat", 0),
        channel(CH["g2_general"], G2, 0, "general", 0, CH["g2_text"], "Welcome to Second Server"),
        channel(CH["staff"], G2, 0, "staff-only", 1, CH["g2_text"], "Hidden from regular members",
                overwrites=[{"id": G2, "type": 0, "allow": "0", "deny": str(VIEW)},
                            {"id": "510000000000000001", "type": 0, "allow": str(VIEW), "deny": "0"},
                            {"id": U["fay"], "type": 1, "allow": str(VIEW), "deny": "0"}],
                nsfw=True, rate_limit_per_user=3605, default_auto_archive_duration=1440),
        channel(CH["g2_voice"], G2, 2, "Hangout", 1, CH["g2_text"], bitrate=64000, user_limit=0),
        # Hidden from the tester too (ShowHiddenChannels)
        channel(CH["staff_voice"], G2, 2, "Staff Voice", 2, CH["g2_text"], bitrate=96000, user_limit=0,
                rtc_region="rotterdam", video_quality_mode=2,
                overwrites=[{"id": G2, "type": 0, "allow": "0", "deny": str(VIEW)},
                            {"id": "510000000000000001", "type": 0, "allow": str(VIEW), "deny": "0"}]),
    ],
}

MEMBERS = {
    G1: {
        "self": ["500000000000000001"],
        "ava": ["500000000000000002"],
        "ben": [],
        "cleo": ["500000000000000002"],
        "dan": [],
        "eve": [],
        "bot": ["500000000000000003"],
        "fay": [],
    },
    G2: {"self": [], "ava": ["510000000000000001"], "ben": [], "fay": []},
}
NICKS = {(G1, "ben"): "Benny"}

GUILDS = {
    G1: {"id": G1, "name": "Qord Dev", "icon": "g1", "owner_id": SELF_ID, "description": "Mock server for Qord",
         "features": ["COMMUNITY"], "premium_tier": 2, "premium_subscription_count": 9,
         "system_channel_id": CH["general"], "rules_channel_id": CH["rules"], "verification_level": 1,
         "default_message_notifications": 1, "explicit_content_filter": 2, "mfa_level": 0,
         "nsfw_level": 0, "preferred_locale": "en-US", "banner": None, "splash": None,
         "afk_channel_id": None, "afk_timeout": 300, "max_members": 500000, "vanity_url_code": None},
    G2: {"id": G2, "name": "Second Server", "icon": None, "owner_id": U["ava"], "description": None,
         "features": [], "premium_tier": 0, "premium_subscription_count": 0,
         "system_channel_id": CH["g2_general"], "verification_level": 0,
         "default_message_notifications": 0, "explicit_content_filter": 0, "mfa_level": 0,
         "nsfw_level": 0, "preferred_locale": "en-US", "banner": None, "splash": None,
         "afk_channel_id": None, "afk_timeout": 300, "max_members": 500000, "vanity_url_code": None},
}

PRIVATE_CHANNELS = [
    {"id": CH["dm_ava"], "type": 1, "recipient_ids": [U["ava"]], "last_message_id": None, "flags": 0},
    {"id": CH["dm_system"], "type": 1, "recipient_ids": [U["system"]], "last_message_id": None, "flags": 0},
    {"id": CH["group"], "type": 3, "recipient_ids": [U["ben"], U["cleo"]], "name": "weekend plans",
     "icon": None, "owner_id": SELF_ID, "last_message_id": None, "flags": 0},
]

RELATIONSHIPS = [
    {"id": U["ava"], "type": 1, "nickname": None, "user_id": U["ava"]},
    {"id": U["ben"], "type": 1, "nickname": None, "user_id": U["ben"]},
    {"id": U["cleo"], "type": 1, "nickname": None, "user_id": U["cleo"]},
    {"id": U["fay"], "type": 3, "nickname": None, "user_id": U["fay"]},  # incoming request
    {"id": U["eve"], "type": 2, "nickname": None, "user_id": U["eve"]},  # blocked
]

# Messages, per channel, oldest first
MESSAGES: dict[str, list[dict]] = {cid: [] for cid in CH.values()}
REACTIONS_ME: set[tuple[str, str]] = set()


def png(width: int, height: int, rgb: tuple[int, int, int], gradient: bool = False) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            if gradient:
                t = x / max(1, width - 1)
                s = y / max(1, height - 1)
                rows += bytes((int(rgb[0] * (1 - t) + 40 * t), int(rgb[1] * (1 - s) + 200 * s), int(rgb[2])))
            else:
                rows += bytes(rgb)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def colour_for(key: str) -> tuple[int, int, int]:
    h = hashlib.sha1(key.encode()).digest()
    return (60 + h[0] % 160, 60 + h[1] % 160, 60 + h[2] % 160)


def attachment(name: str, w: int, h: int) -> dict:
    aid = snowflake()
    url = f"https://cdn.discordapp.com/attachments/{CH['media']}/{aid}/{name}"
    return {"id": aid, "filename": name, "size": w * h * 3, "url": url,
            "proxy_url": url.replace("cdn.discordapp.com", "media.discordapp.net"),
            "width": w, "height": h, "content_type": "image/png"}


# Text files (Discord's text preview, CopyFileContents)
BUILD_LOG = "".join(f"[{i:02d}] step {i}: compiling module_{i}.cpp ... ok\n" for i in range(1, 31)).encode()
BIG_TEXT = "".join(f"line {i:05d}: " + "lorem ipsum dolor sit amet " * 2 + "\n" for i in range(1, 1100)).encode()
TEXT_FILES = {"build.log": BUILD_LOG, "notes-big.txt": BIG_TEXT}


def text_attachment(name: str) -> dict:
    aid = snowflake()
    url = f"https://cdn.discordapp.com/attachments/{CH['dev']}/{aid}/{name}"
    return {"id": aid, "filename": name, "size": len(TEXT_FILES[name]), "url": url,
            "proxy_url": url.replace("cdn.discordapp.com", "media.discordapp.net"),
            "content_type": "text/plain; charset=utf-8"}


def message(cid: str, author: str, content: str, at_ms: int, **extra) -> dict:
    mid = snowflake(at_ms)
    ch_guild = next((g for g, chans in GUILD_CHANNELS.items() if any(c["id"] == cid for c in chans)), None)
    m = {
        "id": mid, "channel_id": cid, "author": USERS[author], "content": content,
        "timestamp": iso(at_ms), "edited_timestamp": None, "tts": False, "mention_everyone": False,
        "mentions": [], "mention_roles": [], "attachments": [], "embeds": [], "reactions": [],
        "pinned": False, "type": 0, "flags": 0, "components": [],
    }
    if ch_guild:
        m["guild_id"] = ch_guild
        roles = MEMBERS.get(ch_guild, {}).get(author)
        if roles is not None:
            m["member"] = {"roles": roles, "nick": NICKS.get((ch_guild, author)), "joined_at": iso(NOW - 900 * HOUR),
                           "deaf": False, "mute": False, "flags": 0}
    m.update(extra)
    return m


def add(cid: str, author: str, content: str, minutes_ago: float, **extra) -> dict:
    m = message(cid, author, content, int(NOW - minutes_ago * 60000), **extra)
    MESSAGES[cid].append(m)
    return m


def member_count(gid: str) -> int:
    # QORD_MOCK_LARGE=1 reports Second Server as a 1,500-member server
    # (ForceOwnerCrown: Qord hides the owner crown in large servers)
    if gid == G2 and os.environ.get("QORD_MOCK_LARGE") == "1":
        return 1500
    return len(MEMBERS[gid])


def seed_messages() -> None:
    g = CH["general"]
    add(g, "dan", "", 60 * 26, type=7)  # member join
    add(g, "ava", "morning everyone ☀️", 60 * 25)
    add(g, "ben", "hey! did anyone try the new build?", 60 * 25 - 2)
    add(g, "ava", "yes, it starts in **under a second** and uses ~50 MB", 60 * 25 - 1)
    add(g, "cleo", "nice. the *plugin* page looks good too", 60 * 3)
    first = add(g, "cleo", "here's the snippet I used:\n```cpp\nint main() {\n    return 0;\n}\n```", 60 * 3 - 1)
    add(g, "ben", "check this out https://github.com/Equicord/Equicord", 60 * 2,
        embeds=[{"type": "link", "url": "https://github.com/Equicord/Equicord", "title": "Equicord/Equicord",
                 "description": "An enhanced version of Vencord with extra plugins.", "color": 0x24292F,
                 "provider": {"name": "GitHub"}}])
    add(g, "ava", f"<@{SELF_ID}> can you look at the member list?", 50,
        mentions=[USERS["self"]], reactions=[{"emoji": {"id": None, "name": "👀"}, "count": 2, "me": False,
                                              "count_details": {"burst": 0, "normal": 2}, "burst_colors": [],
                                              "me_burst": False, "burst_count": 0}])
    add(g, "self", "sure, give me a minute", 49)
    add(g, "cleo", "replying to the snippet: works on Qt 5 too", 30,
        type=19, message_reference={"message_id": first["id"], "channel_id": g, "guild_id": G1},
        referenced_message=first, mentions=[USERS["cleo"]])
    add(g, "eve", "this message is from a blocked user", 20)
    add(g, "bot", "", 15, embeds=[{"type": "rich", "title": "Build #128 passed", "color": 0x2ECC71,
                                     "description": "All 26 tests passed on Qt 6.10 and 25 on Qt 5.15.",
                                     "fields": [{"name": "Duration", "value": "4m 12s", "inline": True},
                                                {"name": "Branch", "value": "qord", "inline": True}],
                                     "footer": {"text": "CI"}, "timestamp": iso(NOW - 15 * 60000)}])
    m = add(g, "ben", "typo in my last mesage", 10)
    m["content"] = "typo in my last message"
    m["edited_timestamp"] = iso(NOW - 9 * 60000)
    add(g, "fay", "|| spoiler || and `inline code` and ~~strike~~ and __underline__ and > quote", 5)
    add(g, "ava", "https://example.com/very/long/link?utm_source=newsletter&utm_medium=email&id=5", 2)
    add(CH["dev"], "cleo", "# Release notes\n-# draft, not final\n> quoting the spec:\n> markdown should match Discord\n"
        "- spoilers\n- timestamps\n- code blocks\nShips <t:%d:R> (<t:%d:F>), ping <@&500000000000000002> "
        "or @here. Run </build release:42>.\n```py\nprint('hi')\n```" % (NOW // 1000 + 7200, NOW // 1000 + 7200),
        1, mention_roles=["500000000000000002"])
    # A FakeNitro emoji and sticker as other clients see them: links
    fake_emoji = "https://cdn.discordapp.com/emojis/720000000000000002.webp?size=48&name=far_cat&lossless=true"
    add(CH["dev"], "ava", f"[far_cat]({fake_emoji})", 0.6,
        embeds=[{"type": "image", "url": fake_emoji,
                 "thumbnail": {"url": fake_emoji, "proxy_url": fake_emoji, "width": 48, "height": 48}}])
    fake_sticker = "https://media.discordapp.net/stickers/700000000000000003.png?size=160&name=Far+Dance&lossless=true"
    add(CH["dev"], "ben", f"[Far Dance]({fake_sticker})", 0.5,
        embeds=[{"type": "image", "url": fake_sticker,
                 "thumbnail": {"url": fake_sticker, "proxy_url": fake_sticker, "width": 160, "height": 160}}])

    for i in range(60):
        who = ["ava", "ben", "cleo", "dan"][i % 4]
        add(CH["dev"], who, f"dev log line {i + 1}: refactored module {i % 7}", 60 * 48 - i * 30)

    add(CH["media"], "ava", "screenshot of the new layout", 90, attachments=[attachment("layout.png", 480, 270)])
    add(CH["media"], "ben", "", 80, attachments=[attachment("diagram.png", 320, 320)])
    add(CH["media"], "ava", f"ask <@{STRANGER['id']}> about the render farm", 78)
    add(CH["media"], "cleo", "¿Alguien quiere probar la nueva versión esta noche?", 77)  # Translate
    add(CH["dev"], "ben", "here's the build log", 75, attachments=[text_attachment("build.log")])
    add(CH["dev"], "cleo", "and my (long) notes", 74, attachments=[text_attachment("notes-big.txt")])
    add(CH["rules"], "self", "1. Be kind\n2. No spam\n3. Have fun", 60 * 400)
    add(CH["announcements"], "self", "**Qord 0.1** is out! 🎉", 60 * 100)
    add(CH["g2_general"], "ava", "welcome to the second server", 60 * 5)
    add(CH["staff"], "ava", "secret staff stuff", 60 * 4)
    add(CH["dm_ava"], "ava", "hey, are you around later?", 60 * 6)
    add(CH["dm_ava"], "self", "yep, after 6", 60 * 6 - 3)
    add(CH["dm_ava"], "ava", "cool, ping me", 12)
    add(CH["dm_system"], "system", "Your account's security settings were updated. If this wasn't you, "
                                   "reset your password.", 60 * 30)
    add(CH["group"], "ben", "saturday or sunday?", 60 * 20)
    add(CH["group"], "cleo", "sunday works for me", 60 * 19)


seed_messages()
for _msgs in MESSAGES.values():
    _msgs.sort(key=lambda m: int(m["id"]))


def last_message_id(cid: str) -> str | None:
    msgs = MESSAGES.get(cid)
    return msgs[-1]["id"] if msgs else None


def guild_member(gid: str, key: str, with_user: bool = True) -> dict:
    m = {"roles": MEMBERS[gid][key], "nick": NICKS.get((gid, key)), "joined_at": iso(NOW - 900 * HOUR),
         "deaf": False, "mute": False, "flags": 0, "pending": False, "avatar": None, "premium_since": None}
    if with_user:
        m["user"] = USERS[key]
    else:
        m["user_id"] = USERS[key]["id"]
    return m


def presence(key: str, gid: str | None = None) -> dict:
    p = {"user_id": USERS[key]["id"], "status": STATUS[key], "activities": ACTIVITIES.get(key, []),
         "client_status": {"desktop": STATUS[key]} if STATUS[key] != "offline" else {}}
    if key == "ava":
        p["client_status"] = {"desktop": "online", "mobile": "online"}
    if key == "fay":
        p["client_status"] = {"web": "online"}
    if key == "ben":  # idle on mobile, gaming on a console (PlatformIndicators)
        p["client_status"] = {"mobile": "idle", "embedded": "idle"}
    if gid:
        p["guild_id"] = gid
    return p


COMMAND_INDEX = {
    "applications": [{"id": U["bot"], "name": "Qordbot"}],
    "application_commands": [
        {"id": "800000000000000001", "application_id": U["bot"], "version": "1", "type": 1, "name": "ping",
         "description": "Check that the bot is alive"},
        {"id": "800000000000000002", "application_id": U["bot"], "version": "1", "type": 1, "name": "roll",
         "description": "Roll a die", "options": [
             {"type": 4, "name": "sides", "description": "How many sides", "required": False}]},
        {"id": "800000000000000003", "application_id": U["bot"], "version": "1", "type": 1, "name": "echo",
         "description": "Repeat some text", "options": [
             {"type": 3, "name": "text", "description": "What to say", "required": True}]},
        # An autocomplete option (the bot suggests cities) and static choices
        {"id": "800000000000000004", "application_id": U["bot"], "version": "1", "type": 1, "name": "weather",
         "description": "Today's weather", "options": [
             {"type": 3, "name": "city", "description": "Which city", "required": True, "autocomplete": True},
             {"type": 3, "name": "units", "description": "Units", "required": False,
              "choices": [{"name": "Celsius", "value": "c"}, {"name": "Fahrenheit", "value": "f"}]}]},
    ],
    "version": "1",
}
CITIES = [("Paris", "fr-75"), ("Pretoria", "za-pt"), ("Amsterdam", "nl-ams"), ("Berlin", "de-be"),
          ("Barcelona", "es-bcn"), ("Portland", "us-pdx")]

# FakeNitro: the self user has no Nitro, so the second server's emojis and
# stickers are sent as links from the first
GUILD_EMOJIS = {
    G1: [{"id": "720000000000000001", "name": "qord_heart", "animated": False, "available": True,
          "managed": False, "roles": [], "require_colons": True}],
    G2: [{"id": "720000000000000002", "name": "far_cat", "animated": False, "available": True,
          "managed": False, "roles": [], "require_colons": True},
         {"id": "720000000000000003", "name": "party_blob", "animated": True, "available": True,
          "managed": False, "roles": [], "require_colons": True}],
}
# Discord's own packs: anyone can send these, anywhere (one Lottie, one PNG)
STICKER_PACKS = [
    {"id": "847199849233514549", "name": "Wumpus Beyond", "sku_id": "847199849233514547",
     "cover_sticker_id": "749054660769218631", "description": "Wumpus's adventures", "banner_asset_id": None,
     "stickers": [
         {"id": "749054660769218631", "name": "Wave", "tags": "wumpus, hello, hi", "description": "Wumpus waves",
          "format_type": 3, "type": 1, "pack_id": "847199849233514549", "available": True, "sort_value": 1},
         {"id": "749054660769218632", "name": "Cheer", "tags": "wumpus, yay", "description": "Wumpus cheers",
          "format_type": 1, "type": 1, "pack_id": "847199849233514549", "available": True, "sort_value": 2}]},
]
PACK_STICKERS = {st["id"]: st for pack in STICKER_PACKS for st in pack["stickers"]}

# A tiny Lottie animation (a yellow circle) for Lottie stickers
LOTTIE_STICKER = json.dumps({
    "v": "5.5.2", "fr": 30, "ip": 0, "op": 30, "w": 160, "h": 160, "nm": "mock", "ddd": 0, "assets": [],
    "layers": [{"ddd": 0, "ind": 1, "ty": 4, "nm": "circle", "sr": 1, "ip": 0, "op": 30, "st": 0, "bm": 0,
                "ks": {"o": {"a": 0, "k": 100}, "r": {"a": 0, "k": 0}, "p": {"a": 0, "k": [80, 80, 0]},
                       "a": {"a": 0, "k": [0, 0, 0]},
                       "s": {"a": 0, "k": [100, 100, 100]}},
                "shapes": [{"ty": "gr", "nm": "g", "it": [
                    {"ty": "el", "nm": "e", "d": 1, "p": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [120, 120]}},
                    {"ty": "fl", "nm": "f", "c": {"a": 0, "k": [0.996, 0.906, 0.361, 1]},
                     "o": {"a": 0, "k": 100}, "r": 1},
                    {"ty": "tr", "p": {"a": 0, "k": [0, 0]}, "a": {"a": 0, "k": [0, 0]},
                     "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}}]}]}],
}).encode()

REACTORS: dict[tuple[str, str], list[dict]] = {}  # (message id, emoji name) -> users

GUILD_STICKERS = {
    G2: [{"id": "700000000000000003", "name": "Far Dance", "tags": "dance", "description": None,
          "format_type": 1, "available": True, "guild_id": G2, "type": 2}],
    G1: [{"id": "700000000000000001", "name": "Qord Wave", "tags": "wave", "description": "Hello!",
          "format_type": 1, "available": True, "guild_id": G1, "type": 2},
         {"id": "700000000000000002", "name": "Ship It", "tags": "ship", "description": None,
          "format_type": 1, "available": True, "guild_id": G1, "type": 2}],
}
GUILD_OF = {c["id"]: gid for gid, chans in GUILD_CHANNELS.items() for c in chans}


def voice_states(gid: str) -> list:
    """Ava hangs out in the first server's voice channel (MemberCount's voice number),
    streaming a test picture (Go Live) with her camera on."""
    if gid != G1:
        return []
    return [{"user_id": U["ava"], "channel_id": CH["lounge"], "session_id": "mock-voice-ava",
             "self_mute": False, "self_deaf": False, "mute": False, "deaf": False, "suppress": False,
             "self_video": True, "self_stream": True}]


VOICE: dict[str, Any] = {"endpoint": "", "guild_id": None, "channel_id": None}


async def voice_state_update(conn, d: dict) -> None:
    """Joining or leaving voice: the voice state, then where the voice server is
    (only when the channel changes; mute and deafen are just state)."""
    moved = d.get("channel_id") != VOICE["channel_id"]
    VOICE["guild_id"], VOICE["channel_id"] = d.get("guild_id"), d.get("channel_id")
    VOICE["self_video"] = bool(d.get("self_video")) and bool(d.get("channel_id"))
    state = {"guild_id": d.get("guild_id"), "channel_id": d.get("channel_id"), "user_id": SELF_ID,
             "session_id": "mock-session", "self_mute": bool(d.get("self_mute")), "self_deaf": bool(d.get("self_deaf")),
             "mute": False, "deaf": False, "suppress": False, "self_video": VOICE["self_video"],
             "self_stream": False}
    if d.get("guild_id") in GUILDS:
        state["member"] = guild_member(d["guild_id"], "self")
    await conn.dispatch("VOICE_STATE_UPDATE", state)
    if d.get("channel_id") and moved:
        await conn.dispatch("VOICE_SERVER_UPDATE", {"token": "mock-voice-token", "guild_id": d.get("guild_id"),
                                                    "channel_id": d.get("channel_id"), "endpoint": VOICE["endpoint"]})
    log(f"gateway: voice -> {d.get('channel_id')}")


async def go_live(conn, op: int, d: dict) -> None:
    """STREAM_CREATE (18), STREAM_DELETE (19), STREAM_WATCH (20), STREAM_SET_PAUSED (22)."""
    if op == 18:
        gid, cid = d.get("guild_id"), d.get("channel_id")
        key = f"guild:{gid}:{cid}:{SELF_ID}" if gid else f"call:{cid}:{SELF_ID}"
    else:
        key = d.get("stream_key", "")
    rtc = str(int(hashlib.sha1(key.encode()).hexdigest()[:12], 16))  # numeric, like Discord's
    owner = key.rsplit(":", 1)[-1]
    if op in (18, 20):
        if owner == U["ava"]:
            mock_voice.FAKE_STREAMS[rtc] = owner
        await conn.dispatch("STREAM_CREATE", {"stream_key": key, "rtc_server_id": rtc, "region": "mock",
                                              "viewer_ids": [], "paused": False})
        await conn.dispatch("STREAM_SERVER_UPDATE", {"stream_key": key, "endpoint": VOICE["endpoint"],
                                                     "token": "mock-stream-token", "guild_id": None})
        if op == 18:
            await conn.dispatch("VOICE_STATE_UPDATE", {
                "guild_id": VOICE["guild_id"], "channel_id": VOICE["channel_id"], "user_id": SELF_ID,
                "session_id": "mock-session", "self_mute": False, "self_deaf": False, "mute": False, "deaf": False,
                "suppress": False, "self_video": VOICE.get("self_video", False), "self_stream": True})
        log(f"gateway: {'stream created' if op == 18 else 'watching'} {key}")
    elif op == 19:
        if owner == SELF_ID:
            await conn.dispatch("STREAM_DELETE", {"stream_key": key, "unavailable": False, "reason": "user_requested"})
            await conn.dispatch("VOICE_STATE_UPDATE", {
                "guild_id": VOICE["guild_id"], "channel_id": VOICE["channel_id"], "user_id": SELF_ID,
                "session_id": "mock-session", "self_mute": False, "self_deaf": False, "mute": False, "deaf": False,
                "suppress": False, "self_video": VOICE.get("self_video", False), "self_stream": False})
        log(f"gateway: stream closed {key}")


def gateway_guild(gid: str) -> dict:
    props = GUILDS[gid]
    chans = []
    for c in GUILD_CHANNELS[gid]:
        c = dict(c)
        if c["type"] in (0, 5):
            c["last_message_id"] = last_message_id(c["id"])
        chans.append(c)
    return {
        "id": gid, "properties": props, "channels": chans, "roles": ROLES[gid], "emojis": GUILD_EMOJIS.get(gid, []),
        "stickers": GUILD_STICKERS.get(gid, []),
        "threads": [], "members": [guild_member(gid, "self")], "member_count": member_count(gid),
        "joined_at": iso(NOW - 900 * HOUR), "lazy": True, "large": False, "premium_subscription_count":
        props["premium_subscription_count"], "data_mode": "full", "version": 1,
        "guild_scheduled_events": [], "stage_instances": [], "activity_instances": [],
    }


def make_guild(name: str, owner: str) -> str:
    gid = snowflake()
    GUILDS[gid] = {"id": gid, "name": name, "icon": None, "owner_id": owner, "description": None, "features": [],
                   "premium_tier": 0, "premium_subscription_count": 0, "verification_level": 0,
                   "default_message_notifications": 0, "explicit_content_filter": 0, "mfa_level": 0,
                   "nsfw_level": 0, "preferred_locale": "en-US", "banner": None, "splash": None,
                   "afk_channel_id": None, "afk_timeout": 300, "max_members": 500000, "vanity_url_code": None}
    text_cat, voice_cat, general, voice = snowflake(), snowflake(), snowflake(), snowflake()
    GUILD_CHANNELS[gid] = [
        channel(text_cat, gid, 4, "Text Channels", 0),
        channel(general, gid, 0, "general", 0, text_cat),
        channel(voice_cat, gid, 4, "Voice Channels", 1),
        channel(voice, gid, 2, "General", 0, voice_cat, bitrate=64000, user_limit=0),
    ]
    GUILDS[gid]["system_channel_id"] = general
    ROLES[gid] = [role(gid, "@everyone", 0, 0, EVERYONE_PERMS)]
    MEMBERS[gid] = {"self": []}
    MESSAGES[general] = []
    MESSAGES[voice] = []
    return gid


INVITES = {"qord": "Invited Server"}
GUILD_SETTINGS: dict = {}


def ready_payload() -> dict:
    guilds = [gateway_guild(gid) for gid in GUILDS]
    private = []
    for c in PRIVATE_CHANNELS:
        c = dict(c)
        c["last_message_id"] = last_message_id(c["id"])
        private.append(c)
    read_entries = []
    for cid in CH.values():
        lm = last_message_id(cid)
        if not lm:
            continue
        # leave #general and the DM unread; everything else read
        last_read = MESSAGES[cid][-3]["id"] if cid in (CH["general"], CH["dm_ava"]) and len(MESSAGES[cid]) > 3 else lm
        mention = 1 if cid in (CH["general"], CH["dm_ava"]) else 0
        read_entries.append({"id": cid, "last_message_id": last_read, "mention_count": mention,
                             "last_pin_timestamp": "1970-01-01T00:00:00+00:00", "flags": 0})
    return {
        "v": 9,
        "user": dict(USERS["self"], email=None, verified=True, mfa_enabled=False, premium_type=0, flags=0,
                     phone=None, bio="Testing Qord against a mock server."),
        "users": [u for k, u in USERS.items() if k != "self"],
        "guilds": guilds,
        "merged_members": [[guild_member(gid, "self", with_user=False)] for gid in GUILDS],
        "merged_presences": {"friends": [presence(k) for k in ("ava", "ben", "cleo")],
                             "guilds": [[presence(k, gid) for k in MEMBERS[gid] if k != "self"] for gid in GUILDS]},
        "private_channels": private,
        "relationships": RELATIONSHIPS,
        "read_state": {"version": 1, "partial": False, "entries": read_entries},
        "user_guild_settings": {"version": 1, "partial": False, "entries": list(GUILD_SETTINGS.values())},
        "user_settings_proto": "",
        "notes": {},
        "session_id": "mock-session",
        "resume_gateway_url": "",
        "session_type": "normal",
        "country_code": "US",
        "sessions": [],
        "analytics_token": "",
        "auth_session_id_hash": "",
        "consents": {},
        "experiments": [], "guild_experiments": [], "connected_accounts": [], "geo_ordered_rtc_regions": [],
        "tutorial": None, "api_code_version": 1, "friend_suggestion_count": 0,
    }


def member_list_update(gid: str) -> dict:
    groups: dict[str, list[str]] = {}
    hoisted = sorted([r for r in ROLES[gid] if r["hoist"]], key=lambda r: -r["position"])
    order = [r["id"] for r in hoisted] + ["online", "offline"]
    for key, roles in MEMBERS[gid].items():
        if STATUS[key] == "offline":
            gidk = "offline"
        else:
            top = next((r["id"] for r in hoisted if r["id"] in roles), None)
            gidk = top or "online"
        groups.setdefault(gidk, []).append(key)
    items = []
    group_list = []
    for gk in order:
        keys = groups.get(gk)
        if not keys:
            continue
        group_list.append({"id": gk, "count": len(keys)})
        items.append({"group": {"id": gk, "count": len(keys)}})
        for key in sorted(keys, key=lambda k: (USERS[k]["global_name"] or USERS[k]["username"]).lower()):
            m = guild_member(gid, key)
            m["presence"] = dict(presence(key), user={"id": USERS[key]["id"]})
            items.append({"member": m})
    online = sum(1 for k in MEMBERS[gid] if STATUS[k] != "offline")
    return {"guild_id": gid, "id": "everyone", "member_count": member_count(gid), "online_count": online,
            "groups": group_list, "ops": [{"op": "SYNC", "range": [0, 99], "items": items}]}


# ---------------------------------------------------------------------------
# Gateway (WebSocket + zlib-stream)
# ---------------------------------------------------------------------------

class GatewayConn:
    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        self.reader = reader
        self.writer = writer
        self.zlib = zlib.compressobj()
        self.seq = 0
        self.ready = False

    async def send_frame(self, opcode: int, payload: bytes) -> None:
        header = bytearray([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header.append(n)
        elif n < 65536:
            header.append(126)
            header += struct.pack(">H", n)
        else:
            header.append(127)
            header += struct.pack(">Q", n)
        self.writer.write(bytes(header) + payload)
        await self.writer.drain()

    async def send(self, op: int, d: Any = None, t: str | None = None) -> None:
        msg: dict[str, Any] = {"op": op, "d": d, "s": None, "t": t}
        if op == 0:
            self.seq += 1
            msg["s"] = self.seq
        data = json.dumps(msg).encode()
        await self.send_frame(0x2, self.zlib.compress(data) + self.zlib.flush(zlib.Z_SYNC_FLUSH))

    async def dispatch(self, t: str, d: Any) -> None:
        await self.send(0, d, t)

    async def recv(self) -> tuple[int, bytes] | None:
        message = bytearray()
        msg_opcode = None
        while True:
            head = await self.reader.readexactly(2)
            fin = head[0] & 0x80
            opcode = head[0] & 0x0F
            masked = head[1] & 0x80
            n = head[1] & 0x7F
            if n == 126:
                n = struct.unpack(">H", await self.reader.readexactly(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", await self.reader.readexactly(8))[0]
            mask = await self.reader.readexactly(4) if masked else b"\0\0\0\0"
            data = bytearray(await self.reader.readexactly(n))
            for i in range(n):
                data[i] ^= mask[i % 4]
            if opcode == 0x8:
                return None
            if opcode == 0x9:
                await self.send_frame(0xA, bytes(data))
                continue
            if opcode == 0xA:
                continue
            if opcode != 0:
                msg_opcode = opcode
            message += data
            if fin:
                return (msg_opcode or 1, bytes(message))


CONNECTIONS: set[GatewayConn] = set()


async def broadcast(t: str, d: Any) -> None:
    for conn in list(CONNECTIONS):
        if conn.ready:
            try:
                await conn.dispatch(t, d)
            except Exception:
                CONNECTIONS.discard(conn)


async def run_gateway(conn: GatewayConn) -> None:
    await conn.send(10, {"heartbeat_interval": 41250, "_trace": ["mock"]})
    CONNECTIONS.add(conn)
    try:
        while True:
            frame = await conn.recv()
            if frame is None:
                break
            try:
                payload = json.loads(frame[1])
            except ValueError:
                log("gateway: undecodable frame")
                continue
            op = payload.get("op")
            d = payload.get("d")
            if op in (1, 40):  # heartbeat and QoS heartbeat
                await conn.send(11)
            elif op in (2, 6):
                await conn.dispatch("READY", ready_payload())
                await conn.dispatch("READY_SUPPLEMENTAL", {
                    "guilds": [{"id": gid, "voice_states": voice_states(gid), "embedded_activities": []}
                               for gid in GUILDS],
                    "merged_members": [[guild_member(gid, k, with_user=False) for k in MEMBERS[gid] if k != "self"]
                                       for gid in GUILDS],
                    "merged_presences": ready_payload()["merged_presences"],
                    "lazy_private_channels": [], "disclose": []})
                # Two sessions of our own: this desktop client and a phone
                await conn.dispatch("SESSIONS_REPLACE", [
                    {"session_id": "all", "status": STATUS["self"], "activities": [],
                     "client_info": {"client": "unknown", "os": "unknown"}},
                    {"session_id": "mock-session", "status": STATUS["self"], "activities": [],
                     "client_info": {"client": "desktop", "os": "windows"}},
                    {"session_id": "mock-phone", "status": "idle", "activities": [],
                     "client_info": {"client": "mobile", "os": "android"}}])
                conn.ready = True
                log("gateway: identified, READY sent")
            elif op == 41:
                pass  # time-spent session id
            elif op == 4:
                await voice_state_update(conn, d or {})
            elif op in (18, 19, 20, 22):
                await go_live(conn, op, d or {})
            elif op == 37:
                for gid in (d or {}).get("subscriptions", {}):
                    if gid in GUILDS:
                        await conn.dispatch("GUILD_MEMBER_LIST_UPDATE", member_list_update(gid))
            elif op == 14:
                gid = (d or {}).get("guild_id")
                if gid in GUILDS:
                    await conn.dispatch("GUILD_MEMBER_LIST_UPDATE", member_list_update(gid))
            elif op == 3:
                STATUS["self"] = (d or {}).get("status", "online")
                log(f"gateway: presence -> {STATUS['self']}")
                await conn.dispatch("SESSIONS_REPLACE", [{"session_id": "mock-session", "status": STATUS["self"],
                                                          "activities": (d or {}).get("activities", []),
                                                          "client_info": {"client": "desktop", "os": "windows"}}])
            elif op == 8:
                gid = (d or {}).get("guild_id")
                gids = gid if isinstance(gid, list) else [gid]
                for g in gids:
                    if g in GUILDS:
                        await conn.dispatch("GUILD_MEMBERS_CHUNK", {
                            "guild_id": g, "members": [guild_member(g, k) for k in MEMBERS[g]],
                            "chunk_index": 0, "chunk_count": 1, "not_found": [],
                            "nonce": (d or {}).get("nonce")})
            else:
                log(f"gateway: op {op} ignored")
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        CONNECTIONS.discard(conn)
        log("gateway: client disconnected")


# ---------------------------------------------------------------------------
# REST
# ---------------------------------------------------------------------------

def find_message(cid: str, mid: str) -> dict | None:
    return next((m for m in MESSAGES.get(cid, []) if m["id"] == mid), None)


def resolve_channel(name_or_id: str) -> str:
    return CH.get(name_or_id, name_or_id)


def profile(uid: str) -> dict:
    key = next((k for k, u in USERS.items() if u["id"] == uid), None)
    u = USERS.get(key) if key else user(uid, "unknown", None)
    return {"user": dict(u, bio="Mock profile bio.", banner=None, accent_color=0x5865F2),
            "user_profile": {"bio": "Mock profile bio.", "accent_color": 0x5865F2, "pronouns": "",
                             "theme_colors": None, "banner": None},
            "connected_accounts": [], "premium_since": None, "premium_type": 0,
            "mutual_guilds": [{"id": gid, "nick": None} for gid in GUILDS if key in MEMBERS[gid]],
            "mutual_friends_count": 1, "badges": [], "guild_badges": []}


async def handle_rest(method: str, path: str, query: dict, body: bytes, headers: dict) -> tuple[int, Any, str]:
    parts = [p for p in path.split("/") if p]
    # /api/v9/...
    if len(parts) >= 2 and parts[0] == "api":
        parts = parts[2:]
    j = {}
    ctype = headers.get("content-type", "")
    if body and "json" in ctype:
        try:
            j = json.loads(body)
        except ValueError:
            j = {}
    elif body and "multipart" in ctype:
        j = parse_multipart_json(body, ctype)

    if parts[:1] == ["channels"] and len(parts) >= 2:
        cid = parts[1]
        rest = parts[2:]
        if rest == ["messages"] and method == "GET":
            limit = int(query.get("limit", ["50"])[0])
            msgs = MESSAGES.get(cid, [])
            if "before" in query:
                before = int(query["before"][0])
                msgs = [m for m in msgs if int(m["id"]) < before]
            if "after" in query:
                after = int(query["after"][0])
                msgs = [m for m in msgs if int(m["id"]) > after][:limit]
                return 200, list(reversed(msgs)), "json"
            if "around" in query:
                around = int(query["around"][0])
                idx = next((i for i, m in enumerate(msgs) if int(m["id"]) >= around), len(msgs))
                lo = max(0, idx - limit // 2)
                return 200, list(reversed(msgs[lo:lo + limit])), "json"
            return 200, list(reversed(msgs[-limit:])), "json"
        if rest == ["messages"] and method == "POST":
            m = message(cid, "self", j.get("content", ""), int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000))
            m["nonce"] = j.get("nonce")
            m["flags"] = int(j.get("flags", 0) or 0)
            stickers = [s for s in GUILD_STICKERS.get(GUILD_OF.get(cid, ""), []) if s["id"] in (j.get("sticker_ids") or [])]
            stickers += [PACK_STICKERS[i] for i in (j.get("sticker_ids") or []) if i in PACK_STICKERS]
            if stickers:
                m["sticker_items"] = [{"id": s["id"], "name": s["name"], "format_type": s["format_type"]}
                                      for s in stickers]
            ref = j.get("message_reference")
            if ref:
                orig = find_message(cid, ref.get("message_id", ""))
                m["type"] = 19
                m["message_reference"] = ref
                m["referenced_message"] = orig
                replied = (j.get("allowed_mentions") or {}).get("replied_user", True)
                if orig and replied:
                    m["mentions"] = [orig["author"]]
            MESSAGES.setdefault(cid, []).append(m)
            log(f"rest: message sent in {cid}: {m['content']!r} flags={m['flags']} "
                f"allowed_mentions={j.get('allowed_mentions')}")
            await broadcast("MESSAGE_CREATE", m)
            return 200, m, "json"
        if len(rest) == 3 and rest[0] == "messages" and rest[2] == "threads" and method == "POST":
            # A thread started from a message: it takes the message's id
            m = find_message(cid, rest[1])
            if not m:
                return 404, {"message": "Unknown Message", "code": 10008}, "json"
            if m.get("flags", 0) & 32:
                return 400, {"message": "A thread has already been created for this message", "code": 160004}, "json"
            gid = GUILD_OF.get(cid)
            name = (j.get("name") or "").strip()[:100]
            if not name:
                return 400, {"message": "Invalid Form Body", "code": 50035}, "json"
            thread = {"id": m["id"], "type": 11, "guild_id": gid, "parent_id": cid, "name": name,
                      "owner_id": SELF_ID, "last_message_id": None, "message_count": 0, "member_count": 1,
                      "rate_limit_per_user": 0, "flags": 0, "total_message_sent": 0,
                      "thread_metadata": {"archived": False, "locked": False,
                                          "auto_archive_duration": j.get("auto_archive_duration", 1440),
                                          "archive_timestamp": iso(int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000))},
                      "member": {"id": m["id"], "user_id": SELF_ID, "flags": 1,
                                 "join_timestamp": iso(int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000))}}
            MESSAGES.setdefault(thread["id"], [])
            GUILD_OF[thread["id"]] = gid
            await broadcast("THREAD_CREATE", dict(thread, newly_created=True))
            m["flags"] = m.get("flags", 0) | 32  # HAS_THREAD
            m["thread"] = thread
            await broadcast("MESSAGE_UPDATE", m)
            log(f"rest: thread {name!r} created from message {m['id']} in {cid}")
            return 201, thread, "json"
        if len(rest) == 2 and rest[0] == "messages":
            mid = rest[1]
            m = find_message(cid, mid)
            if not m:
                return 404, {"message": "Unknown Message", "code": 10008}, "json"
            if method == "PATCH":
                if "content" in j:
                    m["content"] = j["content"]
                m["edited_timestamp"] = iso(int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000))
                await broadcast("MESSAGE_UPDATE", m)
                return 200, m, "json"
            if method == "DELETE":
                MESSAGES[cid].remove(m)
                await broadcast("MESSAGE_DELETE", {"id": mid, "channel_id": cid, "guild_id": m.get("guild_id")})
                return 204, None, "json"
            if method == "GET":
                return 200, m, "json"
        if len(rest) >= 4 and rest[0] == "messages" and rest[2] == "reactions":
            mid = rest[1]
            emoji = urllib.request.unquote(rest[3])
            m = find_message(cid, mid)
            if not m:
                return 404, {"message": "Unknown Message", "code": 10008}, "json"
            # Who reacted (WhoReacted): seeded from the count, then kept up
            name = emoji.split(":")[0]
            r0 = next((r for r in m["reactions"] if r["emoji"]["name"] == name), None)
            who = REACTORS.setdefault((mid, name), [
                *[USERS[k] for k in ("ben", "cleo", "dan", "fay", "ava")][:max(0, (r0 or {}).get("count", 0)
                                                                               - (1 if (r0 or {}).get("me") else 0))],
                *([USERS["self"]] if (r0 or {}).get("me") else [])])
            if method == "GET" and len(rest) == 4:
                log(f"rest: reaction users {name} on {mid} -> {[u['username'] for u in who]}")
                return 200, who, "json"
            if method == "PUT" and USERS["self"] not in who:
                who.append(USERS["self"])
            if method == "DELETE" and USERS["self"] in who:
                who.remove(USERS["self"])
            ev = {"user_id": SELF_ID, "channel_id": cid, "message_id": mid, "guild_id": m.get("guild_id"),
                  "emoji": {"id": None, "name": emoji}, "burst": False, "type": 0}
            r = next((r for r in m["reactions"] if r["emoji"]["name"] == emoji), None)
            if method == "PUT":
                if r is None:
                    r = {"emoji": {"id": None, "name": emoji}, "count": 0, "me": False,
                         "count_details": {"burst": 0, "normal": 0}, "burst_colors": [], "me_burst": False,
                         "burst_count": 0}
                    m["reactions"].append(r)
                if not r["me"]:
                    r["me"] = True
                    r["count"] += 1
                await broadcast("MESSAGE_REACTION_ADD", ev)
            elif method == "DELETE" and r:
                if r["me"]:
                    r["me"] = False
                    r["count"] -= 1
                    if r["count"] <= 0:
                        m["reactions"].remove(r)
                await broadcast("MESSAGE_REACTION_REMOVE", ev)
            return 204, None, "json"
        if rest == ["typing"]:
            log(f"rest: typing in {cid}")
            return 204, None, "json"
        if len(rest) == 3 and rest[0] == "messages" and rest[2] == "ack":
            return 200, {"token": None}, "json"
        if rest == ["pins"] or rest[:2] == ["messages", "pins"]:
            pinned = [m for m in MESSAGES.get(cid, []) if m["pinned"]]
            if rest == ["pins"]:
                return 200, list(reversed(pinned)), "json"
            return 200, {"items": [{"pinned_at": m["timestamp"], "message": m} for m in reversed(pinned)],
                         "has_more": False}, "json"
        if len(rest) == 2 and rest[0] == "pins" or len(rest) == 3 and rest[:2] == ["messages", "pins"]:
            mid = rest[-1]
            m = find_message(cid, mid)
            if m:
                m["pinned"] = method == "PUT"
                await broadcast("MESSAGE_UPDATE", m)
                await broadcast("CHANNEL_PINS_UPDATE", {"channel_id": cid, "guild_id": m.get("guild_id"),
                                                        "last_pin_timestamp": m["timestamp"]})
            return 204, None, "json"
        if rest[:2] == ["messages", "search"]:
            return search(query, channel_id=cid)
        if not rest and method == "GET":
            for chans in GUILD_CHANNELS.values():
                for c in chans:
                    if c["id"] == cid:
                        return 200, c, "json"
    if parts == ["guilds"] and method == "POST":
        gid = make_guild(j.get("name") or "New Server", SELF_ID)
        log(f"rest: created guild {gid} {GUILDS[gid]['name']!r}")
        await broadcast("GUILD_CREATE", gateway_guild(gid))
        return 201, dict(GUILDS[gid], roles=ROLES[gid]), "json"
    if parts[:1] == ["invites"] and len(parts) == 2:
        code = urllib.request.unquote(parts[1])
        if code not in INVITES:
            return 404, {"message": "Unknown Invite", "code": 10006}, "json"
        if method == "GET":
            return 200, {"code": code, "guild": {"id": "0", "name": INVITES[code]}, "approximate_member_count": 3},  "json"
        gid = make_guild(INVITES[code], U["ava"])
        for key in ("ava", "ben"):
            MEMBERS[gid][key] = []
        log(f"rest: joined guild {gid} via invite {code}")
        await broadcast("GUILD_CREATE", gateway_guild(gid))
        return 200, {"code": code, "guild": {"id": gid, "name": INVITES[code]}, "new_member": True}, "json"
    if parts[:1] == ["guilds"] and len(parts) >= 3 and parts[2] == "messages" and parts[3:4] == ["search"]:
        return search(query, guild_id=parts[1])
    if parts[:2] == ["users", "@me"] and parts[2:3] == ["channels"] and method == "POST":
        recips = j.get("recipients") or [j.get("recipient_id")]
        existing = next((c for c in PRIVATE_CHANNELS if c["type"] == 1 and c["recipient_ids"] == recips), None)
        if existing:
            return 200, dict(existing, recipients=[u for u in USERS.values() if u["id"] in recips]), "json"
        cid = snowflake()
        ch = {"id": cid, "type": 1, "recipient_ids": recips, "last_message_id": None, "flags": 0}
        PRIVATE_CHANNELS.append(ch)
        MESSAGES[cid] = []
        full = dict(ch, recipients=[u for u in USERS.values() if u["id"] in recips])
        await broadcast("CHANNEL_CREATE", full)
        return 200, full, "json"
    if parts[:3] == ["users", "@me", "guilds"] and len(parts) == 5 and parts[4] == "settings" and method == "PATCH":
        gid = None if parts[3] in ("@me", "%40me") else parts[3]
        settings = GUILD_SETTINGS.setdefault(gid, {"guild_id": gid, "muted": False, "message_notifications": 1,
                                                   "channel_overrides": [], "suppress_everyone": False,
                                                   "suppress_roles": False, "mobile_push": True, "flags": 0,
                                                   "hide_muted_channels": False, "mute_config": None, "version": 1})
        for key, value in j.items():
            if key == "channel_overrides":
                for cid, override in value.items():
                    existing = next((o for o in settings["channel_overrides"] if o["channel_id"] == cid), None)
                    if existing is None:
                        existing = {"channel_id": cid, "muted": False, "message_notifications": 3, "mute_config": None,
                                    "collapsed": False}
                        settings["channel_overrides"].append(existing)
                    existing.update(override)
            else:
                settings[key] = value
        settings["version"] += 1
        log(f"rest: guild settings {gid} <- {j}")
        await broadcast("USER_GUILD_SETTINGS_UPDATE", settings)
        return 200, settings, "json"
    if parts[:2] == ["users", "@me"] and parts[2:3] == ["relationships"]:
        return await relationships(method, parts[3] if len(parts) > 3 else None, j)
    if parts[:1] == ["users"] and len(parts) == 2 and method == "GET":
        found = next((u for u in list(USERS.values()) + [STRANGER] if u["id"] == parts[1]), None)
        return (200, found, "json") if found else (404, {"message": "Unknown User", "code": 10013}, "json")
    if parts[:1] == ["users"] and len(parts) >= 3 and parts[2] == "profile":
        return 200, profile(parts[1]), "json"
    if parts[:2] == ["users", "@me"] and method == "GET" and len(parts) == 2:
        return 200, USERS["self"], "json"
    if parts[:3] == ["users", "@me", "mentions"]:
        limit = int(query.get("limit", ["25"])[0])
        hits = [m for msgs in MESSAGES.values() for m in msgs
                if any(u["id"] == SELF_ID for u in m.get("mentions", [])) and m["author"]["id"] != SELF_ID]
        hits.sort(key=lambda m: -int(m["id"]))
        return 200, hits[:limit], "json"
    if parts[:3] == ["users", "@me", "settings-proto"]:
        if method == "PATCH":
            log(f"rest: settings-proto/{parts[3] if len(parts) > 3 else '?'} patched ({len(body)} bytes)")
        return 200, {"settings": ""}, "json"
    if len(parts) == 3 and parts[2] == "application-command-index" and parts[0] in ("guilds", "channels"):
        empty = {"applications": [], "application_commands": [], "version": "0"}
        return 200, (COMMAND_INDEX if parts[0] == "guilds" and parts[1] == G1 else empty), "json"
    if parts == ["interactions"] and method == "POST":
        data = j.get("data") or {}
        cid = j.get("channel_id", "")
        log(f"rest: interaction /{data.get('name')} options={json.dumps(data.get('options', []))} "
            f"session={j.get('session_id')!r} app={j.get('application_id')}")

        def flat(opts):
            out = {}
            for o in opts or []:
                if o.get("type") in (1, 2):
                    out["_sub"] = (out.get("_sub", "") + " " + o["name"]).strip()
                    out.update(flat(o.get("options")))
                else:
                    out[o["name"]] = o.get("value")
            return out
        args = flat(data.get("options"))
        name = data.get("name")
        if j.get("type") == 4:
            # Autocomplete: the focused option's suggestions, over the gateway
            focused = next((o for o in data.get("options", []) if o.get("focused")), {})
            typed = str(focused.get("value", "")).lower()
            choices = [{"name": c, "value": v} for c, v in CITIES if typed in c.lower()][:25]
            await broadcast("APPLICATION_COMMAND_AUTOCOMPLETE_RESPONSE", {"nonce": j.get("nonce"), "choices": choices})
            log(f"rest: autocomplete {focused.get('name')}={typed!r} -> {[c['name'] for c in choices]}")
            return 204, None, "json"
        if name == "weather":
            city = next((c for c, v in CITIES if v == args.get("city")), None)
            units = {"c": "°C", "f": "°F"}.get(args.get("units", "c"), "°C")
            reply = (f"☀️ {city}: 21{units}" if city
                     else f"I don't know a city called {args.get('city')!r} (pick one from the list)")
        elif name == "ping":
            reply = "Pong! 🏓"
        elif name == "roll":
            reply = f"🎲 You rolled {random.randint(1, int(args.get('sides', 6) or 6))}"
        elif name == "echo":
            reply = str(args.get("text", ""))
        else:
            reply = f"/{name} {args}"
        m = message(cid, "bot", reply, int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000))
        m["type"] = 20  # CHAT_INPUT_COMMAND
        m["interaction_metadata"] = {"id": "1", "type": 2, "name": name, "user": USERS["self"]}
        MESSAGES.setdefault(cid, []).append(m)
        await broadcast("MESSAGE_CREATE", m)
        return 204, None, "json"
    if parts[:2] == ["gifs", "trending-gifs"] or parts[:2] == ["gifs", "search"]:
        q = query.get("q", [""])[0]
        log(f"rest: gifs {parts[1]} q={q!r}")
        words = ["wave", "party", "cat", "thumbs up", "dance", "coffee", "ship it", "facepalm"]
        hits = [w for w in words if q.lower() in w] if q else words
        return 200, [{"id": f"mock-{i}", "title": w, "url": f"https://tenor.com/view/mock-{w.replace(' ', '-')}-gif-{i}",
                      "src": f"https://media.discordapp.net/mock-gifs/{i}.mp4",
                      "gif_src": f"https://media.discordapp.net/mock-gifs/{i}.gif",
                      "preview": f"https://media.discordapp.net/mock-gifs/{i}.png", "width": 220, "height": 160}
                     for i, w in enumerate(hits)], "json"
    if parts == ["sticker-packs"] and method == "GET":
        return 200, {"sticker_packs": STICKER_PACKS}, "json"
    if len(parts) == 3 and parts[0] == "guilds" and parts[2] == "stickers" and method == "GET":
        return 200, GUILD_STICKERS.get(parts[1], []), "json"
    if parts[:1] == ["read-states"]:
        if parts[1:2] == ["ack-bulk"]:
            try:
                states = json.loads(body or b"{}").get("read_states", [])
            except ValueError:
                states = []
            log(f"rest: ack-bulk for {len(states)} channel(s): {sorted(s.get('channel_id') for s in states)}")
        return 200, {}, "json"
    if parts[:1] == ["science"] or parts[:1] == ["metrics"]:
        return 204, None, "json"
    log(f"rest: unhandled {method} /{'/'.join(parts)}")
    if method == "GET":
        return 200, [], "json"
    return 204, None, "json"


async def relationships(method: str, user_id: str | None, j: dict) -> tuple[int, Any, str]:
    def find(uid):
        return next((r for r in RELATIONSHIPS if r["id"] == uid), None)

    if method == "POST" and user_id is None:
        key = next((k for k, u in USERS.items() if u["username"] == j.get("username")), None)
        if key is None:
            return 400, {"message": "Unknown User", "code": 80004}, "json"
        rel = {"id": USERS[key]["id"], "type": 4, "nickname": None, "user_id": USERS[key]["id"], "user": USERS[key]}
        RELATIONSHIPS.append(rel)
        log(f"rest: friend request to {key}")
        await broadcast("RELATIONSHIP_ADD", rel)
        return 204, None, "json"
    rel = find(user_id)
    if method == "PUT":
        new_type = int(j.get("type", 1))
        user_obj = next((u for u in USERS.values() if u["id"] == user_id), None)
        if rel:
            RELATIONSHIPS.remove(rel)
        rel = {"id": user_id, "type": new_type, "nickname": None, "user_id": user_id, "user": user_obj}
        RELATIONSHIPS.append(rel)
        log(f"rest: relationship {user_id} -> type {new_type}")
        await broadcast("RELATIONSHIP_ADD", rel)
        return 204, None, "json"
    if method == "DELETE" and rel:
        RELATIONSHIPS.remove(rel)
        log(f"rest: relationship {user_id} removed")
        await broadcast("RELATIONSHIP_REMOVE", {"id": user_id, "type": rel["type"]})
        return 204, None, "json"
    return 404, {"message": "Unknown Relationship"}, "json"


def search(query: dict, guild_id: str | None = None, channel_id: str | None = None) -> tuple[int, Any, str]:
    needle = (query.get("content", [""])[0]).lower()
    author = query.get("author_id", [None])[0]
    hits = []
    for cid, msgs in MESSAGES.items():
        if channel_id and cid != channel_id:
            continue
        if guild_id and not any(c["id"] == cid for c in GUILD_CHANNELS.get(guild_id, [])):
            continue
        for m in msgs:
            if needle and needle not in m["content"].lower():
                continue
            if author and m["author"]["id"] != author:
                continue
            hits.append(m)
    hits.sort(key=lambda m: -int(m["id"]))
    offset = int(query.get("offset", ["0"])[0])
    page = hits[offset:offset + 25]
    return 200, {"total_results": len(hits), "messages": [[m] for m in page], "analytics_id": "mock"}, "json"


def parse_multipart_json(body: bytes, ctype: str) -> dict:
    boundary = ctype.split("boundary=")[-1].strip('"').encode()
    for part in body.split(b"--" + boundary):
        if b'name="payload_json"' in part:
            content = part.split(b"\r\n\r\n", 1)[-1].rstrip(b"\r\n-")
            try:
                return json.loads(content)
            except ValueError:
                return {}
    return {}


def cdn(path: str) -> tuple[int, bytes, str]:
    # /cdn/<host>/<path>
    key = path
    if "/attachments/" in path:
        name = path.rsplit("/", 1)[-1]
        if name in TEXT_FILES:
            return 200, TEXT_FILES[name], "text/plain; charset=utf-8"
        if name.startswith("diagram"):
            return 200, png(320, 320, (88, 101, 242), gradient=True), "image/png"
        return 200, png(480, 270, (235, 69, 158), gradient=True), "image/png"
    if "/stickers/" in path and path.endswith(".json"):
        return 200, LOTTIE_STICKER, "application/json"
    if "/embed/avatars/" in path:
        return 200, png(64, 64, (88, 101, 242)), "image/png"
    return 200, png(64, 64, colour_for(key.split("/")[4] if key.count("/") > 4 else key)), "image/png"


# Translate plugin: fake Google / DeepL / Kagi answers (no real service)
SPANISH = {"¿Alguien quiere probar la nueva versión esta noche?":
           "Does anyone want to try the new version tonight?"}


def fake_translation(text: str, to: str) -> tuple[str, str]:
    if text in SPANISH and to.lower().startswith("en"):
        return SPANISH[text], "es"
    return f"[{to}] {text}", "en"


def mock_translate(path: str, query: dict, body: bytes, headers: dict) -> tuple[int, bytes, str]:
    if path.startswith("/cdn/translate-pa.googleapis.com/"):
        text, source = fake_translation(query.get("query.text", [""])[0], query.get("query.targetLanguage", ["en"])[0])
        return 200, json.dumps({"translation": text, "sourceLanguage": source}).encode(), "application/json"
    req = json.loads(body or b"{}")
    if "deepl.com/" in path:
        if headers.get("authorization", "") != "DeepL-Auth-Key mock-key":
            return 403, b"Forbidden", "text/plain"
        text, source = fake_translation(req["text"][0], req.get("target_lang", "en"))
        return 200, json.dumps({"translations": [{"detected_source_language": source.upper(),
                                                  "text": text}]}).encode(), "application/json"
    if "kagi_session=mock-session" not in headers.get("cookie", ""):
        return 401, b"{}", "application/json"
    text, source = fake_translation(req["text"], req.get("to", "en"))
    return 200, json.dumps({"translation": text, "detected_language": {"label": "Spanish" if source == "es" else
                                                                       "English"}}).encode(), "application/json"


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[mock {dt.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while True:
            request_line = await reader.readline()
            if not request_line:
                break
            method, target, _ = request_line.decode("latin-1").split(" ", 2)
            headers: dict[str, str] = {}
            while True:
                line = await reader.readline()
                if line in (b"\r\n", b"\n", b""):
                    break
                k, v = line.decode("latin-1").split(":", 1)
                headers[k.strip().lower()] = v.strip()

            if headers.get("upgrade", "").lower() == "websocket":
                accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] + GUID).encode()).digest())
                writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                             b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                await writer.drain()
                if target.startswith("/voice"):
                    log("voice: gateway connected")
                    await mock_voice.run_voice_gateway(GatewayConn(reader, writer))
                    return
                log("gateway: client connected")
                await run_gateway(GatewayConn(reader, writer))
                return

            if headers.get("expect", "").lower() == "100-continue":
                writer.write(b"HTTP/1.1 100 Continue\r\n\r\n")
                await writer.drain()
            body = b""
            if "content-length" in headers:
                body = await reader.readexactly(int(headers["content-length"]))
            elif headers.get("transfer-encoding", "").lower() == "chunked":
                while True:
                    size = int((await reader.readline()).strip().split(b";")[0], 16)
                    if size == 0:
                        await reader.readline()
                        break
                    body += await reader.readexactly(size)
                    await reader.readline()

            url = urlsplit(target)
            query = parse_qs(url.query)
            if url.path.startswith(("/cdn/translate-pa.googleapis.com/", "/cdn/api-free.deepl.com/",
                                    "/cdn/api.deepl.com/", "/cdn/translate.kagi.com/")):
                status, data, ctype = mock_translate(url.path, query, body, headers)
            elif url.path.startswith("/cdn/"):
                status, data, ctype = cdn(url.path)
            elif url.path == "/mock/dispatch" and method == "POST":
                ev = json.loads(body)
                await broadcast(ev["t"], ev["d"])
                status, data, ctype = 204, b"", "application/json"
            elif url.path == "/mock/state":
                status, data, ctype = 200, json.dumps({"channels": CH, "users": U, "messages": {
                    k: [m["id"] for m in v] for k, v in MESSAGES.items()}}).encode(), "application/json"
            elif url.path.startswith("/mock/"):
                status, data, ctype = await mock_control(url.path, body)
            else:
                status, obj, _ = await handle_rest(method, url.path, query, body, headers)
                data = b"" if obj is None else json.dumps(obj).encode()
                ctype = "application/json"
            reason = {200: "OK", 201: "Created", 204: "No Content", 404: "Not Found", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden", 456: "Quota Exceeded"}.get(status, "OK")
            head = (f"HTTP/1.1 {status} {reason}\r\nContent-Type: {ctype}\r\n"
                    f"Content-Length: {len(data)}\r\nConnection: keep-alive\r\n\r\n")
            writer.write(head.encode() + data)
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, ValueError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass


async def mock_control(path: str, body: bytes) -> tuple[int, bytes, str]:
    """Simulate other users: /mock/say, /mock/edit, /mock/delete, /mock/typing, /mock/presence."""
    args = json.loads(body or b"{}")
    cid = resolve_channel(args.get("channel", "general"))
    who = args.get("user", "ava")
    now = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
    msgs = MESSAGES.setdefault(cid, [])

    def target() -> dict | None:
        mid = args.get("message", "last")
        if mid == "last":
            others = [m for m in msgs if m["author"]["id"] != SELF_ID] or msgs
            return others[-1] if others else None
        return find_message(cid, mid)

    action = path.rsplit("/", 1)[-1]
    if action == "say":
        m = message(cid, who, args.get("content", ""), now)
        msgs.append(m)
        await broadcast("MESSAGE_CREATE", m)
        return 200, json.dumps(m).encode(), "application/json"
    if action == "edit":
        m = target()
        if not m:
            return 404, b"{}", "application/json"
        m["content"] = args.get("content", "")
        m["edited_timestamp"] = iso(now)
        await broadcast("MESSAGE_UPDATE", m)
        return 200, json.dumps(m).encode(), "application/json"
    if action == "delete":
        m = target()
        if not m:
            return 404, b"{}", "application/json"
        msgs.remove(m)
        await broadcast("MESSAGE_DELETE", {"id": m["id"], "channel_id": cid, "guild_id": m.get("guild_id")})
        return 204, b"", "application/json"
    if action == "typing":
        ev = {"channel_id": cid, "user_id": USERS[who]["id"], "timestamp": now // 1000}
        gid = next((g for g, chans in GUILD_CHANNELS.items() if any(c["id"] == cid for c in chans)), None)
        if gid:
            ev["guild_id"] = gid
            ev["member"] = guild_member(gid, who)
        await broadcast("TYPING_START", ev)
        return 204, b"", "application/json"
    if action == "presence":
        STATUS[who] = args.get("status", "online")
        for gid in GUILDS:
            if who in MEMBERS[gid]:
                await broadcast("PRESENCE_UPDATE", dict(presence(who, gid), user={"id": USERS[who]["id"]}))
        return 204, b"", "application/json"
    return 404, b"{}", "application/json"


async def serve(port: int) -> None:
    server = await asyncio.start_server(handle_client, "127.0.0.1", port)
    mock_voice.LOG = log
    mock_voice.FAKE_CAMERAS[G1] = U["ava"]  # Ava's camera, in the first server's voice channel
    await mock_voice.start_udp()
    VOICE["endpoint"] = f"ws://127.0.0.1:{port}/voice"
    log(f"listening on http://127.0.0.1:{port}  (set QORD_MOCK_DISCORD=http://127.0.0.1:{port})")
    async with server:
        await server.serve_forever()


def control(port: int, action: str, payload: dict) -> int:
    req = urllib.request.Request(f"http://127.0.0.1:{port}/mock/{action}", data=json.dumps(payload).encode(),
                                 method="POST", headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as resp:
        out = resp.read()
        if out:
            print(out.decode())
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=int(os.environ.get("QORD_MOCK_PORT", "8765")))
    sub = ap.add_subparsers(dest="cmd")
    for name in ("say", "edit", "delete", "typing", "presence"):
        p = sub.add_parser(name)
        p.add_argument("--channel", default="general")
        p.add_argument("--user", default="ava")
        if name in ("edit", "delete"):
            p.add_argument("--message", default="last")
        if name in ("say", "edit"):
            p.add_argument("content")
        if name == "presence":
            p.add_argument("status", choices=["online", "idle", "dnd", "offline"])
    args = ap.parse_args(argv)
    if args.cmd:
        payload = {k: v for k, v in vars(args).items() if k not in ("cmd", "port")}
        return control(args.port, args.cmd, payload)
    try:
        asyncio.run(serve(args.port))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
