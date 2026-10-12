"""Voice and Go Live for the mock Discord server (tools/mock_discord.py).

A voice gateway (the JSON opcodes Discord's voice servers use, no DAVE) and
one UDP port for media: IP discovery, then RTP relayed between the sessions
of the same RTC server, as Discord's SFU forwards it. Everything stays on
127.0.0.1.

For checking Qord's Go Live without Discord:
- video Qord streams is also decrypted here (aead_aes256_gcm_rtpsize, with
  Windows' BCrypt), taken apart (RFC 6184) and written to
  %TEMP%/qord-mock-stream.h264, so ffmpeg can say whether it is valid H.264;
- Ava streams a test picture with a 660 Hz tone (made once with ffmpeg) that
  Qord can watch, and Qord's stream sound is counted when it arrives;
  and has her camera on (colour bars) in the first server's voice channel.

AES-GCM goes through Windows' BCrypt, or the `cryptography` package elsewhere.
"""
from __future__ import annotations

import asyncio
import ctypes
import json
import os
import random
import struct
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any

UDP_PORT = 8767
MODE = "aead_aes256_gcm_rtpsize"
H264_PT = 101
OPUS_PT = 120
LOG = print
OUT_PATH = os.path.join(tempfile.gettempdir(), "qord-mock-stream.h264")
FFMPEG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "deps", "ffmpeg", "bin", "ffmpeg.exe")
if not os.path.exists(FFMPEG):
    FFMPEG = shutil.which("ffmpeg") or FFMPEG  # Linux, macOS: the one on PATH
FFPROBE = os.path.join(os.path.dirname(FFMPEG), "ffprobe.exe" if FFMPEG.endswith(".exe") else "ffprobe")


# --- AES-256-GCM through Windows' BCrypt ------------------------------------

class _AuthInfo(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_ulong), ("dwInfoVersion", ctypes.c_ulong),
                ("pbNonce", ctypes.c_void_p), ("cbNonce", ctypes.c_ulong),
                ("pbAuthData", ctypes.c_void_p), ("cbAuthData", ctypes.c_ulong),
                ("pbTag", ctypes.c_void_p), ("cbTag", ctypes.c_ulong),
                ("pbMacContext", ctypes.c_void_p), ("cbMacContext", ctypes.c_ulong),
                ("cbAAD", ctypes.c_ulong), ("cbData", ctypes.c_ulonglong), ("dwFlags", ctypes.c_ulong)]


class _PyAesGcm:
    """The same, through the `cryptography` package, where there is no BCrypt."""
    def __init__(self, key: bytes):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        self.aead = AESGCM(key)

    def encrypt(self, nonce: bytes, plain: bytes, aad: bytes) -> bytes:
        return self.aead.encrypt(nonce, plain, aad)

    def decrypt(self, nonce: bytes, sealed: bytes, aad: bytes) -> bytes | None:
        try:
            return self.aead.decrypt(nonce, sealed, aad)
        except Exception:
            return None


class AesGcm:
    def __new__(cls, key: bytes):
        if os.name != "nt":
            return _PyAesGcm(key)
        return super().__new__(cls)

    def __init__(self, key: bytes):
        self.bcrypt = ctypes.WinDLL("bcrypt")
        self.alg = ctypes.c_void_p()
        self._check(self.bcrypt.BCryptOpenAlgorithmProvider(ctypes.byref(self.alg), ctypes.c_wchar_p("AES"), None, 0))
        mode = ctypes.create_unicode_buffer("ChainingModeGCM")
        self._check(self.bcrypt.BCryptSetProperty(self.alg, ctypes.c_wchar_p("ChainingMode"), mode,
                                                  ctypes.sizeof(mode), 0))
        self.key = ctypes.c_void_p()
        kb = ctypes.create_string_buffer(key, len(key))
        self._check(self.bcrypt.BCryptGenerateSymmetricKey(self.alg, ctypes.byref(self.key), None, 0, kb, len(key), 0))

    @staticmethod
    def _check(status: int) -> None:
        if status != 0:
            raise OSError(f"BCrypt failed: {status & 0xFFFFFFFF:#x}")

    def _info(self, nonce, aad, tag) -> _AuthInfo:
        info = _AuthInfo()
        info.cbSize = ctypes.sizeof(_AuthInfo)
        info.dwInfoVersion = 1
        info.pbNonce = ctypes.cast(nonce, ctypes.c_void_p)
        info.cbNonce = 12
        info.pbAuthData = ctypes.cast(aad, ctypes.c_void_p)
        info.cbAuthData = len(aad.raw) if aad is not None else 0
        info.pbTag = ctypes.cast(tag, ctypes.c_void_p)
        info.cbTag = 16
        return info

    def encrypt(self, nonce: bytes, plain: bytes, aad: bytes) -> bytes:
        n = ctypes.create_string_buffer(nonce, 12)
        a = ctypes.create_string_buffer(aad, len(aad))
        tag = ctypes.create_string_buffer(16)
        info = self._info(n, a, tag)
        info.cbAuthData = len(aad)
        out = ctypes.create_string_buffer(max(1, len(plain)))
        got = ctypes.c_ulong()
        self._check(self.bcrypt.BCryptEncrypt(self.key, ctypes.create_string_buffer(plain, len(plain)), len(plain),
                                              ctypes.byref(info), None, 0, out, len(plain), ctypes.byref(got), 0))
        return out.raw[:got.value] + tag.raw

    def decrypt(self, nonce: bytes, sealed: bytes, aad: bytes) -> bytes | None:
        if len(sealed) < 16:
            return None
        body, tagv = sealed[:-16], sealed[-16:]
        n = ctypes.create_string_buffer(nonce, 12)
        a = ctypes.create_string_buffer(aad, len(aad))
        tag = ctypes.create_string_buffer(tagv, 16)
        info = self._info(n, a, tag)
        info.cbAuthData = len(aad)
        out = ctypes.create_string_buffer(max(1, len(body)))
        got = ctypes.c_ulong()
        status = self.bcrypt.BCryptDecrypt(self.key, ctypes.create_string_buffer(body, len(body)), len(body),
                                           ctypes.byref(info), None, 0, out, len(body), ctypes.byref(got), 0)
        return out.raw[:got.value] if status == 0 else None


def rtp_header_size(packet: bytes) -> int:
    """The clear part for rtpsize: fixed header, CSRCs and the extension's 4-byte header."""
    cc = packet[0] & 0x0F
    return 12 + cc * 4 + (4 if packet[0] & 0x10 else 0)


def seal(crypto: AesGcm, counter: int, clear: bytes, plain: bytes) -> bytes:
    nonce4 = struct.pack(">I", counter)
    return clear + crypto.encrypt(nonce4 + b"\0" * 8, plain, clear) + nonce4


def open_packet(crypto: AesGcm, packet: bytes) -> bytes | None:
    """The payload of an rtpsize packet, without the extension body."""
    hs = rtp_header_size(packet)
    if len(packet) < hs + 20:
        return None
    nonce4 = packet[-4:]
    plain = crypto.decrypt(nonce4 + b"\0" * 8, packet[hs:-4], packet[:hs])
    if plain is None:
        return None
    if packet[0] & 0x10:
        ext_words = struct.unpack(">H", packet[hs - 2:hs])[0]
        plain = plain[ext_words * 4:]
    return plain


# --- H.264 over RTP (an implementation separate from Qord's) ----------------

def split_annexb(data: bytes) -> list[bytes]:
    units, i, start = [], 0, None
    while i + 2 < len(data):
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            if start is not None:
                end = i - 1 if i > 0 and data[i - 1] == 0 else i
                if end > start:
                    units.append(data[start:end])
            i += 3
            start = i
        else:
            i += 1
    if start is not None and start < len(data):
        units.append(data[start:])
    return units


def access_units(stream: bytes) -> list[bytes]:
    """Groups NAL units into frames: parameter sets go with the next slice, and
    a frame's slices stay together (x264's zerolatency uses several). A slice
    starting at macroblock 0 (first_mb_in_slice, ue(v), is a lone 1 bit)
    begins a new frame."""
    frames, pending, has_slice = [], b"", False
    for nal in split_annexb(stream):
        kind = nal[0] & 0x1F
        is_slice = kind in (1, 5)
        if has_slice and (not is_slice or nal[1] & 0x80):
            frames.append(pending)
            pending, has_slice = b"", False
        pending += b"\0\0\0\1" + nal
        has_slice = has_slice or is_slice
    if has_slice:
        frames.append(pending)
    return frames


def packetize(frame: bytes, mtu: int = 1100) -> list[bytes]:
    out = []
    for nal in split_annexb(frame):
        if len(nal) <= mtu:
            out.append(nal)
            continue
        indicator = (nal[0] & 0xE0) | 28
        body = nal[1:]
        for off in range(0, len(body), mtu - 2):
            first, last = off == 0, off + mtu - 2 >= len(body)
            out.append(bytes([indicator, (0x80 if first else 0) | (0x40 if last else 0) | (nal[0] & 0x1F)])
                       + body[off:off + mtu - 2])
    return out


class Depacketizer:
    def __init__(self):
        self.nals: list[bytes] = []
        self.fu = b""
        self.last_seq = None
        self.broken = False
        self.frames = 0
        self.keyframes = 0
        self.dropped = 0

    def push(self, seq: int, marker: bool, payload: bytes) -> bytes | None:
        if self.last_seq is not None and (self.last_seq + 1) & 0xFFFF != seq:
            self.broken = True
        self.last_seq = seq
        t = payload[0] & 0x1F
        if t == 28:
            if payload[1] & 0x80:
                self.fu = bytes([(payload[0] & 0xE0) | (payload[1] & 0x1F)])
            self.fu += payload[2:]
            if payload[1] & 0x40:
                self.nals.append(self.fu)
                self.fu = b""
        elif t == 24:
            off = 1
            while off + 2 <= len(payload):
                n = struct.unpack(">H", payload[off:off + 2])[0]
                self.nals.append(payload[off + 2:off + 2 + n])
                off += 2 + n
        else:
            self.nals.append(payload)
        if not marker:
            return None
        frame = b"".join(b"\0\0\0\1" + n for n in self.nals)
        key = any(n[0] & 0x1F in (5, 7) for n in self.nals)
        broken = self.broken
        self.nals, self.fu, self.broken = [], b"", False
        if broken:
            self.dropped += 1
            return None
        self.frames += 1
        self.keyframes += key
        return frame


# --- Sessions ----------------------------------------------------------------

class Session:
    def __init__(self, server: str, user_id: str, ws, video: bool):
        self.server = server
        self.user_id = user_id
        self.ws = ws
        self.ssrc = random.randint(1000, 0x7FFFFFF0)
        self.video_ssrc = self.ssrc + 1
        self.rtx_ssrc = self.ssrc + 2
        self.video = video
        self.addr = None
        self.video_op = None  # the last op 12 this session sent
        self.depacketizer = Depacketizer()
        self.audio_packets = 0
        self.audio_loud = 0


SESSIONS: list[Session] = []
SERVER_KEYS: dict[str, bytes] = {}
FAKE_STREAMS: dict[str, str] = {}  # rtc server id -> user id streaming a test picture
FAKE_CAMERAS: dict[str, str] = {}  # guild (voice server) id -> user id with a camera on
TRANSPORT: asyncio.DatagramTransport | None = None


_CRYPTO: dict[str, AesGcm] = {}


def crypto_for(server: str) -> AesGcm:
    if server not in _CRYPTO:
        _CRYPTO[server] = AesGcm(key_for(server))
    return _CRYPTO[server]


def key_for(server: str) -> bytes:
    # One key per RTC server, so relayed packets stay readable for everyone
    if server not in SERVER_KEYS:
        SERVER_KEYS[server] = os.urandom(32)
    return SERVER_KEYS[server]


class MediaProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        global TRANSPORT
        TRANSPORT = transport
        self.transport = transport

    def datagram_received(self, data: bytes, addr):
        if len(data) == 74 and data[:2] == b"\0\1":
            ssrc = struct.unpack(">I", data[4:8])[0]
            for s in SESSIONS:
                if s.ssrc == ssrc:
                    s.addr = addr
            reply = struct.pack(">HHI", 2, 70, ssrc) + addr[0].encode().ljust(64, b"\0") + struct.pack(">H", addr[1])
            self.transport.sendto(reply, addr)
            return
        sender = next((s for s in SESSIONS if s.addr == addr), None)
        if not sender or len(data) < 12:
            return
        # Qord's sound (a Go Live stream's, or the microphone): count what
        # decrypts and how much of it isn't silence
        if not (192 <= data[1] <= 223) and (data[1] & 0x7F) == OPUS_PT:
            payload = open_packet(crypto_for(sender.server), data)
            if payload is None:
                LOG("voice: an audio packet did not decrypt")
            else:
                sender.audio_packets += 1
                sender.audio_loud += len(payload) > 3
                if sender.audio_packets % 100 == 1:
                    LOG(f"voice: audio from {sender.user_id} on {sender.server}: {sender.audio_packets} packets "
                        f"({sender.audio_loud} with sound)")
        # Qord's own video: check it independently
        if not (192 <= data[1] <= 223) and (data[1] & 0x7F) == H264_PT and sender.video:
            payload = open_packet(crypto_for(sender.server), data)
            if payload is not None:
                seq = struct.unpack(">H", data[2:4])[0]
                frame = sender.depacketizer.push(seq, bool(data[1] & 0x80), payload)
                if frame is not None:
                    with open(OUT_PATH, "ab") as f:
                        f.write(frame)
                    d = sender.depacketizer
                    if d.frames % 30 == 1:
                        LOG(f"voice: stream from {sender.user_id}: {d.frames} frames ({d.keyframes} keyframes, "
                            f"{d.dropped} dropped), saved to {OUT_PATH}")
            else:
                LOG("voice: a video packet did not decrypt")
        for s in SESSIONS:
            if s is not sender and s.server == sender.server and s.addr:
                self.transport.sendto(data, s.addr)


async def start_udp() -> None:
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(MediaProtocol, local_addr=("127.0.0.1", UDP_PORT))
    LOG(f"voice: media on udp://127.0.0.1:{UDP_PORT}")


async def ws_send(ws, op: int, d: Any) -> None:
    await ws.send_frame(0x1, json.dumps({"op": op, "d": d}).encode())


async def run_voice_gateway(ws) -> None:
    session: Session | None = None
    await ws_send(ws, 8, {"heartbeat_interval": 13750, "v": 8})
    try:
        while True:
            frame = await ws.recv()
            if frame is None:
                break
            if frame[0] == 0x2:
                continue  # DAVE binary opcodes: the mock has no DAVE
            msg = json.loads(frame[1])
            op, d = msg.get("op"), msg.get("d") or {}
            if op == 0:
                video = bool(d.get("video"))
                session = Session(str(d.get("server_id")), str(d.get("user_id")), ws, video)
                SESSIONS.append(session)
                streams = [{"type": "video", "rid": "100", "ssrc": session.video_ssrc, "rtx_ssrc": session.rtx_ssrc,
                            "quality": 100, "active": False}] if video else []
                await ws_send(ws, 2, {"ssrc": session.ssrc, "ip": "127.0.0.1", "port": UDP_PORT,
                                      "modes": [MODE], "streams": streams, "experiments": []})
                LOG(f"voice: {session.user_id} identified on {session.server} (video: {video})")
            elif op == 3:
                await ws_send(ws, 6, {"t": d.get("t") if isinstance(d, dict) else d})
            elif op == 1 and session:
                await ws_send(ws, 4, {"mode": MODE, "secret_key": list(key_for(session.server)),
                                      "audio_codec": "opus", "video_codec": "H264", "media_session_id": "mock",
                                      "dave_protocol_version": 0})
                others = [s for s in SESSIONS if s.server == session.server and s is not session]
                if others:
                    await ws_send(ws, 11, {"user_ids": sorted({s.user_id for s in others})})
                for s in others:
                    if s.video_op:
                        await ws_send(ws, 12, s.video_op)
                fake = FAKE_STREAMS.get(session.server)
                if fake and session.video:
                    asyncio.create_task(fake_stream(session, fake))
                camera = FAKE_CAMERAS.get(session.server)
                if camera and session.video:
                    asyncio.create_task(fake_stream(session, camera, "smptehdbars"))
            elif op == 12 and session:
                session.video_op = dict(d, user_id=session.user_id)
                active = any(s.get("active") for s in d.get("streams", []))
                LOG(f"voice: {session.user_id} video {'on' if active else 'off'}: "
                    f"{json.dumps(d.get('streams', []))}")
                if active:
                    open(OUT_PATH, "wb").close()
                for s in SESSIONS:
                    if s is not session and s.server == session.server:
                        await ws_send(s.ws, 12, session.video_op)
            elif op in (5, 15):
                pass  # speaking, media sink wants
            else:
                LOG(f"voice: op {op} ignored")
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        if session in SESSIONS:
            SESSIONS.remove(session)
            LOG(f"voice: {session.user_id} left {session.server}")


# --- Ava's test stream ------------------------------------------------------

_TEST_FRAMES: dict[str, list[bytes]] = {}


def test_frames(source: str = "testsrc2") -> list[bytes]:
    """Ten seconds of an ffmpeg test picture: testsrc2 for streams, a colour
    bar pattern for cameras."""
    if source not in _TEST_FRAMES:
        path = os.path.join(tempfile.gettempdir(), f"qord-mock-{source}.h264")
        if not os.path.exists(path):
            encoders = subprocess.run([FFMPEG, "-hide_banner", "-encoders"], capture_output=True, text=True).stdout
            codec = ["-c:v", "libopenh264", "-profile:v", "constrained_baseline"] if "libopenh264" in encoders \
                else ["-c:v", "libx264", "-profile:v", "baseline", "-tune", "zerolatency"]
            subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                            f"{source}=size=640x360:rate=30", "-t", "10", *codec,
                            "-g", "30", "-b:v", "1M", "-f", "h264", path], check=True)
        with open(path, "rb") as f:
            _TEST_FRAMES[source] = access_units(f.read())
    return _TEST_FRAMES[source]


_TEST_AUDIO: list[bytes] = []


def ogg_packets(data: bytes) -> list[bytes]:
    """The packets of an Ogg stream (one logical stream, as ffmpeg writes it)."""
    packets, current, pos = [], b"", 0
    while pos + 27 <= len(data) and data[pos:pos + 4] == b"OggS":
        count = data[pos + 26]
        table = data[pos + 27:pos + 27 + count]
        pos += 27 + count
        for size in table:
            current += data[pos:pos + size]
            pos += size
            if size < 255:
                packets.append(current)
                current = b""
    return packets


def test_audio() -> list[bytes]:
    """Ten seconds of a 660 Hz tone as 20 ms Opus packets (made once with ffmpeg)."""
    if not _TEST_AUDIO:
        path = os.path.join(tempfile.gettempdir(), "qord-mock-tone.opus")
        if not os.path.exists(path):
            subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                            "sine=frequency=660:sample_rate=48000", "-ac", "2", "-t", "10", "-c:a", "libopus",
                            "-b:a", "96k", "-frame_duration", "20", "-f", "ogg", path], check=True)
        with open(path, "rb") as f:
            _TEST_AUDIO.extend(ogg_packets(f.read())[2:])  # past OpusHead and OpusTags
    return _TEST_AUDIO


async def fake_stream_audio(viewer: Session, ssrc: int) -> None:
    """The stream's sound: Opus every 20 ms, as Discord's SFU forwards it."""
    packets = test_audio()
    crypto = crypto_for(viewer.server)
    seq, counter, start, i = random.randint(0, 65535), 1 << 31, time.monotonic(), 0
    while viewer in SESSIONS:
        if not viewer.addr or TRANSPORT is None:
            await asyncio.sleep(0.1)
            continue
        header = struct.pack(">BBHII", 0x80, OPUS_PT, seq, (i * 960) & 0xFFFFFFFF, ssrc)
        TRANSPORT.sendto(seal(crypto, counter, header, packets[i % len(packets)]), viewer.addr)
        seq, counter, i = (seq + 1) & 0xFFFF, counter + 1, i + 1
        # Keep to the clock, not to the sleep
        await asyncio.sleep(max(0.0, start + i * 0.02 - time.monotonic()))


async def fake_stream(viewer: Session, user_id: str, source: str = "testsrc2") -> None:
    """Sends the test picture to one viewer, as if `user_id` were streaming it
    (Go Live) or had their camera on (the call's own connection)."""
    ssrc = 900000 + random.randint(0, 9999)
    op12 = {"user_id": user_id, "audio_ssrc": ssrc - 1, "video_ssrc": ssrc, "rtx_ssrc": ssrc + 1,
            "streams": [{"type": "video", "rid": "100", "ssrc": ssrc, "active": True, "quality": 100}]}
    await ws_send(viewer.ws, 12, op12)
    if source == "testsrc2":  # a Go Live stream has sound; a camera doesn't
        asyncio.create_task(fake_stream_audio(viewer, ssrc - 1))
    frames = test_frames(source)
    crypto = crypto_for(viewer.server)
    seq, counter, start = random.randint(0, 65535), 1, time.monotonic()
    ext_head, ext_body = b"\xBE\xDE\x00\x01", b"\x51\x00\x00\x00"
    LOG(f"voice: sending {user_id}'s test stream to {viewer.user_id}")
    i = 0
    while viewer in SESSIONS:
        if not viewer.addr or TRANSPORT is None:
            await asyncio.sleep(0.1)
            continue
        frame = frames[i % len(frames)]
        ts = int((time.monotonic() - start) * 90000) & 0xFFFFFFFF
        payloads = packetize(frame)
        for n, payload in enumerate(payloads):
            marker = 0x80 if n == len(payloads) - 1 else 0
            header = struct.pack(">BBHII", 0x90, marker | H264_PT, seq, ts, ssrc)
            clear = header + ext_head
            TRANSPORT.sendto(seal(crypto, counter, clear, ext_body + payload), viewer.addr)
            seq = (seq + 1) & 0xFFFF
            counter += 1
        i += 1
        await asyncio.sleep(1 / 30)


def check_saved_stream() -> int:
    """Decodes the saved stream with ffmpeg; prints what it found."""
    if not os.path.exists(OUT_PATH) or os.path.getsize(OUT_PATH) == 0:
        print("no stream saved")
        return 1
    r = subprocess.run([FFMPEG, "-v", "error", "-i", OUT_PATH, "-f", "null", "-"], capture_output=True, text=True)
    probe = subprocess.run([FFPROBE, "-v", "error", "-count_frames",
                            "-select_streams", "v:0", "-show_entries", "stream=codec_name,profile,width,height,nb_read_frames",
                            "-of", "json", OUT_PATH], capture_output=True, text=True)
    print(probe.stdout.strip())
    if r.stderr.strip():
        print("ffmpeg reported:", r.stderr.strip()[:2000])
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(check_saved_stream())
