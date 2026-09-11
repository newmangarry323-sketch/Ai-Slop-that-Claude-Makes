#!/usr/bin/env python3
"""
Cadence - a single-file music manager with a VS Code inspired interface.

Everything lives in this one file: the audio tag parser, the library index,
the local web server and the user interface. It depends only on the Python
standard library, so it runs from a plain `python Cadence.py` and freezes
cleanly into a one-file executable with PyInstaller.

Usage:
    python Cadence.py                     # open the library (asks for a folder on first run)
    python Cadence.py --folder D:\\Music   # designate the folder and scan it
    python Cadence.py --port 8731         # pin the port
    python Cadence.py --rescan            # force a full re-read of every file
    python Cadence.py --no-browser        # start the server only

License: MIT
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import mimetypes
import os
import re
import secrets
import shutil
import socket
import sqlite3
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

APP_NAME = "Cadence"
APP_VERSION = "1.1.4"
DEFAULT_PORT = 8731

# Extensions we will index. The ones we can actually parse tags for are listed
# in TAGGED_EXTS; anything else still gets indexed using its filename.
AUDIO_EXTS = {
    ".mp3", ".flac", ".ogg", ".oga", ".opus", ".spx", ".m4a", ".m4b", ".mp4",
    ".aac", ".alac", ".wav", ".wave", ".aif", ".aiff", ".aifc", ".wma",
    ".ape", ".wv", ".mpc", ".mp2", ".dsf", ".it", ".mod", ".xm", ".s3m",
}

# Containers that can hold pictures as well as sound, so the extension alone
# cannot decide and the file has to be looked inside.
AMBIGUOUS_EXTS = {".mp4", ".ogg"}

SKIP_DIRS = {
    ".git", ".svn", ".hg", "__pycache__", "node_modules", "$RECYCLE.BIN",
    "System Volume Information", ".Trash", ".Trash-1000", ".DS_Store",
}

MAX_ART_BYTES = 6 * 1024 * 1024


def log(*parts: object) -> None:
    """Print only when a console exists (PyInstaller --noconsole leaves stdout as None)."""
    if sys.stdout is None:
        return
    try:
        print(f"[{APP_NAME}]", *parts, flush=True)
    except Exception:
        pass


def config_dir() -> str:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        path = os.path.join(base, APP_NAME)
    elif sys.platform == "darwin":
        path = os.path.expanduser(f"~/Library/Application Support/{APP_NAME}")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
        path = os.path.join(base, APP_NAME.lower())
    os.makedirs(path, exist_ok=True)
    return path


# ---------------------------------------------------------------------------
# Tag parsing
#
# Each reader takes an open binary file and returns a dict of whatever it could
# find. Missing keys are normal - the caller fills the gaps from the filename.
# ---------------------------------------------------------------------------

def _clean(value: object) -> str:
    if value is None:
        return ""
    text = str(value).replace("\x00", " ").strip()
    return re.sub(r"\s+", " ", text)


def _first_int(value: object) -> int:
    match = re.search(r"\d+", str(value or ""))
    return int(match.group()) if match else 0


def _year_of(value: object) -> int:
    match = re.search(r"(1[0-9]{3}|2[0-9]{3})", str(value or ""))
    return int(match.group()) if match else 0


# --- ID3 (MP3) -------------------------------------------------------------

_ID3_FRAMES = {
    "TIT2": "title", "TT2": "title",
    "TPE1": "artist", "TP1": "artist",
    "TPE2": "albumartist", "TP2": "albumartist",
    "TALB": "album", "TAL": "album",
    "TRCK": "track", "TRK": "track",
    "TPOS": "disc", "TPA": "disc",
    "TCON": "genre", "TCO": "genre",
    "TYER": "year", "TYE": "year",
    "TDRC": "year", "TDRL": "year", "TDAT": "year",
    "TCOM": "composer", "TCM": "composer",
    "TLEN": "_length_ms",
}

_ID3V1_GENRES = tuple(
    "Blues;Classic Rock;Country;Dance;Disco;Funk;Grunge;Hip-Hop;Jazz;Metal;New Age;Oldies;"
    "Other;Pop;R&B;Rap;Reggae;Rock;Techno;Industrial;Alternative;Ska;Death Metal;Pranks;"
    "Soundtrack;Euro-Techno;Ambient;Trip-Hop;Vocal;Jazz+Funk;Fusion;Trance;Classical;"
    "Instrumental;Acid;House;Game;Sound Clip;Gospel;Noise;AlternRock;Bass;Soul;Punk;Space;"
    "Meditative;Instrumental Pop;Instrumental Rock;Ethnic;Gothic;Darkwave;Techno-Industrial;"
    "Electronic;Pop-Folk;Eurodance;Dream;Southern Rock;Comedy;Cult;Gangsta;Top 40;"
    "Christian Rap;Pop/Funk;Jungle;Native American;Cabaret;New Wave;Psychadelic;Rave;"
    "Showtunes;Trailer;Lo-Fi;Tribal;Acid Punk;Acid Jazz;Polka;Retro;Musical;Rock & Roll;"
    "Hard Rock;Folk;Folk-Rock;National Folk;Swing;Fast Fusion;Bebob;Latin;Revival;Celtic;"
    "Bluegrass;Avantgarde;Gothic Rock;Progressive Rock;Psychedelic Rock;Symphonic Rock;"
    "Slow Rock;Big Band;Chorus;Easy Listening;Acoustic;Humour;Speech;Chanson;Opera;"
    "Chamber Music;Sonata;Symphony;Booty Bass;Primus;Porn Groove;Satire;Slow Jam;Club;"
    "Tango;Samba;Folklore;Ballad;Power Ballad;Rhythmic Soul;Freestyle;Duet;Punk Rock;"
    "Drum Solo;A cappella;Euro-House;Dance Hall".split(";")
)


def _id3_text(data: bytes) -> str:
    if not data:
        return ""
    encoding, raw = data[0], data[1:]
    codecs = {0: "latin-1", 1: "utf-16", 2: "utf-16-be", 3: "utf-8"}
    try:
        text = raw.decode(codecs.get(encoding, "latin-1"))
    except Exception:
        text = raw.decode("latin-1", "replace")
    # Multi-value frames are null separated; keep the first meaningful part.
    parts = [p for p in text.split("\x00") if p.strip()]
    return _clean(parts[0]) if parts else ""


def _id3_genre(text: str) -> str:
    # Old encoders write "(17)" or "(17)Rock" instead of a plain name.
    match = re.fullmatch(r"\((\d+)\)(.*)", text.strip())
    if match:
        name = match.group(2).strip()
        if name:
            return name
        index = int(match.group(1))
        if index < len(_ID3V1_GENRES):
            return _ID3V1_GENRES[index]
    return text


def _read_id3v2(fh: io.BufferedReader) -> tuple[dict, int]:
    """Return (tags, bytes consumed by the tag)."""
    fh.seek(0)
    header = fh.read(10)
    if len(header) < 10 or header[:3] != b"ID3":
        return {}, 0
    major, _revision, flags = header[3], header[4], header[5]
    size = (header[6] << 21) | (header[7] << 14) | (header[8] << 7) | header[9]
    body = fh.read(size)
    if flags & 0x80:  # unsynchronisation applied to the whole tag
        body = body.replace(b"\xff\x00", b"\xff")
    if flags & 0x40:  # skip the extended header
        if major >= 4 and len(body) >= 4:
            ext = (body[0] << 21) | (body[1] << 14) | (body[2] << 7) | body[3]
            body = body[ext:]
        elif len(body) >= 4:
            body = body[4 + struct.unpack(">I", body[:4])[0]:]

    tags: dict = {}
    pos = 0
    id_len, size_len = (3, 3) if major == 2 else (4, 4)
    while pos + id_len + size_len <= len(body):
        frame_id = body[pos:pos + id_len]
        if not frame_id.strip(b"\x00"):
            break
        raw_size = body[pos + id_len:pos + id_len + size_len]
        if major == 2:
            frame_size = int.from_bytes(raw_size, "big")
            frame_flags = 0
            head = id_len + size_len
        else:
            if major >= 4:
                frame_size = ((raw_size[0] << 21) | (raw_size[1] << 14)
                              | (raw_size[2] << 7) | raw_size[3])
            else:
                frame_size = struct.unpack(">I", raw_size)[0]
            frame_flags = struct.unpack(">H", body[pos + 8:pos + 10])[0]
            head = 10
        if frame_size <= 0 or pos + head + frame_size > len(body):
            break
        payload = body[pos + head:pos + head + frame_size]
        pos += head + frame_size

        if frame_flags & 0x0002:  # per-frame unsynchronisation
            payload = payload.replace(b"\xff\x00", b"\xff")
        if frame_flags & 0x0001 and len(payload) >= 4:  # data length indicator
            payload = payload[4:]

        name = frame_id.decode("latin-1", "replace")
        if name in ("APIC", "PIC"):
            tags.setdefault("_art", _parse_apic(payload, name))
        elif name in _ID3_FRAMES:
            field = _ID3_FRAMES[name]
            value = _id3_text(payload)
            if field == "genre":
                value = _id3_genre(value)
            if value and not tags.get(field):
                tags[field] = value
    return tags, size + 10


def _parse_apic(payload: bytes, frame: str) -> tuple[str, bytes] | None:
    try:
        encoding = payload[0]
        rest = payload[1:]
        if frame == "PIC":
            mime = {"JPG": "image/jpeg", "PNG": "image/png"}.get(
                rest[:3].decode("latin-1", "replace").upper(), "image/jpeg")
            rest = rest[3:]
        else:
            end = rest.find(b"\x00")
            if end < 0:
                return None
            mime = rest[:end].decode("latin-1", "replace") or "image/jpeg"
            rest = rest[end + 1:]
        rest = rest[1:]  # picture type byte
        # Skip the description, which uses the frame's text encoding.
        if encoding in (1, 2):
            end = 0
            while end + 1 < len(rest) and rest[end:end + 2] != b"\x00\x00":
                end += 2
            rest = rest[end + 2:]
        else:
            end = rest.find(b"\x00")
            rest = rest[end + 1:] if end >= 0 else rest
        if "/" not in mime:
            mime = "image/" + mime.lower().lstrip("-")
        return (mime, rest) if rest else None
    except Exception:
        return None


def _read_id3v1(fh: io.BufferedReader, size: int) -> dict:
    if size < 128:
        return {}
    fh.seek(size - 128)
    block = fh.read(128)
    if len(block) < 128 or block[:3] != b"TAG":
        return {}
    text = lambda b: _clean(b.decode("latin-1", "replace"))
    tags = {
        "title": text(block[3:33]),
        "artist": text(block[33:63]),
        "album": text(block[63:93]),
        "year": text(block[93:97]),
    }
    comment = block[97:127]
    if comment[28] == 0 and comment[29] != 0:
        tags["track"] = str(comment[29])
    genre = block[127]
    if genre < len(_ID3V1_GENRES):
        tags["genre"] = _ID3V1_GENRES[genre]
    return {k: v for k, v in tags.items() if v}


# --- MPEG audio frames (duration / bitrate for MP3) ------------------------

_MPEG_BITRATES = {
    (3, 3): [0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448],
    (3, 2): [0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384],
    (3, 1): [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
    (2, 3): [0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256],
    (2, 2): [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
}
_MPEG_BITRATES[(2, 1)] = _MPEG_BITRATES[(2, 2)]
_MPEG_RATES = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def _parse_mpeg_header(head: bytes) -> dict | None:
    if len(head) < 4 or head[0] != 0xFF or (head[1] & 0xE0) != 0xE0:
        return None
    version = (head[1] >> 3) & 0x03   # 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
    layer = (head[1] >> 1) & 0x03     # 3 = Layer I, 2 = Layer II, 1 = Layer III
    if version == 1 or layer == 0:
        return None
    bitrate_index = (head[2] >> 4) & 0x0F
    rate_index = (head[2] >> 2) & 0x03
    if bitrate_index in (0, 15) or rate_index == 3:
        return None
    table_key = (3 if version == 3 else 2, layer)
    bitrate = _MPEG_BITRATES[table_key][bitrate_index] * 1000
    samplerate = _MPEG_RATES[version][rate_index]
    padding = (head[2] >> 1) & 0x01
    mode = (head[3] >> 6) & 0x03      # 3 = mono
    if layer == 3:
        samples = 384
        length = (12 * bitrate // samplerate + padding) * 4
    elif layer == 2:
        samples = 1152
        length = 144 * bitrate // samplerate + padding
    else:
        samples = 1152 if version == 3 else 576
        length = (144 if version == 3 else 72) * bitrate // samplerate + padding
    return {
        "bitrate": bitrate, "samplerate": samplerate, "channels": 1 if mode == 3 else 2,
        "frame_len": length, "samples": samples, "version": version, "layer": layer,
        "mono": mode == 3,
    }


def _read_mp3_stream(fh: io.BufferedReader, start: int, size: int) -> dict:
    """Find the first audio frame, then prefer Xing/VBRI headers for duration."""
    fh.seek(start)
    window = fh.read(min(256 * 1024, max(size - start, 0)))
    frame = None
    offset = 0
    while offset < len(window) - 4:
        index = window.find(b"\xff", offset)
        if index < 0 or index > len(window) - 4:
            break
        candidate = _parse_mpeg_header(window[index:index + 4])
        if candidate:
            # Confirm with the following frame to avoid matching random bytes.
            nxt = index + candidate["frame_len"]
            if nxt + 4 > len(window) or _parse_mpeg_header(window[nxt:nxt + 4]):
                frame, offset = candidate, index
                break
        offset = index + 1
    if not frame:
        return {}

    audio_bytes = size - (start + offset)
    info = {
        "bitrate": frame["bitrate"], "samplerate": frame["samplerate"],
        "channels": frame["channels"],
        "codec": {1: "MP3", 2: "MP2", 3: "MP1"}[frame["layer"]],
    }

    xing_at = offset + 4 + ((17 if frame["mono"] else 32) if frame["version"] == 3
                            else (9 if frame["mono"] else 17))
    tag = window[xing_at:xing_at + 4]
    if tag in (b"Xing", b"Info"):
        flags = struct.unpack(">I", window[xing_at + 4:xing_at + 8])[0]
        cursor = xing_at + 8
        frames = byte_count = 0
        if flags & 0x01:
            frames = struct.unpack(">I", window[cursor:cursor + 4])[0]
            cursor += 4
        if flags & 0x02:
            byte_count = struct.unpack(">I", window[cursor:cursor + 4])[0]
        if frames:
            info["duration"] = frames * frame["samples"] / frame["samplerate"]
            if byte_count and info["duration"] > 0:
                info["bitrate"] = int(byte_count * 8 / info["duration"])
            return info
    elif window[offset + 4 + 32:offset + 4 + 36] == b"VBRI":
        base = offset + 4 + 32
        byte_count = struct.unpack(">I", window[base + 10:base + 14])[0]
        frames = struct.unpack(">I", window[base + 14:base + 18])[0]
        if frames:
            info["duration"] = frames * frame["samples"] / frame["samplerate"]
            if byte_count and info["duration"] > 0:
                info["bitrate"] = int(byte_count * 8 / info["duration"])
            return info

    if frame["bitrate"] > 0:
        info["duration"] = audio_bytes * 8 / frame["bitrate"]
    return info


def read_mp3(fh: io.BufferedReader, size: int) -> dict:
    tags, tag_size = _read_id3v2(fh)
    for key, value in _read_id3v1(fh, size).items():
        tags.setdefault(key, value)
    tags.update({k: v for k, v in _read_mp3_stream(fh, tag_size, size).items()
                 if k not in tags or k in ("bitrate", "samplerate", "channels", "codec")})
    if tags.get("_length_ms") and not tags.get("duration"):
        tags["duration"] = _first_int(tags["_length_ms"]) / 1000.0
    tags.pop("_length_ms", None)
    return tags


# --- Vorbis comments (FLAC / Ogg / Opus) -----------------------------------

_VORBIS_KEYS = {
    "title": "title", "artist": "artist", "album": "album",
    "albumartist": "albumartist", "album artist": "albumartist",
    "tracknumber": "track", "track": "track", "discnumber": "disc",
    "disc": "disc", "date": "year", "year": "year", "genre": "genre",
    "composer": "composer",
}


def _parse_vorbis_comment(data: bytes) -> dict:
    tags: dict = {}
    try:
        pos = 0
        vendor_len = struct.unpack("<I", data[pos:pos + 4])[0]
        pos += 4 + vendor_len
        count = struct.unpack("<I", data[pos:pos + 4])[0]
        pos += 4
        for _ in range(min(count, 512)):
            length = struct.unpack("<I", data[pos:pos + 4])[0]
            pos += 4
            entry = data[pos:pos + length].decode("utf-8", "replace")
            pos += length
            if "=" not in entry:
                continue
            key, _, value = entry.partition("=")
            key = key.strip().lower()
            if key == "metadata_block_picture" and "_art" not in tags:
                art = _parse_flac_picture(base64.b64decode(value))
                if art:
                    tags["_art"] = art
            elif key in _VORBIS_KEYS and not tags.get(_VORBIS_KEYS[key]):
                tags[_VORBIS_KEYS[key]] = _clean(value)
    except Exception:
        pass
    return tags


def _parse_flac_picture(block: bytes) -> tuple[str, bytes] | None:
    try:
        pos = 4  # picture type
        mime_len = struct.unpack(">I", block[pos:pos + 4])[0]
        pos += 4
        mime = block[pos:pos + mime_len].decode("latin-1", "replace")
        pos += mime_len
        desc_len = struct.unpack(">I", block[pos:pos + 4])[0]
        pos += 4 + desc_len + 16  # description + width/height/depth/colours
        data_len = struct.unpack(">I", block[pos:pos + 4])[0]
        pos += 4
        data = block[pos:pos + data_len]
        return (mime or "image/jpeg", data) if data else None
    except Exception:
        return None


def read_flac(fh: io.BufferedReader, size: int) -> dict:
    fh.seek(0)
    if fh.read(4) != b"fLaC":
        return {}
    tags: dict = {"codec": "FLAC"}
    while True:
        header = fh.read(4)
        if len(header) < 4:
            break
        last = header[0] & 0x80
        block_type = header[0] & 0x7F
        length = int.from_bytes(header[1:4], "big")
        if length > 64 * 1024 * 1024:
            break
        block = fh.read(length)
        if block_type == 0 and len(block) >= 18:  # STREAMINFO
            packed = int.from_bytes(block[10:18], "big")
            samplerate = packed >> 44
            channels = ((packed >> 41) & 0x07) + 1
            bits = ((packed >> 36) & 0x1F) + 1
            total = packed & 0xFFFFFFFFF
            if samplerate:
                tags["samplerate"] = samplerate
                tags["channels"] = channels
                tags["bitdepth"] = bits
                if total:
                    tags["duration"] = total / samplerate
                    tags["bitrate"] = int(size * 8 / (total / samplerate))
        elif block_type == 4:  # VORBIS_COMMENT
            tags.update(_parse_vorbis_comment(block))
        elif block_type == 6 and "_art" not in tags:  # PICTURE
            art = _parse_flac_picture(block)
            if art:
                tags["_art"] = art
        if last:
            break
    return tags


def _ogg_packets(fh: io.BufferedReader, max_pages: int = 24) -> list[bytes]:
    """Return the first few logical packets of an Ogg stream."""
    fh.seek(0)
    packets: list[bytes] = []
    pending = b""
    for _ in range(max_pages):
        header = fh.read(27)
        if len(header) < 27 or header[:4] != b"OggS":
            break
        segment_count = header[26]
        table = fh.read(segment_count)
        if len(table) < segment_count:
            break
        body = fh.read(sum(table))
        cursor = 0
        for length in table:
            pending += body[cursor:cursor + length]
            cursor += length
            if length < 255:
                packets.append(pending)
                pending = b""
        if len(packets) >= 2:
            break
    return packets


def _ogg_last_granule(fh: io.BufferedReader, size: int) -> int:
    fh.seek(max(0, size - 65536))
    tail = fh.read(65536)
    index = tail.rfind(b"OggS")
    if index < 0 or index + 14 > len(tail):
        return 0
    return struct.unpack("<q", tail[index + 6:index + 14])[0]


def read_ogg(fh: io.BufferedReader, size: int) -> dict:
    packets = _ogg_packets(fh)
    if not packets:
        return {}
    if any(p.startswith(b"\x80theora") for p in packets):
        return {"video": True}
    head = packets[0]
    tags: dict = {}
    samplerate = 0
    if head.startswith(b"\x01vorbis"):
        tags["codec"] = "Vorbis"
        samplerate = struct.unpack("<I", head[12:16])[0]
        tags["channels"] = head[11]
        nominal = struct.unpack("<i", head[20:24])[0]
        if nominal > 0:
            tags["bitrate"] = nominal
        if len(packets) > 1 and packets[1].startswith(b"\x03vorbis"):
            tags.update(_parse_vorbis_comment(packets[1][7:]))
    elif head.startswith(b"OpusHead"):
        tags["codec"] = "Opus"
        tags["channels"] = head[9]
        samplerate = 48000  # Opus always decodes to 48 kHz
        for packet in packets[1:]:
            if packet.startswith(b"OpusTags"):
                tags.update(_parse_vorbis_comment(packet[8:]))
                break
    elif head.startswith(b"\x80theora") or head.startswith(b"\x80video"):
        return {"video": True}
    elif head.startswith(b"Speex   "):
        tags["codec"] = "Speex"
        samplerate = struct.unpack("<I", head[36:40])[0]
    if samplerate:
        tags["samplerate"] = samplerate
        granule = _ogg_last_granule(fh, size)
        if granule > 0:
            tags["duration"] = granule / samplerate
            tags.setdefault("bitrate", int(size * 8 / (granule / samplerate)))
    return tags


# --- MP4 / M4A -------------------------------------------------------------

_MP4_CONTAINERS = {b"moov", b"udta", b"trak", b"mdia", b"minf", b"stbl", b"meta", b"ilst"}
_MP4_CODECS = {b"mp4a": "AAC", b"alac": "ALAC", b"ac-3": "AC3", b"ec-3": "EAC3",
               b"samr": "AMR", b".mp3": "MP3", b"lpcm": "PCM"}
# .mp4 is a container, not a format: it holds music videos as readily as albums.
# A sample entry in this set, or a track whose handler is "vide", means pictures.
_MP4_VIDEO_CODECS = {b"avc1", b"avc3", b"hvc1", b"hev1", b"mp4v", b"av01", b"vp08",
                     b"vp09", b"s263", b"encv", b"dvh1", b"dvhe", b"jpeg", b"mjpa"}
_MP4_KEYS = {
    b"\xa9nam": "title", b"\xa9ART": "artist", b"aART": "albumartist",
    b"\xa9alb": "album", b"\xa9day": "year", b"\xa9gen": "genre",
    b"gnre": "genre", b"\xa9wrt": "composer", b"trkn": "track", b"disk": "disc",
    b"covr": "_art",
}


def _mp4_walk(fh: io.BufferedReader, end: int, tags: dict, depth: int = 0) -> None:
    if depth > 8:
        return
    while fh.tell() + 8 <= end:
        start = fh.tell()
        header = fh.read(8)
        if len(header) < 8:
            return
        box_size = struct.unpack(">I", header[:4])[0]
        box_type = header[4:8]
        if box_size == 1:
            extended = fh.read(8)
            if len(extended) < 8:
                return
            box_size = struct.unpack(">Q", extended)[0]
        elif box_size == 0:
            box_size = end - start
        if box_size < 8 or start + box_size > end:
            return
        box_end = start + box_size

        if box_type == b"hdlr":
            payload = fh.read(min(box_size - 8, 24))
            if len(payload) >= 12 and payload[8:12] == b"vide":
                tags["video"] = True
        elif box_type == b"stsd":
            entry = fh.read(min(box_size - 8, 96))
            try:
                fmt = entry[12:16]
                if fmt in _MP4_VIDEO_CODECS:
                    tags["video"] = True
                else:
                    # Only an audio sample entry has channels and rate at these
                    # offsets; reading them off a video one yields nonsense.
                    tags["codec"] = _MP4_CODECS.get(
                        fmt, fmt.decode("latin-1", "replace").strip())
                    tags["channels"] = struct.unpack(">H", entry[32:34])[0]
                    tags["samplerate"] = struct.unpack(">H", entry[40:42])[0]
            except Exception:
                pass
        elif box_type == b"mdhd":
            payload = fh.read(min(box_size - 8, 40))
            try:
                if payload[0] == 1:
                    timescale = struct.unpack(">I", payload[20:24])[0]
                    duration = struct.unpack(">Q", payload[24:32])[0]
                else:
                    timescale = struct.unpack(">I", payload[12:16])[0]
                    duration = struct.unpack(">I", payload[16:20])[0]
                if timescale and duration:
                    # The media header describes the track's own timeline, which is
                    # more trustworthy than the movie header when the two disagree.
                    tags["duration"] = duration / timescale
                    tags.setdefault("samplerate", timescale)
            except Exception:
                pass
        elif box_type == b"mvhd":
            payload = fh.read(min(box_size - 8, 120))
            try:
                if payload[0] == 1:
                    timescale = struct.unpack(">I", payload[20:24])[0]
                    duration = struct.unpack(">Q", payload[24:32])[0]
                else:
                    timescale = struct.unpack(">I", payload[12:16])[0]
                    duration = struct.unpack(">I", payload[16:20])[0]
                if timescale and not tags.get("duration"):
                    tags["duration"] = duration / timescale
            except Exception:
                pass
        elif box_type in _MP4_CONTAINERS:
            if box_type == b"meta":
                fh.read(4)  # meta carries a version/flags word before children
            _mp4_walk(fh, box_end, tags, depth + 1)
        elif box_type in _MP4_KEYS and box_size < 32 * 1024 * 1024:
            field = _MP4_KEYS[box_type]
            payload = fh.read(box_size - 8)
            value = _mp4_value(payload, box_type)
            if value and not tags.get(field):
                tags[field] = value
        fh.seek(box_end)


def _mp4_value(payload: bytes, box_type: bytes):
    index = payload.find(b"data")
    if index < 4:
        return None
    flags = int.from_bytes(payload[index + 4:index + 8], "big") & 0xFFFFFF
    body = payload[index + 12:]
    if box_type == b"covr":
        mime = "image/png" if flags == 14 else "image/jpeg"
        return (mime, body) if body else None
    if box_type in (b"trkn", b"disk") and len(body) >= 4:
        return str(struct.unpack(">H", body[2:4])[0])
    if box_type == b"gnre" and len(body) >= 2:
        index = struct.unpack(">H", body[:2])[0] - 1
        return _ID3V1_GENRES[index] if 0 <= index < len(_ID3V1_GENRES) else None
    if flags == 1:
        return _clean(body.decode("utf-8", "replace"))
    return _clean(body.decode("utf-8", "replace")) or None


def read_mp4(fh: io.BufferedReader, size: int) -> dict:
    fh.seek(4)
    if fh.read(4) != b"ftyp":
        return {}
    fh.seek(0)
    tags: dict = {"codec": "AAC"}
    _mp4_walk(fh, size, tags)
    if tags.get("video"):
        return {"video": True}
    if tags.get("duration"):
        tags.setdefault("bitrate", int(size * 8 / tags["duration"]))
    return tags


# --- RIFF / AIFF -----------------------------------------------------------

_RIFF_INFO = {
    b"INAM": "title", b"IART": "artist", b"IPRD": "album", b"ICRD": "year",
    b"IGNR": "genre", b"ITRK": "track", b"IMUS": "composer",
}


def read_wav(fh: io.BufferedReader, size: int) -> dict:
    fh.seek(0)
    if fh.read(4) != b"RIFF" or fh.read(8)[4:8] != b"WAVE":
        return {}
    fh.seek(12)
    tags: dict = {"codec": "PCM"}
    byte_rate = 0
    while fh.tell() + 8 <= size:
        header = fh.read(8)
        if len(header) < 8:
            break
        chunk_id, chunk_size = header[:4], struct.unpack("<I", header[4:8])[0]
        body_at = fh.tell()
        if chunk_id == b"fmt " and chunk_size >= 16:
            fmt = fh.read(16)
            tags["channels"] = struct.unpack("<H", fmt[2:4])[0]
            tags["samplerate"] = struct.unpack("<I", fmt[4:8])[0]
            byte_rate = struct.unpack("<I", fmt[8:12])[0]
            tags["bitdepth"] = struct.unpack("<H", fmt[14:16])[0]
            tags["bitrate"] = byte_rate * 8
        elif chunk_id == b"data" and byte_rate:
            tags["duration"] = chunk_size / byte_rate
        elif chunk_id == b"LIST" and chunk_size < 1024 * 1024:
            block = fh.read(chunk_size)
            if block[:4] == b"INFO":
                pos = 4
                while pos + 8 <= len(block):
                    key = block[pos:pos + 4]
                    length = struct.unpack("<I", block[pos + 4:pos + 8])[0]
                    value = block[pos + 8:pos + 8 + length]
                    if key in _RIFF_INFO:
                        tags.setdefault(_RIFF_INFO[key], _clean(value.decode("latin-1", "replace")))
                    pos += 8 + length + (length & 1)
        fh.seek(body_at + chunk_size + (chunk_size & 1))
        if fh.tell() <= body_at:
            break
    # Some WAV files carry an ID3 chunk; and stray ID3v2 at the head is common.
    id3, _ = _read_id3v2(fh)
    for key, value in id3.items():
        tags.setdefault(key, value)
    return tags


def read_aiff(fh: io.BufferedReader, size: int) -> dict:
    fh.seek(0)
    if fh.read(4) != b"FORM":
        return {}
    fh.seek(12)
    tags: dict = {"codec": "AIFF"}
    while fh.tell() + 8 <= size:
        header = fh.read(8)
        if len(header) < 8:
            break
        chunk_id, chunk_size = header[:4], struct.unpack(">I", header[4:8])[0]
        body_at = fh.tell()
        if chunk_id == b"COMM" and chunk_size >= 18:
            body = fh.read(18)
            channels = struct.unpack(">H", body[:2])[0]
            frames = struct.unpack(">I", body[2:6])[0]
            depth = struct.unpack(">H", body[6:8])[0]
            rate = _extended_float(body[8:18])
            tags.update({"channels": channels, "bitdepth": depth})
            if rate:
                tags["samplerate"] = int(rate)
                tags["duration"] = frames / rate
                tags["bitrate"] = int(rate * channels * depth)
        elif chunk_id in (b"NAME", b"AUTH") and chunk_size < 8192:
            field = "title" if chunk_id == b"NAME" else "artist"
            tags.setdefault(field, _clean(fh.read(chunk_size).decode("latin-1", "replace")))
        fh.seek(body_at + chunk_size + (chunk_size & 1))
        if fh.tell() <= body_at:
            break
    return tags


def _extended_float(raw: bytes) -> float:
    """Decode an 80-bit IEEE extended float (used by AIFF for the sample rate)."""
    try:
        exponent = struct.unpack(">H", raw[:2])[0]
        mantissa = struct.unpack(">Q", raw[2:10])[0]
        sign = -1 if exponent & 0x8000 else 1
        exponent &= 0x7FFF
        if exponent == 0 and mantissa == 0:
            return 0.0
        return sign * mantissa * (2.0 ** (exponent - 16383 - 63))
    except Exception:
        return 0.0


_READERS = {
    ".mp3": read_mp3, ".mp2": read_mp3,
    ".flac": read_flac,
    ".ogg": read_ogg, ".oga": read_ogg, ".opus": read_ogg, ".spx": read_ogg,
    ".m4a": read_mp4, ".m4b": read_mp4, ".mp4": read_mp4, ".aac": read_mp4,
    ".alac": read_mp4,
    ".wav": read_wav, ".wave": read_wav,
    ".aif": read_aiff, ".aiff": read_aiff, ".aifc": read_aiff,
}


def read_metadata(path: str, root: str | None = None) -> dict:
    """Read whatever tags we can, then fill the gaps from the path itself.

    `root` is the library folder. Knowing it keeps the Artist/Album/track folder
    convention from inventing an artist out of the library folder's own name.
    """
    ext = os.path.splitext(path)[1].lower()
    size = os.path.getsize(path)
    tags: dict = {}
    reader = _READERS.get(ext)
    if reader:
        try:
            with open(path, "rb") as fh:
                tags = reader(fh, size) or {}
        except Exception as exc:
            log("tag read failed", os.path.basename(path), repr(exc))

    if tags.get("video"):
        return {"video": True}

    art = tags.pop("_art", None)
    stem = os.path.splitext(os.path.basename(path))[0]

    if not tags.get("title") or not tags.get("artist"):
        # "01 - Artist - Title" / "Artist - Title" / "01. Title"
        guess = re.sub(r"^\s*\d{1,3}\s*[-._)]\s*", "", stem)
        if " - " in guess:
            left, _, right = guess.partition(" - ")
            tags.setdefault("artist", _clean(left))
            tags.setdefault("title", _clean(right))
        else:
            tags.setdefault("title", _clean(guess))
    if not tags.get("track"):
        match = re.match(r"^\s*(\d{1,3})\s*[-._) ]", stem)
        if match:
            tags["track"] = match.group(1)
    parent_dir = os.path.dirname(path)
    grandparent_dir = os.path.dirname(parent_dir)
    normalised_root = os.path.normpath(root) if root else None

    def outside_root(candidate: str) -> bool:
        return normalised_root is None or os.path.normpath(candidate) != normalised_root

    if outside_root(parent_dir):
        tags.setdefault("album", _clean(os.path.basename(parent_dir)) or "Unknown Album")
    if not tags.get("artist") and outside_root(grandparent_dir) and grandparent_dir != parent_dir:
        tags["artist"] = _clean(os.path.basename(grandparent_dir)) or "Unknown Artist"

    return {
        "title": _clean(tags.get("title")) or stem,
        "artist": _clean(tags.get("artist")) or "Unknown Artist",
        "album": _clean(tags.get("album")) or "Unknown Album",
        "albumartist": _clean(tags.get("albumartist")) or _clean(tags.get("artist")) or "Unknown Artist",
        "genre": _clean(tags.get("genre")),
        "composer": _clean(tags.get("composer")),
        "year": _year_of(tags.get("year")),
        "track": _first_int(tags.get("track")),
        "disc": _first_int(tags.get("disc")),
        "duration": float(tags.get("duration") or 0.0),
        "bitrate": int(tags.get("bitrate") or 0),
        "samplerate": int(tags.get("samplerate") or 0),
        "channels": int(tags.get("channels") or 0),
        "codec": _clean(tags.get("codec")) or ext.lstrip(".").upper(),
        "art": art,
    }


# ---------------------------------------------------------------------------
# Library index
#
# A SQLite file in the config directory. Rescans are incremental: a file is
# only re-parsed when its size or mtime changed, so relaunching is instant.
# ---------------------------------------------------------------------------

SCHEMA = """
CREATE TABLE IF NOT EXISTS tracks (
    id          INTEGER PRIMARY KEY,
    path        TEXT UNIQUE NOT NULL,
    folder      TEXT NOT NULL,
    filename    TEXT NOT NULL,
    size        INTEGER NOT NULL,
    mtime       REAL NOT NULL,
    title       TEXT, artist TEXT, album TEXT, albumartist TEXT,
    genre       TEXT, composer TEXT,
    year        INTEGER, track INTEGER, disc INTEGER,
    duration    REAL, bitrate INTEGER, samplerate INTEGER, channels INTEGER,
    codec       TEXT, art_mime TEXT, art BLOB,
    added       REAL, plays INTEGER DEFAULT 0, last_played REAL DEFAULT 0,
    rating      INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_artist ON tracks(artist);
CREATE INDEX IF NOT EXISTS idx_album  ON tracks(album);
CREATE INDEX IF NOT EXISTS idx_folder ON tracks(folder);

CREATE TABLE IF NOT EXISTS playlists (
    id      INTEGER PRIMARY KEY,
    name    TEXT UNIQUE NOT NULL,
    created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS playlist_items (
    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, track_id)
);
-- Files we looked inside and decided against, so a rescan does not open every
-- video in the folder again just to reach the same conclusion.
CREATE TABLE IF NOT EXISTS skipped (
    path   TEXT PRIMARY KEY,
    size   INTEGER NOT NULL,
    mtime  REAL NOT NULL,
    reason TEXT
);
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""

TRACK_COLUMNS = (
    "id, path, folder, filename, size, title, artist, album, albumartist, genre, "
    "year, track, disc, duration, bitrate, samplerate, channels, codec, "
    "(art IS NOT NULL) AS has_art, added, plays, last_played, rating"
)


class Library:
    def __init__(self, db_path: str):
        self.db_path = db_path
        self._local = threading.local()
        self._write_lock = threading.Lock()
        self.scan_state = {
            "running": False, "found": 0, "added": 0, "updated": 0,
            "removed": 0, "current": "", "started": 0.0, "finished": 0.0,
            "errors": 0, "log": [],
        }
        with self.connect() as conn:
            conn.executescript(SCHEMA)

    def connect(self) -> sqlite3.Connection:
        conn = getattr(self._local, "conn", None)
        if conn is None:
            conn = sqlite3.connect(self.db_path, timeout=30, check_same_thread=False)
            conn.row_factory = sqlite3.Row
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("PRAGMA synchronous=NORMAL")
            conn.execute("PRAGMA foreign_keys=ON")
            self._local.conn = conn
        return conn

    # -- settings -----------------------------------------------------------

    def get_setting(self, key: str, default: str = "") -> str:
        row = self.connect().execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
        return row["value"] if row else default

    def set_setting(self, key: str, value: str) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.execute("INSERT INTO settings(key, value) VALUES(?,?) "
                         "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (key, str(value)))
            conn.commit()

    # -- scanning -----------------------------------------------------------

    def _note(self, message: str) -> None:
        entries = self.scan_state["log"]
        entries.append(f"{time.strftime('%H:%M:%S')}  {message}")
        del entries[:-400]

    def walk(self, root: str):
        for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not d.startswith(".")]
            for name in filenames:
                if os.path.splitext(name)[1].lower() in AUDIO_EXTS:
                    yield os.path.join(dirpath, name)

    def recheck_ambiguous_once(self) -> int:
        """Force a re-read of container formats indexed before video was filtered.

        The incremental scan skips a file whose size and timestamp are unchanged,
        so a video an earlier build accepted would never be looked at again.
        Dropping those rows once makes the next scan re-read them, after which
        the usual incremental path applies.
        """
        if self.get_setting("video_filter") == "1":
            return 0
        conn = self.connect()
        stale = [row["path"] for row in conn.execute("SELECT path FROM tracks")
                 if os.path.splitext(row["path"])[1].lower() in AMBIGUOUS_EXTS]
        if stale:
            with self._write_lock:
                conn.executemany("DELETE FROM tracks WHERE path=?", [(p,) for p in stale])
                conn.commit()
        self.set_setting("video_filter", "1")
        return len(stale)

    def scan(self, root: str, full: bool = False) -> dict:
        """Index `root`. Incremental unless `full`, and safe to call repeatedly."""
        state = self.scan_state
        if state["running"]:
            return state
        state.update({"running": True, "found": 0, "added": 0, "updated": 0,
                      "removed": 0, "errors": 0, "skipped": 0, "current": "", "log": [],
                      "started": time.time(), "finished": 0.0})
        conn = self.connect()
        try:
            if not root or not os.path.isdir(root):
                self._note(f"Folder not found: {root or '(not set)'}")
                state["error"] = f"Folder not found: {root or '(not set)'}"
                return state

            self._note(f"Scanning {root}")
            rechecked = self.recheck_ambiguous_once()
            if rechecked:
                self._note(f"Re-reading {rechecked} container file(s) to check for video")
            known = {row["path"]: (row["size"], row["mtime"], row["id"])
                     for row in conn.execute("SELECT id, path, size, mtime FROM tracks")}
            known_skips = {row["path"]: (row["size"], row["mtime"])
                           for row in conn.execute("SELECT path, size, mtime FROM skipped")}
            seen: set[str] = set()
            seen_skips: set[str] = set()
            new_skips: list[tuple] = []
            batch: list[tuple] = []
            started = time.time()

            for path in self.walk(root):
                seen.add(path)
                state["found"] += 1
                state["current"] = path
                try:
                    stat = os.stat(path)
                except OSError:
                    state["errors"] += 1
                    continue
                previous = known.get(path)
                if previous and not full and previous[0] == stat.st_size and \
                        abs(previous[1] - stat.st_mtime) < 1e-6:
                    continue
                was_skipped = known_skips.get(path)
                if was_skipped and not full and was_skipped[0] == stat.st_size and \
                        abs(was_skipped[1] - stat.st_mtime) < 1e-6:
                    seen_skips.add(path)
                    state["skipped"] += 1
                    continue
                try:
                    meta = read_metadata(path, root)
                except Exception as exc:
                    state["errors"] += 1
                    self._note(f"error: {os.path.basename(path)} - {exc}")
                    continue
                if meta.get("video"):
                    # A container that turned out to hold pictures. Remember the
                    # verdict, and drop it if an earlier scan had let it in.
                    new_skips.append((path, stat.st_size, stat.st_mtime, "video"))
                    seen_skips.add(path)
                    seen.discard(path)
                    state["skipped"] += 1
                    continue
                art = meta.pop("art", None)
                art_mime, art_blob = (art if art and len(art[1]) <= MAX_ART_BYTES else (None, None))
                batch.append((
                    path, os.path.dirname(path), os.path.basename(path),
                    stat.st_size, stat.st_mtime,
                    meta["title"], meta["artist"], meta["album"], meta["albumartist"],
                    meta["genre"], meta["composer"], meta["year"], meta["track"],
                    meta["disc"], meta["duration"], meta["bitrate"], meta["samplerate"],
                    meta["channels"], meta["codec"], art_mime, art_blob, time.time(),
                ))
                if previous:
                    state["updated"] += 1
                else:
                    state["added"] += 1
                if len(batch) >= 200:
                    self._flush(conn, batch)
                    batch.clear()

            if batch:
                self._flush(conn, batch)

            if new_skips:
                with self._write_lock:
                    conn.executemany(
                        "INSERT INTO skipped(path, size, mtime, reason) VALUES(?,?,?,?) "
                        "ON CONFLICT(path) DO UPDATE SET size=excluded.size, "
                        "mtime=excluded.mtime, reason=excluded.reason", new_skips)
                    conn.executemany("DELETE FROM tracks WHERE path=?",
                                     [(entry[0],) for entry in new_skips])
                    conn.commit()

            stale_skips = [path for path in known_skips
                           if path not in seen_skips and path.startswith(root)]
            if stale_skips:
                with self._write_lock:
                    conn.executemany("DELETE FROM skipped WHERE path=?",
                                     [(p,) for p in stale_skips])
                    conn.commit()

            gone = [path for path in known if path not in seen and path.startswith(root)]
            if gone:
                with self._write_lock:
                    conn.executemany("DELETE FROM tracks WHERE path=?", [(p,) for p in gone])
                    conn.commit()
                state["removed"] = len(gone)

            elapsed = time.time() - started
            self._note(f"Done in {elapsed:.1f}s - {state['found']} files, "
                       f"{state['added']} added, {state['updated']} updated, "
                       f"{state['removed']} removed, {state['skipped']} video skipped, "
                       f"{state['errors']} errors")
            self.set_setting("last_scan", str(time.time()))
        finally:
            state["current"] = ""
            state["running"] = False
            state["finished"] = time.time()
        return state

    def _flush(self, conn: sqlite3.Connection, batch: list[tuple]) -> None:
        with self._write_lock:
            conn.executemany(
                """INSERT INTO tracks
                   (path, folder, filename, size, mtime, title, artist, album,
                    albumartist, genre, composer, year, track, disc, duration,
                    bitrate, samplerate, channels, codec, art_mime, art, added)
                   VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
                   ON CONFLICT(path) DO UPDATE SET
                     size=excluded.size, mtime=excluded.mtime, title=excluded.title,
                     artist=excluded.artist, album=excluded.album,
                     albumartist=excluded.albumartist, genre=excluded.genre,
                     composer=excluded.composer, year=excluded.year,
                     track=excluded.track, disc=excluded.disc,
                     duration=excluded.duration, bitrate=excluded.bitrate,
                     samplerate=excluded.samplerate, channels=excluded.channels,
                     codec=excluded.codec, art_mime=excluded.art_mime, art=excluded.art
                """, batch)
            conn.commit()

    def scan_async(self, root: str, full: bool = False) -> None:
        if self.scan_state["running"]:
            return
        threading.Thread(target=self.scan, args=(root, full), daemon=True).start()

    # -- queries ------------------------------------------------------------

    def tracks(self) -> list[dict]:
        rows = self.connect().execute(
            f"SELECT {TRACK_COLUMNS} FROM tracks "
            "ORDER BY albumartist COLLATE NOCASE, album COLLATE NOCASE, disc, track, title"
        ).fetchall()
        return [dict(row) for row in rows]

    def track(self, track_id: int) -> dict | None:
        row = self.connect().execute("SELECT * FROM tracks WHERE id=?", (track_id,)).fetchone()
        return dict(row) if row else None

    def art(self, track_id: int) -> tuple[str, bytes] | None:
        row = self.connect().execute(
            "SELECT art_mime, art FROM tracks WHERE id=?", (track_id,)).fetchone()
        if row and row["art"]:
            return row["art_mime"] or "image/jpeg", row["art"]
        return None

    def stats(self) -> dict:
        row = self.connect().execute(
            "SELECT COUNT(*) AS tracks, COALESCE(SUM(duration),0) AS seconds, "
            "COALESCE(SUM(size),0) AS bytes, COUNT(DISTINCT artist) AS artists, "
            "COUNT(DISTINCT album) AS albums FROM tracks").fetchone()
        return dict(row)

    def mark_played(self, track_id: int) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.execute("UPDATE tracks SET plays=plays+1, last_played=? WHERE id=?",
                         (time.time(), track_id))
            conn.commit()

    def set_rating(self, track_id: int, rating: int) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.execute("UPDATE tracks SET rating=? WHERE id=?", (max(0, min(5, rating)), track_id))
            conn.commit()

    # -- playlists ----------------------------------------------------------

    def playlists(self) -> list[dict]:
        rows = self.connect().execute(
            "SELECT p.id, p.name, p.created, COUNT(i.track_id) AS count, "
            "COALESCE(SUM(t.duration),0) AS seconds "
            "FROM playlists p "
            "LEFT JOIN playlist_items i ON i.playlist_id = p.id "
            "LEFT JOIN tracks t ON t.id = i.track_id "
            "GROUP BY p.id ORDER BY p.name COLLATE NOCASE").fetchall()
        return [dict(row) for row in rows]

    def playlist_tracks(self, playlist_id: int) -> list[int]:
        rows = self.connect().execute(
            "SELECT track_id FROM playlist_items WHERE playlist_id=? ORDER BY position",
            (playlist_id,)).fetchall()
        return [row["track_id"] for row in rows]

    def create_playlist(self, name: str) -> int:
        with self._write_lock:
            conn = self.connect()
            cursor = conn.execute("INSERT INTO playlists(name, created) VALUES(?,?)",
                                  (name, time.time()))
            conn.commit()
            return int(cursor.lastrowid)

    def rename_playlist(self, playlist_id: int, name: str) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.execute("UPDATE playlists SET name=? WHERE id=?", (name, playlist_id))
            conn.commit()

    def delete_playlist(self, playlist_id: int) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.execute("DELETE FROM playlist_items WHERE playlist_id=?", (playlist_id,))
            conn.execute("DELETE FROM playlists WHERE id=?", (playlist_id,))
            conn.commit()

    def add_to_playlist(self, playlist_id: int, track_ids: list[int]) -> int:
        with self._write_lock:
            conn = self.connect()
            row = conn.execute("SELECT COALESCE(MAX(position), -1) AS top "
                               "FROM playlist_items WHERE playlist_id=?", (playlist_id,)).fetchone()
            position = row["top"] + 1
            added = 0
            for track_id in track_ids:
                try:
                    conn.execute("INSERT INTO playlist_items(playlist_id, track_id, position) "
                                 "VALUES(?,?,?)", (playlist_id, track_id, position))
                    position += 1
                    added += 1
                except sqlite3.IntegrityError:
                    continue
            conn.commit()
            return added

    def remove_from_playlist(self, playlist_id: int, track_ids: list[int]) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.executemany("DELETE FROM playlist_items WHERE playlist_id=? AND track_id=?",
                             [(playlist_id, tid) for tid in track_ids])
            conn.commit()

    def set_playlist_order(self, playlist_id: int, track_ids: list[int]) -> None:
        with self._write_lock:
            conn = self.connect()
            conn.executemany("UPDATE playlist_items SET position=? "
                             "WHERE playlist_id=? AND track_id=?",
                             [(i, playlist_id, tid) for i, tid in enumerate(track_ids)])
            conn.commit()


# ---------------------------------------------------------------------------
# Icon
#
# The mark is a deep-navy rounded tile carrying five rounded level bars in a
# symmetric peak, lit from a vertical blue gradient, sitting on a bright
# baseline. It stays readable down to 16 px, which is what a taskbar needs.
#
# The SVG below is the source of truth; the rasteriser under it redraws the
# same geometry so the program can emit its own .ico with no image library.
# ---------------------------------------------------------------------------

ICON_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256" role="img" aria-label="Cadence">
  <defs>
    <linearGradient id="tile" x1="0" y1="0" x2="0.6" y2="1">
      <stop offset="0" stop-color="#13395f"/>
      <stop offset="1" stop-color="#071a2e"/>
    </linearGradient>
    <linearGradient id="bar" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#6cc0ff"/>
      <stop offset="1" stop-color="#1668c4"/>
    </linearGradient>
  </defs>
  <rect width="256" height="256" rx="56" fill="url(#tile)"/>
  <rect x="10" y="10" width="236" height="236" rx="48" fill="none" stroke="#2f6fae" stroke-opacity="0.35" stroke-width="2"/>
  <g fill="url(#bar)">
    <rect x="36"  y="132" width="26" height="68"  rx="13"/>
    <rect x="76"  y="96"  width="26" height="104" rx="13"/>
    <rect x="115" y="56"  width="26" height="144" rx="13"/>
    <rect x="154" y="96"  width="26" height="104" rx="13"/>
    <rect x="194" y="132" width="26" height="68"  rx="13"/>
  </g>
  <rect x="32" y="208" width="192" height="10" rx="5" fill="#7fc9ff"/>
</svg>"""

# (x, y, w, h) in a 256x256 space - kept in step with the SVG above.
_ICON_BARS = [
    (36.0, 132.0, 26.0, 68.0),
    (76.0, 96.0, 26.0, 104.0),
    (115.0, 56.0, 26.0, 144.0),
    (154.0, 96.0, 26.0, 104.0),
    (194.0, 132.0, 26.0, 68.0),
]
_ICON_BASELINE = (32.0, 208.0, 192.0, 10.0)

# At 16-24 px the five bars collapse into mush, so small sizes get a simplified
# three-bar cut of the same mark. Icon sets do this routinely.
_ICON_BARS_SMALL = [
    (42.0, 126.0, 40.0, 74.0),
    (108.0, 62.0, 40.0, 138.0),
    (174.0, 126.0, 40.0, 74.0),
]
_ICON_BASELINE_SMALL = (34.0, 208.0, 188.0, 14.0)


def _rounded_rect_coverage(px: float, py: float, x: float, y: float,
                           w: float, h: float, radius: float, feather: float) -> float:
    """Anti-aliased coverage of a rounded rectangle at a point, via its distance field."""
    cx, cy = x + w / 2.0, y + h / 2.0
    hx, hy = w / 2.0, h / 2.0
    radius = min(radius, hx, hy)
    qx = abs(px - cx) - (hx - radius)
    qy = abs(py - cy) - (hy - radius)
    outside = ((max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2) ** 0.5)
    distance = outside + min(max(qx, qy), 0.0) - radius
    return max(0.0, min(1.0, 0.5 - distance / feather))


def _blend(dst: list[float], src: tuple[float, float, float], alpha: float) -> None:
    dst[0] = src[0] * alpha + dst[0] * (1 - alpha)
    dst[1] = src[1] * alpha + dst[1] * (1 - alpha)
    dst[2] = src[2] * alpha + dst[2] * (1 - alpha)
    dst[3] = alpha + dst[3] * (1 - alpha)


def render_icon(size: int) -> bytearray:
    """Draw the mark at `size` px and return straight RGBA bytes."""
    scale = size / 256.0
    feather = 1.0 / scale  # one device pixel, expressed in icon units
    pixels = bytearray(size * size * 4)
    for row in range(size):
        for col in range(size):
            px = (col + 0.5) / scale
            py = (row + 0.5) / scale
            pixel = [0.0, 0.0, 0.0, 0.0]

            tile = _rounded_rect_coverage(px, py, 0, 0, 256, 256, 56, feather)
            if tile > 0.0:
                t = min(1.0, (px * 0.6 + py) / 256.0)
                _blend(pixel, (0x13 + (0x07 - 0x13) * t,
                               0x39 + (0x1a - 0x39) * t,
                               0x5f + (0x2e - 0x5f) * t), tile)

                if size >= 32:  # the hairline ring only helps at larger sizes
                    ring = (_rounded_rect_coverage(px, py, 10, 10, 236, 236, 48, feather)
                            - _rounded_rect_coverage(px, py, 12, 12, 232, 232, 46, feather))
                    if ring > 0.01:
                        _blend(pixel, (0x2f, 0x6f, 0xae), min(1.0, ring) * 0.35)

                for (bx, by, bw, bh) in (_ICON_BARS_SMALL if size <= 24 else _ICON_BARS):
                    if not (bx - feather <= px <= bx + bw + feather
                            and by - feather <= py <= by + bh + feather):
                        continue
                    cover = _rounded_rect_coverage(px, py, bx, by, bw, bh, bw / 2.0, feather)
                    if cover > 0.0:
                        t = min(1.0, max(0.0, (py - 56.0) / 144.0))
                        _blend(pixel, (0x6c + (0x16 - 0x6c) * t,
                                       0xc0 + (0x68 - 0xc0) * t,
                                       0xff + (0xc4 - 0xff) * t), cover)

                bx, by, bw, bh = _ICON_BASELINE_SMALL if size <= 24 else _ICON_BASELINE
                if (bx - feather <= px <= bx + bw + feather
                        and by - feather <= py <= by + bh + feather):
                    cover = _rounded_rect_coverage(px, py, bx, by, bw, bh, bh / 2.0, feather)
                    if cover > 0.0:
                        _blend(pixel, (0x7f, 0xc9, 0xff), cover)

            offset = (row * size + col) * 4
            pixels[offset] = int(max(0.0, min(255.0, pixel[0])))
            pixels[offset + 1] = int(max(0.0, min(255.0, pixel[1])))
            pixels[offset + 2] = int(max(0.0, min(255.0, pixel[2])))
            pixels[offset + 3] = int(max(0.0, min(255.0, pixel[3] * 255.0)))
    return pixels


def png_bytes(size: int, rgba: bytearray) -> bytes:
    import zlib
    raw = bytearray()
    stride = size * 4
    for row in range(size):
        raw.append(0)  # filter type 0
        raw += rgba[row * stride:(row + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def bmp_bytes(size: int, rgba: bytearray) -> bytes:
    """A DIB entry for an .ico: BGRA bottom-up, plus the AND mask Windows still expects."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4,
                         0, 0, 0, 0)
    body = bytearray()
    for row in range(size - 1, -1, -1):
        base = row * size * 4
        for col in range(size):
            offset = base + col * 4
            body += bytes((rgba[offset + 2], rgba[offset + 1], rgba[offset], rgba[offset + 3]))
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)  # all zero: every pixel opaque per the alpha channel
    return header + bytes(body) + bytes(mask)


# 16 and 32 are the classic list and desktop sizes; 20, 40 and 96 are what the
# shell asks for at 125%, 150% and 200% scaling; 48, 64, 128 and 256 cover the
# larger icon views. 256 is the ceiling: an ICO directory entry stores width in
# a single byte, where 0 means 256, so nothing above that is representable.
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
ICO_PNG_FROM = 64


def bundled_path(name: str) -> str | None:
    """Locate a file PyInstaller packed into the executable, if there is one."""
    base = getattr(sys, "_MEIPASS", None)
    if base:
        candidate = os.path.join(base, name)
        if os.path.isfile(candidate):
            return candidate
    beside = os.path.join(os.path.dirname(running_exe()), name)
    return beside if os.path.isfile(beside) else None


_ICON_BYTES: dict[str, bytes] = {}


def app_ico() -> bytes:
    """The .ico the window and taskbar use: the packed copy, else drawn here."""
    if "ico" not in _ICON_BYTES:
        packed = bundled_path("Cadence.ico")
        if packed:
            with open(packed, "rb") as handle:
                _ICON_BYTES["ico"] = handle.read()
        else:
            _ICON_BYTES["ico"] = ico_bytes()
    return _ICON_BYTES["ico"]


def app_png(size: int = 256) -> bytes:
    key = f"png{size}"
    if key not in _ICON_BYTES:
        _ICON_BYTES[key] = png_bytes(size, render_icon(size))
    return _ICON_BYTES[key]


def ico_bytes(sizes: tuple[int, ...] = ICO_SIZES) -> bytes:
    images: list[tuple[int, bytes]] = []
    for size in sizes:
        rgba = render_icon(size)
        # Small entries stay as DIBs for maximum compatibility with older
        # shells; the big ones use PNG so the file does not balloon.
        images.append((size, png_bytes(size, rgba) if size >= ICO_PNG_FROM
                       else bmp_bytes(size, rgba)))

    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, blob in images:
        directory += struct.pack("<BBBBHHII", size & 0xFF, size & 0xFF, 0, 0, 1, 32,
                                 len(blob), offset)
        offset += len(blob)
    return bytes(directory) + b"".join(blob for _size, blob in images)


def build_ico(path: str, sizes: tuple[int, ...] = ICO_SIZES) -> str:
    with open(path, "wb") as handle:
        handle.write(ico_bytes(sizes))
    return path


def build_png(path: str, size: int = 1024) -> str:
    with open(path, "wb") as fh:
        fh.write(png_bytes(size, render_icon(size)))
    return path




# ---------------------------------------------------------------------------
# Updates
#
# Checks the project's GitHub releases for a newer version. Nothing is ever
# downloaded or replaced without the user pressing the button: the check is a
# single read-only request, cached for a day so a restart is not a new call.
# ---------------------------------------------------------------------------

UPDATE_REPO = "newmangarry323-sketch/Ai-Slop-that-Claude-Makes"
UPDATE_API = f"https://api.github.com/repos/{UPDATE_REPO}/releases/latest"
UPDATE_CACHE_SECONDS = 24 * 60 * 60
TAG_PREFIX = "cadence-v"


def version_tuple(text: str) -> tuple:
    return tuple(int(part) for part in re.findall(r"\d+", str(text))[:4]) or (0,)


def is_frozen() -> bool:
    return bool(getattr(sys, "frozen", False))


def running_exe() -> str:
    return os.path.abspath(sys.executable if is_frozen() else sys.argv[0])


def check_for_update(library: "Library", use_cache: bool = True) -> dict:
    cached = library.get_setting("update_cache")
    if use_cache and cached:
        try:
            payload = json.loads(cached)
            if time.time() - payload.get("at", 0) < UPDATE_CACHE_SECONDS:
                return payload["result"]
        except Exception:
            pass

    import urllib.error
    request = urllib.request.Request(
        UPDATE_API,
        headers={"Accept": "application/vnd.github+json",
                 "User-Agent": f"{APP_NAME}/{APP_VERSION}"})
    try:
        with urllib.request.urlopen(request, timeout=12) as response:
            release = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return {"error": f"GitHub returned {exc.code}"}
    except Exception as exc:
        return {"error": f"Could not reach GitHub: {exc}"}

    tag = str(release.get("tag_name", ""))
    latest = tag[len(TAG_PREFIX):] if tag.startswith(TAG_PREFIX) else tag.lstrip("v")
    asset = next((a for a in release.get("assets", [])
                  if a.get("name", "").lower().endswith(".exe")), None)
    result = {
        "latest": latest,
        "current": APP_VERSION,
        "available": bool(latest) and version_tuple(latest) > version_tuple(APP_VERSION),
        "url": release.get("html_url", ""),
        "notes": (release.get("body") or "").strip(),
        "asset": asset.get("browser_download_url") if asset else "",
        "asset_size": asset.get("size", 0) if asset else 0,
        "frozen": is_frozen(),
    }
    library.set_setting("update_cache", json.dumps({"at": time.time(), "result": result}))
    return result


def install_update(library: "Library") -> dict:
    """Download the new executable and hand over to a helper that swaps them.

    The running executable cannot overwrite itself on Windows, but it *can* be
    renamed, so the helper renames the current one aside as a backup and moves
    the download into its place. A failure at any point leaves both files on
    disk rather than a half-written program.
    """
    if not is_frozen():
        return {"error": "Running from source - update with git instead"}
    if sys.platform != "win32":
        return {"error": "In-place update is only wired up for the Windows build"}

    info = check_for_update(library, use_cache=False)
    if info.get("error"):
        return info
    if not info.get("available") or not info.get("asset"):
        return {"error": "No newer build to install"}

    current = running_exe()
    folder = os.path.dirname(current)
    download = os.path.join(folder, f"{APP_NAME}-{info['latest']}.download")
    backup = os.path.join(folder, f"{APP_NAME}-previous.exe")

    request = urllib.request.Request(
        info["asset"], headers={"User-Agent": f"{APP_NAME}/{APP_VERSION}"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response, \
                open(download, "wb") as handle:
            shutil.copyfileobj(response, handle, 1024 * 256)
    except Exception as exc:
        if os.path.exists(download):
            os.remove(download)
        return {"error": f"Download failed: {exc}"}

    size = os.path.getsize(download)
    if size < 1_000_000:                      # a real build is far larger than this
        os.remove(download)
        return {"error": "Downloaded file looks wrong, leaving the current version alone"}

    helper = os.path.join(folder, f"{APP_NAME}-update.cmd")
    with open(helper, "w", encoding="ascii") as handle:
        handle.write(
            "@echo off\r\n"
            "setlocal\r\n"
            ":wait\r\n"
            "timeout /t 1 /nobreak >nul\r\n"
            f'tasklist /fi "PID eq {os.getpid()}" | find "{os.getpid()}" >nul && goto wait\r\n'
            f'if exist "{backup}" del "{backup}"\r\n'
            f'move /y "{current}" "{backup}" >nul || exit /b 1\r\n'
            f'move /y "{download}" "{current}" >nul || (move /y "{backup}" "{current}" >nul & exit /b 1)\r\n'
            f'start "" "{current}"\r\n'
            'del "%~f0"\r\n')
    subprocess.Popen(["cmd", "/c", helper], cwd=folder, close_fds=True,
                     **_no_window_flags())
    return {"ok": True, "size_mb": round(size / 1024 / 1024, 1), "version": info["latest"]}


# ---------------------------------------------------------------------------
# HTTP server
#
# Bound to the loopback interface only. Requests carry a per-run token so that
# a random page in the browser cannot enumerate the music library behind your
# back, and the Host header is checked to blunt DNS rebinding.
# ---------------------------------------------------------------------------

# A window that stops checking in is treated as gone.
#
# The long grace is deliberately longer than a minute: a browser throttles timers
# in a hidden window down to roughly one a minute once it has been minimised for
# a while, so a shorter grace would shut the server out from under a window that
# is still open. It only governs the case where the window vanished without
# saying so - a crash, or the browser being killed. A window that closes
# normally sends word on its way out and gets the short grace, so the usual
# shutdown is still a few seconds.
HEARTBEAT_GRACE = 90.0
CLOSING_GRACE = 3.0


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, handler, library: Library, token: str):
        super().__init__(address, handler)
        self.library = library
        self.token = token
        self.started = time.time()
        self.clients: dict[str, float] = {}   # window id -> time it stops counting as alive
        self.seen_client = False
        self.browser: subprocess.Popen | None = None


class Handler(BaseHTTPRequestHandler):
    server_version = f"{APP_NAME}/{APP_VERSION}"
    protocol_version = "HTTP/1.1"

    # -- plumbing -----------------------------------------------------------

    def log_message(self, fmt: str, *args) -> None:  # quieter than the default
        pass

    @property
    def library(self) -> Library:
        return self.server.library

    def _host_ok(self) -> bool:
        host = (self.headers.get("Host") or "").split(":")[0].strip("[]").lower()
        return host in ("127.0.0.1", "localhost", "::1", "")

    def _authorised(self, query: dict) -> bool:
        supplied = self.headers.get("X-Cadence-Token") or (query.get("t", [""])[0])
        return secrets.compare_digest(str(supplied), self.server.token)

    def _send(self, status: int, body: bytes, content_type: str,
              extra: dict | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, payload, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send(status, body, "application/json; charset=utf-8",
                   {"Cache-Control": "no-store"})

    def _error(self, status: int, message: str) -> None:
        self._json({"error": message}, status)

    def _body(self) -> dict:
        try:
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > 32 * 1024 * 1024:
                return {}
            return json.loads(self.rfile.read(length).decode("utf-8")) or {}
        except Exception:
            return {}

    # -- routing ------------------------------------------------------------

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        if not self._host_ok():
            return self._error(403, "bad host")
        parsed = urllib.parse.urlparse(self.path)
        route = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if route in ("/", "/index.html"):
            body = render_page(self.server.token).encode("utf-8")
            return self._send(200, body, "text/html; charset=utf-8",
                              {"Cache-Control": "no-store"})
        if route == "/icon.svg":
            return self._send(200, ICON_SVG.encode("utf-8"), "image/svg+xml",
                              {"Cache-Control": "max-age=86400"})
        if route == "/favicon.ico":
            return self._send(200, app_ico(), "image/x-icon",
                              {"Cache-Control": "max-age=86400"})
        if route == "/icon.png":
            try:
                size = max(16, min(512, int(query.get("size", ["256"])[0])))
            except ValueError:
                size = 256
            return self._send(200, app_png(size), "image/png",
                              {"Cache-Control": "max-age=86400"})

        if not route.startswith("/api/"):
            return self._error(404, "not found")
        if not self._authorised(query):
            return self._error(403, "bad or missing token")

        if route == "/api/state":
            return self._json(self.state_payload())
        if route == "/api/tracks":
            return self._json({"tracks": self.library.tracks()})
        if route == "/api/scan/status":
            state = dict(self.library.scan_state)
            state["stats"] = self.library.stats()
            return self._json(state)
        if route == "/api/playlists":
            playlists = self.library.playlists()
            for playlist in playlists:
                playlist["tracks"] = self.library.playlist_tracks(playlist["id"])
            return self._json({"playlists": playlists})
        if route == "/api/update/check":
            cached = query.get("cached", ["0"])[0] in ("1", "true")
            return self._json(check_for_update(self.library, use_cache=cached))
        if route == "/api/browse":
            return self._json(self.browse(query.get("path", [""])[0]))
        if route == "/api/art":
            return self.serve_art(query)
        if route == "/api/stream":
            return self.serve_stream(query)
        if route == "/api/export":
            return self.serve_export(query)
        return self._error(404, "not found")

    def do_POST(self) -> None:
        if not self._host_ok():
            return self._error(403, "bad host")
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        if not self._authorised(query):
            return self._error(403, "bad or missing token")
        route, payload = parsed.path, self._body()
        library = self.library

        if route == "/api/folder":
            folder = os.path.abspath(os.path.expanduser(str(payload.get("path", "")).strip()))
            if not os.path.isdir(folder):
                return self._error(400, f"Not a folder: {folder}")
            library.set_setting("music_folder", folder)
            library.scan_async(folder, full=bool(payload.get("full")))
            return self._json(self.state_payload())
        if route == "/api/scan":
            folder = library.get_setting("music_folder")
            if not folder:
                return self._error(400, "No music folder is designated yet")
            library.scan_async(folder, full=bool(payload.get("full")))
            return self._json({"started": True})
        if route == "/api/played":
            library.mark_played(int(payload.get("id", 0)))
            return self._json({"ok": True})
        if route == "/api/rating":
            library.set_rating(int(payload.get("id", 0)), int(payload.get("rating", 0)))
            return self._json({"ok": True})
        if route == "/api/setting":
            key = str(payload.get("key", ""))
            allowed = {"theme", "accent", "sidebar_width", "panel_height", "volume",
                       "repeat", "shuffle", "columns", "view", "eq"}
            if key not in allowed and not key.startswith("pref_"):
                return self._error(400, "unknown setting")
            library.set_setting(key, str(payload.get("value", "")))
            return self._json({"ok": True})

        if route == "/api/playlist/create":
            name = _clean(payload.get("name")) or "New Playlist"
            try:
                playlist_id = library.create_playlist(name)
            except sqlite3.IntegrityError:
                return self._error(409, f"A playlist named '{name}' already exists")
            ids = [int(i) for i in payload.get("tracks", [])]
            if ids:
                library.add_to_playlist(playlist_id, ids)
            return self._json({"id": playlist_id})
        if route == "/api/playlist/rename":
            library.rename_playlist(int(payload["id"]), _clean(payload.get("name")) or "Playlist")
            return self._json({"ok": True})
        if route == "/api/playlist/delete":
            library.delete_playlist(int(payload["id"]))
            return self._json({"ok": True})
        if route == "/api/playlist/add":
            count = library.add_to_playlist(int(payload["id"]),
                                            [int(i) for i in payload.get("tracks", [])])
            return self._json({"added": count})
        if route == "/api/playlist/remove":
            library.remove_from_playlist(int(payload["id"]),
                                         [int(i) for i in payload.get("tracks", [])])
            return self._json({"ok": True})
        if route == "/api/playlist/order":
            library.set_playlist_order(int(payload["id"]),
                                       [int(i) for i in payload.get("tracks", [])])
            return self._json({"ok": True})
        if route == "/api/update/install":
            result = install_update(library)
            if result.get("error"):
                return self._error(400, result["error"])
            threading.Thread(target=self._shutdown, daemon=True).start()
            return self._json(result)
        if route == "/api/reveal":
            return self._json(self.reveal(int(payload.get("id", 0))))
        if route == "/api/heartbeat":
            client = str(payload.get("id", ""))[:64]
            if client:
                grace = CLOSING_GRACE if payload.get("closing") else HEARTBEAT_GRACE
                self.server.clients[client] = time.time() + grace
                self.server.seen_client = True
            return self._json({"ok": True})
        if route == "/api/quit":
            threading.Thread(target=self._shutdown, daemon=True).start()
            return self._json({"ok": True})
        return self._error(404, "not found")

    def _shutdown(self) -> None:
        time.sleep(0.3)
        close_window(self.server)
        self.server.shutdown()

    # -- handlers -----------------------------------------------------------

    def state_payload(self) -> dict:
        library = self.library
        folder = library.get_setting("music_folder")
        settings = {row["key"]: row["value"] for row in
                    library.connect().execute("SELECT key, value FROM settings")}
        return {
            "app": APP_NAME, "version": APP_VERSION,
            "folder": folder, "folder_exists": bool(folder) and os.path.isdir(folder),
            "stats": library.stats(), "settings": settings,
            "scanning": library.scan_state["running"],
            "platform": sys.platform,
            "home": os.path.expanduser("~"),
            "db": library.db_path,
        }

    def browse(self, path: str) -> dict:
        """Directory listing used by the in-app folder picker."""
        if not path:
            roots = []
            if sys.platform == "win32":
                for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
                    drive = f"{letter}:\\"
                    if os.path.isdir(drive):
                        roots.append({"name": drive, "path": drive})
            else:
                roots.append({"name": "/", "path": "/"})
            home = os.path.expanduser("~")
            entries = [{"name": os.path.basename(home) or home, "path": home}]
            for child in ("Music", "Downloads", "Desktop", "Documents"):
                candidate = os.path.join(home, child)
                if os.path.isdir(candidate):
                    entries.append({"name": child, "path": candidate})
            return {"path": "", "parent": None, "places": entries + roots, "dirs": [], "audio": 0}

        path = os.path.abspath(os.path.expanduser(path))
        if not os.path.isdir(path):
            return {"error": f"Not a folder: {path}", "path": path, "dirs": []}
        dirs, audio = [], 0
        try:
            with os.scandir(path) as scan:
                for entry in scan:
                    try:
                        if entry.is_dir(follow_symlinks=False):
                            if entry.name in SKIP_DIRS or entry.name.startswith("."):
                                continue
                            dirs.append({"name": entry.name, "path": entry.path})
                        elif os.path.splitext(entry.name)[1].lower() in AUDIO_EXTS:
                            audio += 1
                    except OSError:
                        continue
        except PermissionError:
            return {"error": f"Permission denied: {path}", "path": path, "dirs": []}
        dirs.sort(key=lambda d: d["name"].lower())
        parent = os.path.dirname(path.rstrip("\\/")) or None
        if parent == path:
            parent = None
        return {"path": path, "parent": parent, "dirs": dirs[:2000], "audio": audio}

    def reveal(self, track_id: int) -> dict:
        track = self.library.track(track_id)
        if not track:
            return {"error": "unknown track"}
        folder = os.path.dirname(track["path"])
        try:
            if sys.platform == "win32":
                subprocess.Popen(["explorer", "/select,", os.path.normpath(track["path"])],
                                 **_no_window_flags())
            elif sys.platform == "darwin":
                subprocess.Popen(["open", "-R", track["path"]])
            else:
                subprocess.Popen(["xdg-open", folder])
            return {"ok": True, "folder": folder}
        except Exception as exc:
            return {"error": str(exc), "folder": folder}

    def serve_art(self, query: dict) -> None:
        try:
            track_id = int(query.get("id", ["0"])[0])
        except ValueError:
            return self._error(400, "bad id")
        art = self.library.art(track_id)
        if not art:
            return self._send(404, b"", "text/plain")
        mime, blob = art
        self._send(200, blob, mime, {"Cache-Control": "max-age=604800, immutable"})

    def serve_export(self, query: dict) -> None:
        """Write out an .m3u8 playlist the rest of the world can read."""
        ids = [int(i) for i in query.get("ids", [""])[0].split(",") if i.strip().isdigit()]
        name = query.get("name", ["playlist"])[0]
        lines = ["#EXTM3U"]
        for track_id in ids:
            track = self.library.track(track_id)
            if not track:
                continue
            lines.append(f"#EXTINF:{int(track['duration'] or 0)},"
                         f"{track['artist']} - {track['title']}")
            lines.append(track["path"])
        body = ("\n".join(lines) + "\n").encode("utf-8")
        safe = re.sub(r"[^A-Za-z0-9._-]+", "_", name) or "playlist"
        self._send(200, body, "audio/x-mpegurl",
                   {"Content-Disposition": f'attachment; filename="{safe}.m3u8"'})

    def serve_stream(self, query: dict) -> None:
        """Serve the audio file itself, honouring Range so seeking works."""
        try:
            track_id = int(query.get("id", ["0"])[0])
        except ValueError:
            return self._error(400, "bad id")
        track = self.library.track(track_id)
        if not track:
            return self._error(404, "unknown track")
        path = track["path"]
        if not os.path.isfile(path):
            return self._error(410, "file has moved or been deleted")

        size = os.path.getsize(path)
        mime = mimetypes.guess_type(path)[0] or "application/octet-stream"
        if path.lower().endswith((".flac", ".opus", ".ogg", ".oga", ".m4a", ".m4b", ".wav")):
            mime = {
                ".flac": "audio/flac", ".opus": "audio/ogg", ".ogg": "audio/ogg",
                ".oga": "audio/ogg", ".m4a": "audio/mp4", ".m4b": "audio/mp4",
                ".wav": "audio/wav",
            }[os.path.splitext(path.lower())[1]]

        start, end = 0, size - 1
        status = 200
        header = self.headers.get("Range")
        if header:
            match = re.fullmatch(r"bytes=(\d*)-(\d*)", header.strip())
            if match:
                first, last = match.group(1), match.group(2)
                if first:
                    start = int(first)
                    if last:
                        end = min(int(last), size - 1)
                elif last:  # suffix range: the final N bytes
                    start = max(0, size - int(last))
                if start >= size or start > end:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                status = 206

        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command == "HEAD":
            return
        try:
            with open(path, "rb") as fh:
                fh.seek(start)
                remaining = length
                while remaining > 0:
                    block = fh.read(min(256 * 1024, remaining))
                    if not block:
                        break
                    self.wfile.write(block)
                    remaining -= len(block)
        except (BrokenPipeError, ConnectionResetError):
            pass  # the player seeked away or closed the tab


# ---------------------------------------------------------------------------
# Interface
#
# The layout borrows VS Code's shell: title bar with menus, activity bar,
# side bar, editor tabs over a track list, a toggleable panel, and a status
# bar in the accent colour. Colours come from one dark-blue token ramp so a
# single variable change re-tints the whole app.
# ---------------------------------------------------------------------------

UI_STYLE = r"""<style>
:root {
  --a900:#05213d; --a800:#072c52; --a700:#0a3a6b; --a600:#0d4c8b;
  --a500:#1060ad; --a400:#2a7fd4; --a300:#57a6ee;
  --bg-editor:#1e1e1e; --bg-side:#252526; --bg-activity:#333333;
  --bg-title:#3c3c3c; --bg-panel:#181818; --bg-input:#3c3c3c;
  --bg-hover:#2a2d2e; --bg-widget:#252526; --bg-drop:#1f1f1f;
  --fg:#cccccc; --fg-muted:#8b8b8b; --fg-strong:#ffffff; --fg-faint:#6a6a6a;
  --border:#3c3c3c; --border-soft:#2b2b2b; --shadow:rgba(0,0,0,.45);
  --sel:var(--a800); --sel-inactive:#2f3336;
  --ok:#89d185; --warn:#cca700; --err:#f14c4c;
  --mono:"Cascadia Code","JetBrains Mono",Consolas,"Courier New",monospace;
  --ui:"Segoe UI",system-ui,-apple-system,"Ubuntu","Droid Sans",sans-serif;
  --row-h:24px;
}
/* Solarized, by Ethan Schoonover: base03..base3 for surfaces, base01/base0 for text. */
html[data-theme="solarized-dark"] {
  --bg-editor:#002b36; --bg-side:#073642; --bg-activity:#00252e;
  --bg-title:#073642; --bg-panel:#00252e; --bg-input:#073642;
  --bg-hover:#0a4250; --bg-widget:#073642; --bg-drop:#002b36;
  --fg:#93a1a1; --fg-muted:#839496; --fg-strong:#fdf6e3; --fg-faint:#586e75;
  --border:#0a4b5c; --border-soft:#073642; --shadow:rgba(0,20,26,.6);
  --sel:#0a4250; --sel-inactive:#073642;
  --ok:#859900; --warn:#b58900; --err:#dc322f;
}
html[data-theme="solarized-light"] {
  --bg-editor:#fdf6e3; --bg-side:#eee8d5; --bg-activity:#e4ddc8;
  --bg-title:#eee8d5; --bg-panel:#f5efdc; --bg-input:#fdf6e3;
  --bg-hover:#e4ddc8; --bg-widget:#eee8d5; --bg-drop:#fdf6e3;
  --fg:#586e75; --fg-muted:#657b83; --fg-strong:#002b36; --fg-faint:#93a1a1;
  --border:#d9d2bd; --border-soft:#e4ddc8; --shadow:rgba(88,110,117,.22);
  --sel:#dbe6ea; --sel-inactive:#e4ddc8;
  --ok:#859900; --warn:#b58900; --err:#dc322f;
}
/* Nord, by Arctic Ice Studio: polar night for surfaces, snow storm for text. */
html[data-theme="nord"] {
  --bg-editor:#2e3440; --bg-side:#3b4252; --bg-activity:#272c36;
  --bg-title:#3b4252; --bg-panel:#272c36; --bg-input:#434c5e;
  --bg-hover:#434c5e; --bg-widget:#3b4252; --bg-drop:#2e3440;
  --fg:#d8dee9; --fg-muted:#a9b1c1; --fg-strong:#eceff4; --fg-faint:#7b8494;
  --border:#4c566a; --border-soft:#3b4252; --shadow:rgba(20,24,32,.55);
  --sel:#434c5e; --sel-inactive:#3b4252;
  --ok:#a3be8c; --warn:#ebcb8b; --err:#bf616a;
}
/* Gruvbox dark, by Pavel Pertsev: warm greys against soft retro tones. */
html[data-theme="gruvbox"] {
  --bg-editor:#282828; --bg-side:#32302f; --bg-activity:#1d2021;
  --bg-title:#32302f; --bg-panel:#1d2021; --bg-input:#3c3836;
  --bg-hover:#3c3836; --bg-widget:#32302f; --bg-drop:#282828;
  --fg:#ebdbb2; --fg-muted:#bdae93; --fg-strong:#fbf1c7; --fg-faint:#928374;
  --border:#504945; --border-soft:#3c3836; --shadow:rgba(0,0,0,.5);
  --sel:#504945; --sel-inactive:#3c3836;
  --ok:#b8bb26; --warn:#fabd2f; --err:#fb4934;
}
/* Higher contrast than Dark, for when the default is too soft. */
html[data-theme="black"] {
  --bg-editor:#000000; --bg-side:#0a0a0a; --bg-activity:#0a0a0a;
  --bg-title:#141414; --bg-panel:#000000; --bg-input:#1c1c1c;
  --bg-hover:#1f1f1f; --bg-widget:#121212; --bg-drop:#000000;
  --fg:#e6e6e6; --fg-muted:#a0a0a0; --fg-strong:#ffffff; --fg-faint:#6e6e6e;
  --border:#3a3a3a; --border-soft:#242424; --shadow:rgba(0,0,0,.8);
  --sel:#1f3a56; --sel-inactive:#232323;
  --ok:#7ee787; --warn:#e3b341; --err:#ff7b72;
}
/* Paper: warm off-white, easier than pure white under a lamp. */
html[data-theme="paper"] {
  --bg-editor:#faf7f2; --bg-side:#f2ede4; --bg-activity:#e8e2d6;
  --bg-title:#ebe5da; --bg-panel:#f5f1e9; --bg-input:#ffffff;
  --bg-hover:#e8e2d6; --bg-widget:#f2ede4; --bg-drop:#faf7f2;
  --fg:#43403a; --fg-muted:#6b665e; --fg-strong:#1f1d1a; --fg-faint:#9a948a;
  --border:#d8d0c2; --border-soft:#e6e0d4; --shadow:rgba(90,80,60,.2);
  --sel:#dfe7f0; --sel-inactive:#e6e0d4;
  --ok:#4a7c2f; --warn:#96700a; --err:#b3261e;
}
html[data-theme="light"] {
  --bg-editor:#ffffff; --bg-side:#f3f3f3; --bg-activity:#e8e8e8;
  --bg-title:#dddddd; --bg-panel:#f8f8f8; --bg-input:#ffffff;
  --bg-hover:#e8e8e8; --bg-widget:#f3f3f3; --bg-drop:#ffffff;
  --fg:#3b3b3b; --fg-muted:#616161; --fg-strong:#000000; --fg-faint:#8a8a8a;
  --border:#cecece; --border-soft:#e5e5e5; --shadow:rgba(0,0,0,.18);
  --sel:#cfe3f7; --sel-inactive:#e4e6f1;
  --ok:#28792c; --warn:#976c00; --err:#e51400;
}
*{box-sizing:border-box;margin:0;padding:0}
[hidden]{display:none!important}
html,body{height:100%;overflow:hidden}
body{
  background:var(--bg-editor); color:var(--fg); font-family:var(--ui);
  font-size:13px; line-height:1.4; user-select:none; -webkit-font-smoothing:antialiased;
}
button{font:inherit;color:inherit;background:none;border:none;cursor:pointer}
input,select{font:inherit}
::-webkit-scrollbar{width:14px;height:14px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:rgba(121,121,121,.4);border:3px solid transparent;background-clip:content-box}
::-webkit-scrollbar-thumb:hover{background:rgba(121,121,121,.7);border:3px solid transparent;background-clip:content-box}
.codicon{width:16px;height:16px;flex:0 0 auto;fill:none;stroke:currentColor;stroke-width:1.4;
  stroke-linecap:round;stroke-linejoin:round}

/* ---------- shell ---------- */
#shell{display:grid;height:100vh;
  grid-template-columns:48px auto 1fr;
  grid-template-rows:35px 1fr 22px;
  grid-template-areas:"title title title" "activity side editor" "status status status";}
#titlebar{grid-area:title;background:var(--bg-title);display:flex;align-items:center;
  gap:2px;padding:0 8px;border-bottom:1px solid var(--border-soft);-webkit-app-region:drag}
#titlebar img{width:17px;height:17px;margin:0 8px 0 4px}
#wincontrols{display:flex;align-items:center;gap:8px;padding:0 8px 0 2px;-webkit-app-region:no-drag}
.win{width:12px;height:12px;border-radius:50%;flex:0 0 auto;position:relative;
  padding:0;border:1px solid rgba(0,0,0,.2)}
.win.close{background:#ff5f57}
.win.min{background:#febc2e}
.win.max{background:#28c840}
.win[disabled]{background:#6b6b6b;border-color:transparent;cursor:default}
/* Drawn as geometry rather than text: a glyph is centred by its line box, not
   by its ink, so x and a dash land at different heights - and the font may not
   carry the arrow at all. Shapes centre exactly and look the same everywhere. */
.win::before,.win::after{content:"";position:absolute;left:50%;top:50%;
  background:rgba(0,0,0,.62);border-radius:1px;opacity:0;transition:opacity .1s;
  box-sizing:border-box}
#wincontrols:hover .win:not([disabled])::before,
#wincontrols:hover .win:not([disabled])::after{opacity:1}
.win.min::before,.win.max::before{display:none}
.win.close::before{width:7px;height:1.5px;transform:translate(-50%,-50%) rotate(45deg)}
.win.close::after{width:7px;height:1.5px;transform:translate(-50%,-50%) rotate(-45deg)}
.win.min::after{width:7px;height:1.5px;transform:translate(-50%,-50%)}
.win.max::after{width:6.5px;height:6.5px;background:none;border-radius:1.5px;
  border:1.5px solid rgba(0,0,0,.62);transform:translate(-50%,-50%)}
.menu{position:relative}
.menu>button{padding:3px 8px;border-radius:4px;font-size:12px;color:var(--fg)}
.menu>button:hover,.menu.open>button{background:rgba(128,128,128,.22)}
.menu-pop{position:absolute;top:100%;left:0;min-width:230px;background:var(--bg-widget);
  border:1px solid var(--border);border-radius:5px;box-shadow:0 6px 18px var(--shadow);
  padding:4px;z-index:60;display:none}
.menu.open .menu-pop{display:block}
.menu-pop button{display:flex;width:100%;align-items:center;gap:10px;padding:4px 10px;
  border-radius:4px;font-size:12px;text-align:left}
.menu-pop button:hover{background:var(--a600);color:#fff}
.menu-pop .key{margin-left:auto;color:var(--fg-faint);font-size:11px}
.menu-pop hr{border:none;border-top:1px solid var(--border);margin:4px 2px}
#command-center{margin:0 auto;display:flex;align-items:center;gap:8px;width:min(460px,42vw);
  height:22px;padding:0 10px;background:var(--bg-editor);border:1px solid var(--border);
  border-radius:5px;color:var(--fg-muted);font-size:12px;cursor:pointer;-webkit-app-region:no-drag}
#command-center:hover{background:var(--bg-hover)}
#titlebar .spacer{flex:1}

/* ---------- activity bar ---------- */
#activity{grid-area:activity;background:var(--bg-activity);display:flex;flex-direction:column;
  align-items:center;border-right:1px solid var(--border-soft)}
.act{position:relative;width:48px;height:48px;display:grid;place-items:center;color:var(--fg-muted)}
.act:hover{color:var(--fg-strong)}
.act.on{color:var(--fg-strong)}
.act.on::before{content:"";position:absolute;left:0;top:0;bottom:0;width:2px;background:var(--a400)}
.act .codicon{width:22px;height:22px;stroke-width:1.3}
.act .badge{position:absolute;right:7px;bottom:7px;min-width:15px;height:15px;padding:0 3px;
  border-radius:8px;background:var(--a500);color:#fff;font-size:9px;font-weight:600;
  display:grid;place-items:center;font-family:var(--mono)}
#activity .gap{flex:1}

/* ---------- side bar ---------- */
#side{grid-area:side;background:var(--bg-side);width:270px;display:flex;flex-direction:column;
  border-right:1px solid var(--border-soft);min-width:170px;max-width:60vw;position:relative}
#side.hidden{display:none}
#side-title{display:flex;align-items:center;gap:6px;padding:0 8px 0 20px;height:35px;flex:0 0 auto;
  font-size:11px;letter-spacing:.6px;text-transform:uppercase;color:var(--fg-muted)}
#side-title .acts{margin-left:auto;display:flex;gap:2px}
#side-title .acts button{padding:3px;border-radius:4px;color:var(--fg-muted);display:grid;place-items:center}
#side-title .acts button:hover{background:var(--bg-hover);color:var(--fg-strong)}
#side-body{flex:1;min-height:0;overflow:auto;padding-bottom:12px}
#side-grip{position:absolute;right:-3px;top:0;bottom:0;width:6px;cursor:col-resize;z-index:20}
#side-grip:hover{background:var(--a500)}

.section{border-top:1px solid var(--border-soft)}
.section:first-child{border-top:none}
.section>.head{display:flex;align-items:center;gap:4px;height:22px;padding:0 8px;
  font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.4px;cursor:pointer}
.section>.head:hover{background:var(--bg-hover)}
.section>.head .chev{transition:transform .12s}
.section.collapsed>.head .chev{transform:rotate(-90deg)}
.section.collapsed>.body{display:none}
.section .count{margin-left:auto;color:var(--fg-faint);font-size:10px;font-family:var(--mono)}

.row{display:flex;align-items:center;gap:6px;height:var(--row-h);padding:0 10px 0 8px;
  cursor:pointer;white-space:nowrap;color:var(--fg);position:relative}
.row:hover{background:var(--bg-hover)}
.row.on{background:var(--sel)}
.row.on::before{content:"";position:absolute;left:0;top:0;bottom:0;width:2px;background:var(--a400)}
.row .label{overflow:hidden;text-overflow:ellipsis}
.row .sub{margin-left:auto;color:var(--fg-faint);font-size:10px;font-family:var(--mono);padding-left:8px}
.row .twist{width:12px;height:12px;flex:0 0 auto;transition:transform .12s;color:var(--fg-muted)}
.row.closed .twist{transform:rotate(-90deg)}
.row.lvl1{padding-left:20px}
.row.lvl2{padding-left:36px}
.row.lvl3{padding-left:52px}

/* ---------- editor ---------- */
#editor{grid-area:editor;display:flex;flex-direction:column;min-width:0;min-height:0;
  overflow:hidden;background:var(--bg-editor)}
#tabs{display:flex;height:35px;background:var(--bg-panel);flex:0 0 auto;overflow-x:auto;
  overflow-y:hidden;scrollbar-width:none}
#tabs::-webkit-scrollbar{height:3px}
.tab{display:flex;align-items:center;gap:8px;padding:0 10px;min-width:110px;max-width:240px;
  height:35px;background:var(--bg-panel);color:var(--fg-muted);border-right:1px solid var(--bg-editor);
  cursor:pointer;font-size:13px;position:relative;flex:0 0 auto}
.tab.on{background:var(--bg-editor);color:var(--fg-strong)}
.tab.on::before{content:"";position:absolute;top:0;left:0;right:0;height:1px;background:var(--a400)}
.tab .name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.tab .x{width:16px;height:16px;border-radius:4px;display:grid;place-items:center;opacity:0;flex:0 0 auto}
.tab:hover .x,.tab.on .x{opacity:.75}
.tab .x:hover{background:rgba(128,128,128,.3);opacity:1}
#breadcrumbs{display:flex;align-items:center;gap:4px;height:22px;padding:0 14px;flex:0 0 auto;
  font-size:12px;color:var(--fg-muted);overflow:hidden;white-space:nowrap}
#breadcrumbs .crumb{display:flex;align-items:center;gap:4px}
#breadcrumbs .crumb:not(:last-child)::after{content:"›";color:var(--fg-faint);margin:0 2px}

#list-head{display:grid;grid-template-columns:var(--cols,48px 3fr 2fr 2fr 1fr 54px 58px);
  align-items:center;height:24px;flex:0 0 auto;padding:0 12px;
  border-bottom:1px solid var(--border-soft);font-size:11px;color:var(--fg-muted);
  text-transform:uppercase;letter-spacing:.4px}
#list-head .col{display:flex;align-items:center;gap:4px;cursor:pointer;overflow:hidden}
#list-head .col:hover{color:var(--fg-strong)}
#list-head .col .arrow{opacity:0;font-size:9px}
#list-head .col.sorted .arrow{opacity:1;color:var(--a300)}
#list-wrap{flex:1;min-height:0;overflow:auto;position:relative;outline:none}
#list-sizer{position:relative;width:100%}
#list-rows{position:absolute;top:0;left:0;right:0}
.trk{display:grid;grid-template-columns:var(--cols,48px 3fr 2fr 2fr 1fr 54px 58px);
  align-items:center;height:var(--row-h);padding:0 12px;cursor:default;
  font-size:13px;white-space:nowrap;position:absolute;left:0;right:0}
.trk:hover{background:var(--bg-hover)}
.trk.sel{background:var(--sel)}
.trk.cur{color:var(--a300);font-weight:600}
.trk.cur.sel{color:#fff}
.trk .c{overflow:hidden;text-overflow:ellipsis;padding-right:12px}
.trk .c.num{font-family:var(--mono);font-size:11px;color:var(--fg-faint);text-align:right;
  padding-right:14px;display:flex;align-items:center;justify-content:flex-end;gap:5px}
.trk.cur .c.num{color:var(--a300)}
.trk .c.dur,.trk .c.year{font-family:var(--mono);font-size:11px;text-align:right;padding-right:4px}
.trk .eq{display:inline-flex;align-items:flex-end;gap:1px;height:9px}
.trk .eq i{width:2px;background:var(--a300);animation:eq .9s ease-in-out infinite}
.trk .eq i:nth-child(2){animation-delay:.25s}
.trk .eq i:nth-child(3){animation-delay:.5s}
@keyframes eq{0%,100%{height:2px}50%{height:9px}}
@media (prefers-reduced-motion:reduce){.trk .eq i{animation:none;height:6px}}

.empty{display:flex;flex-direction:column;align-items:center;justify-content:center;
  position:absolute;inset:0;overflow:auto;
  gap:14px;color:var(--fg-muted);padding:40px;text-align:center}
.empty h2{font-size:19px;font-weight:400;color:var(--fg)}
.empty p{max-width:460px;font-size:13px;line-height:1.7}
.empty code{font-family:var(--mono);background:var(--bg-input);padding:1px 5px;border-radius:3px;font-size:12px}
.btn{background:var(--a500);color:#fff;padding:5px 16px;border-radius:3px;font-size:13px}
.btn:hover{background:var(--a400)}
.btn.ghost{background:transparent;border:1px solid var(--a500);color:var(--a300)}
.btn.ghost:hover{background:var(--a900)}
.btn.quiet{background:var(--bg-input);color:var(--fg)}
.btn.quiet:hover{background:var(--bg-hover)}
.btn:focus-visible,.row:focus-visible,.act:focus-visible{outline:1px solid var(--a400);outline-offset:1px}

/* ---------- panel ---------- */
#panel{flex:0 0 auto;height:220px;display:none;flex-direction:column;background:var(--bg-panel);
  border-top:1px solid var(--border-soft);position:relative}
#panel.open{display:flex}
#panel-grip{position:absolute;top:-3px;left:0;right:0;height:6px;cursor:row-resize;z-index:20}
#panel-grip:hover{background:var(--a500)}
#panel-tabs{display:flex;align-items:center;gap:14px;height:35px;padding:0 14px;flex:0 0 auto}
#panel-tabs .pt{font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:var(--fg-muted);
  height:35px;display:flex;align-items:center;border-bottom:1px solid transparent}
#panel-tabs .pt.on{color:var(--fg-strong);border-bottom-color:var(--a400)}
#panel-tabs .close{margin-left:auto;color:var(--fg-muted)}
#panel-body{flex:1;overflow:auto;padding:0 14px 10px;font-size:12px}
#output{font-family:var(--mono);font-size:12px;white-space:pre-wrap;color:var(--fg-muted);line-height:1.6}
#output .hl{color:var(--a300)}

/* ---------- player ---------- */
#player{flex:0 0 auto;display:flex;align-items:center;gap:14px;height:62px;padding:0 14px;
  background:var(--bg-side);border-top:1px solid var(--border-soft)}
#art{width:42px;height:42px;border-radius:3px;background:var(--bg-input);flex:0 0 auto;
  object-fit:cover;display:grid;place-items:center;color:var(--fg-faint);overflow:hidden}
#art img{width:100%;height:100%;object-fit:cover;display:block}
#np{min-width:0;width:190px;flex:0 0 auto}
#np .t{font-size:12px;color:var(--fg-strong);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#np .a{font-size:11px;color:var(--fg-muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pbtn{width:30px;height:30px;border-radius:50%;display:grid;place-items:center;color:var(--fg)}
.pbtn:hover{background:var(--bg-hover);color:var(--fg-strong)}
.pbtn.big{width:34px;height:34px;background:var(--a500);color:#fff}
.pbtn.big:hover{background:var(--a400)}
.pbtn.on{color:var(--a300)}
.pbtn.on::after{content:"";position:absolute;margin-top:20px;width:3px;height:3px;border-radius:50%;
  background:var(--a300)}
.pbtn{position:relative}
#seek-wrap{flex:1;display:flex;align-items:center;gap:9px;min-width:120px}
.time{font-family:var(--mono);font-size:11px;color:var(--fg-muted);width:40px;flex:0 0 auto}
.time.r{text-align:right}
.bar{flex:1;height:4px;border-radius:2px;background:var(--bg-input);position:relative;cursor:pointer}
.bar .fill{position:absolute;inset:0 auto 0 0;border-radius:2px;background:var(--a400);width:0}
.bar .buf{position:absolute;inset:0 auto 0 0;border-radius:2px;background:rgba(128,128,128,.35);width:0}
.bar .knob{position:absolute;top:50%;width:10px;height:10px;margin:-5px 0 0 -5px;border-radius:50%;
  background:#fff;opacity:0;transition:opacity .1s}
.bar:hover .knob,.bar.dragging .knob{opacity:1}
.bar .fill{z-index:1}.bar .knob{z-index:2}
#vol{width:78px;flex:0 0 auto}

/* ---------- status bar ---------- */
#status{grid-area:status;background:var(--a600);color:#fff;display:flex;align-items:center;
  gap:0;font-size:12px;padding:0 4px;overflow:hidden;white-space:nowrap}
#status .item{display:flex;align-items:center;gap:5px;padding:0 7px;height:22px;cursor:pointer}
#status .item:hover{background:rgba(255,255,255,.14)}
#status .item.flat{cursor:default}
#status .item.flat:hover{background:none}
#status .gap{flex:1}
#status .codicon{width:13px;height:13px}
.spin{animation:spin 1.4s linear infinite;transform-origin:50% 50%}
@keyframes spin{to{transform:rotate(360deg)}}
@media (prefers-reduced-motion:reduce){.spin{animation:none}}

/* ---------- overlays ---------- */
#scrim{position:fixed;inset:0;background:rgba(0,0,0,.35);z-index:90;display:none}
#scrim.on{display:block}
.qp{position:fixed;top:0;left:50%;transform:translateX(-50%);width:min(620px,90vw);
  background:var(--bg-widget);border:1px solid var(--border);border-radius:0 0 6px 6px;
  box-shadow:0 8px 28px var(--shadow);z-index:100;display:none;overflow:hidden}
.qp.on{display:block}
.qp input{width:100%;background:var(--bg-input);color:var(--fg);border:1px solid var(--a500);
  padding:6px 9px;font-size:13px;outline:none;border-radius:3px;margin:8px}
.qp input{width:calc(100% - 16px)}
.qp .results{max-height:min(420px,55vh);overflow:auto;padding-bottom:6px}
.qp .qi{display:flex;align-items:center;gap:9px;padding:5px 12px;cursor:pointer;font-size:13px}
.qp .qi.on{background:var(--a600);color:#fff}
.qp .qi .d{color:var(--fg-faint);font-size:11px;margin-left:auto;padding-left:12px;
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:45%}
.qp .qi.on .d{color:rgba(255,255,255,.75)}
.qp .qi .m{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.qp .qi .m b{color:var(--a300);font-weight:600}
.qp .qi.on .m b{color:#fff;text-decoration:underline}
.qp .none{padding:10px 14px;color:var(--fg-muted);font-size:12px}

.modal{position:fixed;top:12vh;left:50%;transform:translateX(-50%);width:min(620px,92vw);
  background:var(--bg-widget);border:1px solid var(--border);border-radius:6px;
  box-shadow:0 10px 34px var(--shadow);z-index:100;display:none;flex-direction:column;
  max-height:74vh;overflow:hidden}
.modal.on{display:flex}
.modal h3{font-size:13px;font-weight:600;padding:12px 16px;border-bottom:1px solid var(--border)}
.modal .mbody{padding:12px 16px;overflow:auto;flex:1}
.modal .mfoot{display:flex;gap:8px;justify-content:flex-end;padding:10px 16px;
  border-top:1px solid var(--border);align-items:center}
.modal .mfoot .note{margin-right:auto;color:var(--fg-muted);font-size:11px;font-family:var(--mono);
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:58%}
.field{display:flex;flex-direction:column;gap:5px;margin-bottom:14px}
.field label{font-size:11px;text-transform:uppercase;letter-spacing:.4px;color:var(--fg-muted)}
.field input,.field select{background:var(--bg-input);color:var(--fg);border:1px solid var(--border);
  padding:5px 8px;border-radius:3px;outline:none}
.field input:focus,.field select:focus{border-color:var(--a400)}
.field .hint{font-size:11px;color:var(--fg-faint)}
.crumbbar{display:flex;align-items:center;gap:3px;flex-wrap:wrap;font-family:var(--mono);font-size:11px;
  padding:5px 7px;background:var(--bg-editor);border:1px solid var(--border);border-radius:3px;margin-bottom:10px}
.crumbbar button{color:var(--a300);padding:1px 3px;border-radius:3px}
.crumbbar button:hover{background:var(--bg-hover)}
.dirlist{border:1px solid var(--border);border-radius:3px;max-height:300px;overflow:auto;
  background:var(--bg-editor)}
.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:1px;background:var(--border-soft);
  border:1px solid var(--border-soft);border-radius:4px;overflow:hidden}
.stats div{background:var(--bg-editor);padding:9px 12px}
.stats b{display:block;font-size:17px;font-weight:500;color:var(--fg-strong);font-family:var(--mono)}
.stats span{font-size:11px;color:var(--fg-muted);text-transform:uppercase;letter-spacing:.4px}
.checks{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:2px 10px}
.checks label{display:flex;align-items:center;gap:7px;padding:3px 5px;border-radius:3px;
  cursor:pointer;font-size:12px;text-transform:none;letter-spacing:0;color:var(--fg)}
.checks label:hover{background:var(--bg-hover)}
.checks input{accent-color:var(--a400);width:13px;height:13px;cursor:pointer;margin:0}
.swatches{display:flex;gap:8px;flex-wrap:wrap}
.swatch{width:30px;height:30px;border-radius:4px;border:2px solid transparent;cursor:pointer}
.swatch.on{border-color:var(--fg-strong)}

/* ---------- preferences popup ---------- */
.modal.wide{width:min(860px,94vw)}
.prefcols{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:22px}
.prefcols section{min-width:0}
.prefcols h4{display:flex;align-items:center;gap:7px;font-size:11px;text-transform:uppercase;
  letter-spacing:.5px;color:var(--fg-muted);font-weight:700;margin-bottom:9px;
  padding-bottom:6px;border-bottom:1px solid var(--border-soft)}
.prefcols h4 .codicon{width:14px;height:14px}
label.sw{display:flex;align-items:flex-start;gap:8px;padding:4px 5px;border-radius:3px;
  cursor:pointer;font-size:12px;line-height:1.5}
label.sw:hover{background:var(--bg-hover)}
label.sw input{accent-color:var(--a400);width:13px;height:13px;margin:2px 0 0;cursor:pointer;flex:0 0 auto}
label.sw em{display:block;font-style:normal;color:var(--fg-faint);font-size:11px}
label.pick{display:flex;align-items:center;justify-content:space-between;gap:10px;
  font-size:12px;padding:4px 5px}
label.pick select,.eqfoot select{background:var(--bg-input);color:var(--fg);
  border:1px solid var(--border);border-radius:3px;padding:3px 6px;font-size:12px;outline:none}
label.pick select:focus,.eqfoot select:focus{border-color:var(--a400)}
.eq{margin:6px 0 4px;transition:opacity .15s}
.eq.off{opacity:.4;pointer-events:none}
.eqrow{display:flex;gap:2px;justify-content:space-between;padding:8px 2px 4px;
  background:var(--bg-editor);border:1px solid var(--border-soft);border-radius:4px}
.band{display:flex;flex-direction:column;align-items:center;gap:3px;flex:1;min-width:0}
.fader{position:relative;width:18px;height:96px;cursor:ns-resize;touch-action:none;
  flex:0 0 auto;outline:none}
.fader::before{content:"";position:absolute;left:50%;top:0;bottom:0;width:4px;margin-left:-2px;
  background:var(--bg-input);border-radius:2px}
.fader .fill{position:absolute;left:50%;width:4px;margin-left:-2px;background:var(--a400);
  border-radius:2px}
.fader .knob{position:absolute;left:50%;width:14px;height:8px;margin-left:-7px;border-radius:2px;
  background:var(--fg);box-shadow:0 1px 2px rgba(0,0,0,.45);pointer-events:none}
.fader:hover .knob{background:var(--fg-strong)}
.fader:focus-visible{outline:1px solid var(--a400);outline-offset:2px;border-radius:3px}
.band .db{font-family:var(--mono);font-size:10px;color:var(--fg)}
.band .hz{font-family:var(--mono);font-size:9px;color:var(--fg-faint)}
.eqfoot{display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin-top:9px;font-size:12px}
.eqfoot label{display:flex;align-items:center;gap:7px;color:var(--fg-muted)}
.eqfoot input[type=range]{width:96px;accent-color:var(--a400);cursor:pointer}
.eqfoot span{font-family:var(--mono);font-size:11px;color:var(--fg)}
.updbox .hint{font-size:12px;line-height:1.7;margin-bottom:8px}
.relnotes{font-size:11px;line-height:1.6;color:var(--fg-muted);background:var(--bg-editor);
  border:1px solid var(--border-soft);border-radius:4px;padding:8px 10px;max-height:120px;
  overflow:auto;white-space:pre-wrap}
a.btn{text-decoration:none;display:inline-block}
html.no-anim .trk .eq i{animation:none;height:6px}
html.no-anim .spin{animation:none}

#ctx{position:fixed;z-index:110;background:var(--bg-widget);border:1px solid var(--border);
  border-radius:5px;box-shadow:0 6px 18px var(--shadow);padding:4px;min-width:210px;display:none}
#ctx.on{display:block}
#ctx button{display:flex;width:100%;align-items:center;gap:9px;padding:4px 10px;border-radius:4px;
  font-size:12px;text-align:left}
#ctx button:hover{background:var(--a600);color:#fff}
#ctx button[disabled]{opacity:.4;pointer-events:none}
#ctx hr{border:none;border-top:1px solid var(--border);margin:4px 2px}
#ctx .key{margin-left:auto;color:var(--fg-faint);font-size:11px}

#toasts{position:fixed;right:16px;bottom:96px;display:flex;flex-direction:column;gap:8px;z-index:120}
.toast{background:var(--bg-widget);border:1px solid var(--border);border-left:3px solid var(--a400);
  border-radius:4px;padding:9px 13px;font-size:12px;box-shadow:0 4px 14px var(--shadow);
  max-width:360px;animation:slide .16s ease-out}
.toast.err{border-left-color:var(--err)}
.toast.ok{border-left-color:var(--ok)}
@keyframes slide{from{transform:translateX(14px);opacity:0}to{transform:none;opacity:1}}
@media (prefers-reduced-motion:reduce){.toast{animation:none}}
</style>"""


UI_BODY = r"""
<svg style="display:none">
  <defs>
    <g id="i-library"><path d="M3 3h5v18H3zM10 3h4v18h-4z"/><path d="M17 4l4 15-3 1-4-15z"/></g>
    <g id="i-search"><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21"/></g>
    <g id="i-playlist"><path d="M3 6h11M3 11h11M3 16h7"/><circle cx="17.5" cy="17" r="3"/><path d="M20.5 17V8l3 1"/></g>
    <g id="i-queue"><path d="M3 5h12M3 10h12M3 15h7M3 20h7"/><path d="M17 10v10M13.5 16.5L17 20l3.5-3.5"/></g>
    <g id="i-settings"><path d="M10.23 1.95L13.77 1.95L13.86 4.53L15.97 5.40L17.85 3.64L20.36 6.15L18.60 8.03L19.47 10.14L22.05 10.23L22.05 13.77L19.47 13.86L18.60 15.97L20.36 17.85L17.85 20.36L15.97 18.60L13.86 19.47L13.77 22.05L10.23 22.05L10.14 19.47L8.03 18.60L6.15 20.36L3.64 17.85L5.40 15.97L4.53 13.86L1.95 13.77L1.95 10.23L4.53 10.14L5.40 8.03L3.64 6.15L6.15 3.64L8.03 5.40L10.14 4.53Z"/><circle cx="12" cy="12" r="3.4"/></g>
    <g id="i-chev"><path d="M9 6l6 6-6 6"/></g>
    <g id="i-play"><path d="M7 4.5l13 7.5-13 7.5z" fill="currentColor" stroke="none"/></g>
    <g id="i-pause"><path d="M7 4h4v16H7zM13 4h4v16h-4z" fill="currentColor" stroke="none"/></g>
    <g id="i-prev"><path d="M18 5v14L7 12z" fill="currentColor" stroke="none"/><path d="M5 5v14" stroke-width="2"/></g>
    <g id="i-next"><path d="M6 5v14l11-7z" fill="currentColor" stroke="none"/><path d="M19 5v14" stroke-width="2"/></g>
    <g id="i-shuffle"><path d="M16 3l4 4-4 4M16 13l4 4-4 4"/><path d="M20 7h-4.2a5 5 0 00-4 2L9 14a5 5 0 01-4 2H3M3 7h2a5 5 0 014 2l.5.7M14.5 14.3l.5.7a5 5 0 004 2H20"/></g>
    <g id="i-repeat"><path d="M17 2l3 3-3 3"/><path d="M20 5H8a4 4 0 00-4 4v1"/><path d="M7 22l-3-3 3-3"/><path d="M4 19h12a4 4 0 004-4v-1"/></g>
    <g id="i-vol"><path d="M11 5L6.5 9H3v6h3.5L11 19z" fill="currentColor" stroke="none"/><path d="M15.5 8.5a5 5 0 010 7M18.5 5.5a9 9 0 010 13"/></g>
    <g id="i-mute"><path d="M11 5L6.5 9H3v6h3.5L11 19z" fill="currentColor" stroke="none"/><path d="M16 9.5l5 5M21 9.5l-5 5"/></g>
    <g id="i-refresh"><path d="M20 4v6h-6"/><path d="M4 20v-6h6"/><path d="M19 10a7.5 7.5 0 00-13-3L4 9M5 14a7.5 7.5 0 0013 3l2-2"/></g>
    <g id="i-folder"><path d="M3 7a2 2 0 012-2h4l2 2.5h8a2 2 0 012 2V18a2 2 0 01-2 2H5a2 2 0 01-2-2z"/></g>
    <g id="i-note"><path d="M9 18V5l11-2v13" /><circle cx="6.5" cy="18" r="2.8"/><circle cx="17.5" cy="16" r="2.8"/></g>
    <g id="i-disc"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2.6"/></g>
    <g id="i-person"><circle cx="12" cy="8" r="4"/><path d="M4.5 20a7.5 7.5 0 0115 0"/></g>
    <g id="i-x"><path d="M5 5l14 14M19 5L5 19"/></g>
    <g id="i-plus"><path d="M12 5v14M5 12h14"/></g>
    <g id="i-clock"><circle cx="12" cy="12" r="9"/><path d="M12 7v5.5l3.5 2"/></g>
    <g id="i-star"><path d="M12 3.5l2.6 5.6 6 .8-4.4 4.2 1.1 6-5.3-2.9-5.3 2.9 1.1-6L3.4 9.9l6-.8z"/></g>
    <g id="i-warn"><path d="M12 3l9.5 17H2.5z"/><path d="M12 9.5v5M12 17.2v.1"/></g>
    <g id="i-check"><path d="M4 12.5l5.5 5.5L20 6"/></g>
    <g id="i-minimise"><path d="M5 12h14"/></g>
    <g id="i-maximise"><rect x="4.5" y="4.5" width="15" height="15" rx="2"/></g>
    <g id="i-restore"><rect x="4.5" y="7.5" width="12" height="12" rx="2"/><path d="M8 7.5V6a1.5 1.5 0 011.5-1.5H18A1.5 1.5 0 0119.5 6v8.5A1.5 1.5 0 0118 16h-1.5"/></g>
    <g id="i-download"><path d="M12 3v12"/><path d="M7 10.5l5 5 5-5"/><path d="M4 20h16"/></g>
    <g id="i-sliders"><path d="M5 6h14M5 12h14M5 18h14"/><circle cx="9" cy="6" r="2.2"/><circle cx="15" cy="12" r="2.2"/><circle cx="8" cy="18" r="2.2"/></g>
    <g id="i-pin"><path d="M12 2v9M8 11h8l1.5 5H6.5zM12 16v6"/></g>
  </defs>
</svg>

<div id="shell">
  <div id="titlebar">
    <div id="wincontrols">
      <button class="win close" id="win-close" title="Quit Cadence" aria-label="Quit"></button>
      <button class="win min" id="win-restore" title="Leave full screen" aria-label="Restore"></button>
      <button class="win max" id="win-max" title="Full screen" aria-label="Full screen"></button>
    </div>
    <img id="app-icon" alt="">
    <div class="menu" data-menu="file"><button>File</button><div class="menu-pop" id="menu-file"></div></div>
    <div class="menu" data-menu="edit"><button>Edit</button><div class="menu-pop" id="menu-edit"></div></div>
    <div class="menu" data-menu="view"><button>View</button><div class="menu-pop" id="menu-view"></div></div>
    <div class="menu" data-menu="play"><button>Playback</button><div class="menu-pop" id="menu-play"></div></div>
    <div class="menu" data-menu="help"><button>Help</button><div class="menu-pop" id="menu-help"></div></div>
    <button id="command-center" title="Search your library (Ctrl+P)">
      <svg class="codicon" viewBox="0 0 24 24" style="width:13px;height:13px"><use href="#i-search"/></svg>
      <span id="cc-label">Search Cadence</span>
    </button>
    <div class="spacer"></div>
  </div>

  <nav id="activity" aria-label="Views">
    <button class="act on" data-view="library" title="Library (Ctrl+Shift+E)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-library"/></svg></button>
    <button class="act" data-view="search" title="Search (Ctrl+Shift+F)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-search"/></svg></button>
    <button class="act" data-view="playlists" title="Playlists (Ctrl+Shift+Y)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-playlist"/></svg></button>
    <button class="act" data-view="queue" title="Queue (Ctrl+Shift+Q)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-queue"/></svg><span class="badge" id="queue-badge" hidden></span></button>
    <div class="gap"></div>
    <button class="act" data-view="settings" title="Settings"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-settings"/></svg></button>
  </nav>

  <aside id="side">
    <div id="side-title"><span id="side-heading">Library</span><div class="acts" id="side-actions"></div></div>
    <div id="side-body"></div>
    <div id="side-grip" title="Resize side bar"></div>
  </aside>

  <main id="editor">
    <div id="tabs"></div>
    <div id="breadcrumbs"></div>
    <div id="list-head"></div>
    <div id="list-wrap" tabindex="0">
      <div id="list-sizer"><div id="list-rows"></div></div>
      <div id="list-empty" class="empty" hidden></div>
    </div>
    <div id="panel">
      <div id="panel-grip"></div>
      <div id="panel-tabs">
        <button class="pt on" data-tab="queue">Queue</button>
        <button class="pt" data-tab="details">Details</button>
        <button class="pt" data-tab="output">Output</button>
        <button class="close" id="panel-close" title="Close panel (Ctrl+J)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-x"/></svg></button>
      </div>
      <div id="panel-body"></div>
    </div>
    <div id="player">
      <div id="art"><svg class="codicon" viewBox="0 0 24 24" style="width:20px;height:20px"><use href="#i-note"/></svg></div>
      <div id="np"><div class="t" id="np-title">Nothing playing</div><div class="a" id="np-artist">Cadence</div></div>
      <button class="pbtn" id="btn-shuffle" title="Shuffle (S)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-shuffle"/></svg></button>
      <button class="pbtn" id="btn-prev" title="Previous (Ctrl+Left)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-prev"/></svg></button>
      <button class="pbtn big" id="btn-play" title="Play / Pause (Space)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-play"/></svg></button>
      <button class="pbtn" id="btn-next" title="Next (Ctrl+Right)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-next"/></svg></button>
      <button class="pbtn" id="btn-repeat" title="Repeat (R)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-repeat"/></svg></button>
      <div id="seek-wrap">
        <span class="time" id="t-now">0:00</span>
        <div class="bar" id="seek"><div class="buf"></div><div class="fill"></div><div class="knob"></div></div>
        <span class="time r" id="t-end">0:00</span>
      </div>
      <button class="pbtn" id="btn-vol" title="Mute (M)"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-vol"/></svg></button>
      <div class="bar" id="vol"><div class="fill"></div><div class="knob"></div></div>
    </div>
  </main>

  <footer id="status">
    <button class="item" id="st-play"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-play"/></svg><span id="st-play-label">Stopped</span></button>
    <button class="item" id="st-now"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-note"/></svg><span id="st-now-label">No track</span></button>
    <div class="gap"></div>
    <button class="item" id="st-scan" hidden><svg class="codicon spin"><use href="#i-refresh"/></svg><span id="st-scan-label">Scanning</span></button>
    <button class="item" id="st-sel" hidden><span id="st-sel-label"></span></button>
    <button class="item" id="st-format"><span id="st-format-label">-</span></button>
    <button class="item" id="st-count"><svg class="codicon" viewBox="0 0 24 24"><use href="#i-library"/></svg><span id="st-count-label">0 tracks</span></button>
    <button class="item" id="st-version" title="Which build this is - click for preferences">
      <span id="st-version-label"></span></button>
    <button class="item" id="st-update" hidden title="An update is available">
      <svg class="codicon" viewBox="0 0 24 24"><use href="#i-download"/></svg>
      <span id="st-update-label">Update</span></button>
    <button class="item" id="st-theme" title="Cycle theme"><span id="st-theme-label">Dark</span></button>
  </footer>
</div>

<div id="scrim"></div>
<div class="qp" id="qp">
  <input id="qp-input" autocomplete="off" spellcheck="false" placeholder="Search">
  <div class="results" id="qp-results"></div>
</div>
<div class="modal" id="modal">
  <h3 id="modal-title">Dialog</h3>
  <div class="mbody" id="modal-body"></div>
  <div class="mfoot" id="modal-foot"></div>
</div>
<div id="ctx"></div>
<div id="toasts"></div>
<audio id="audio" preload="metadata"></audio>
"""


UI_SCRIPT_CORE = r"""
const $  = (s, r) => (r || document).querySelector(s);
const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
let ROW_H = 24;

const state = {
  token: window.CADENCE_TOKEN,
  info: null,
  tracks: [],
  byId: new Map(),
  playlists: [],
  tabs: [],
  activeTab: null,
  view: 'library',
  sort: { key: 'default', dir: 1 },
  columns: [],
  prefs: {},
  update: {},
  sel: new Set(),
  anchor: -1,
  search: '',
  expanded: new Set(),
  rows: [],
  scanTimer: null,
  outputLog: [],
};

const player = {
  queue: [], order: [], pos: -1,
  shuffle: false, repeat: 'off', volume: 0.8, muted: false,
  current: null, seeking: false,
};

const audio = $('#audio');

/* ---------- helpers ---------- */
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function fmtTime(sec) {
  if (!isFinite(sec) || sec <= 0) return '0:00';
  sec = Math.round(sec);
  const h = Math.floor(sec / 3600), m = Math.floor(sec % 3600 / 60), s = sec % 60;
  return h ? `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
           : `${m}:${String(s).padStart(2, '0')}`;
}
function fmtSpan(sec) {
  sec = Math.round(sec || 0);
  const d = Math.floor(sec / 86400), h = Math.floor(sec % 86400 / 3600), m = Math.floor(sec % 3600 / 60);
  if (d) return `${d}d ${h}h`;
  if (h) return `${h}h ${m}m`;
  return `${m}m`;
}
function fmtBytes(n) {
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0; n = n || 0;
  while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
  return `${n < 10 && i ? n.toFixed(1) : Math.round(n)} ${units[i]}`;
}
const icon = (name, cls) =>
  `<svg class="codicon ${cls || ''}" viewBox="0 0 24 24"><use href="#i-${name}"/></svg>`;

async function api(path, options) {
  const opts = Object.assign({ headers: {} }, options || {});
  opts.headers['X-Cadence-Token'] = state.token;
  if (opts.body !== undefined) {
    opts.method = opts.method || 'POST';
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(opts.body);
  }
  const response = await fetch(path, opts);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}
const streamUrl = id => `/api/stream?id=${id}&t=${encodeURIComponent(state.token)}`;
const artUrl    = id => `/api/art?id=${id}&t=${encodeURIComponent(state.token)}`;

function toast(message, kind) {
  const node = document.createElement('div');
  node.className = 'toast ' + (kind || '');
  node.innerHTML = esc(message);
  $('#toasts').appendChild(node);
  setTimeout(() => node.remove(), kind === 'err' ? 6500 : 3800);
}
function out(line) {
  state.outputLog.push(line);
  if (state.outputLog.length > 500) state.outputLog.shift();
  if ($('#panel').classList.contains('open') && panelTab === 'output') renderPanel();
}

/* ---------- columns & sorting ---------- */
// `fixed` columns are always drawn - a track list with no number or title is
// not much of a list. Everything else is switched on and off under Settings.
const COLUMNS = [
  { key: 'index',       label: '#',            width: '48px',   cls: 'num', fixed: true },
  { key: 'title',       label: 'Title',        width: 'minmax(140px,3fr)',  fixed: true },
  { key: 'artist',      label: 'Artist',       width: 'minmax(100px,2fr)' },
  { key: 'album',       label: 'Album',        width: 'minmax(100px,2fr)' },
  { key: 'albumartist', label: 'Album artist', width: 'minmax(100px,2fr)' },
  { key: 'genre',       label: 'Genre',        width: 'minmax(70px,1fr)' },
  { key: 'track',       label: 'Track',        width: '58px',   cls: 'year', numeric: true },
  { key: 'year',        label: 'Year',         width: '54px',   cls: 'year', numeric: true },
  { key: 'codec',       label: 'Format',       width: '74px' },
  { key: 'plays',       label: 'Plays',        width: '56px',   cls: 'year', numeric: true },
  { key: 'duration',    label: 'Time',         width: '58px',   cls: 'dur',  numeric: true },
];
const OPTIONAL_COLUMNS = COLUMNS.filter(c => !c.fixed);
// The set shown before anyone changes anything, i.e. what the list looked like
// when every column was hard-coded.
const DEFAULT_COLUMNS = ['artist', 'album', 'genre', 'year', 'duration'];

const visibleColumns = () => COLUMNS.filter(c => c.fixed || state.columns.includes(c.key));

function setColumn(key, on) {
  const wanted = new Set(state.columns);
  on ? wanted.add(key) : wanted.delete(key);
  // Keep the configured order rather than the order they were ticked in.
  state.columns = OPTIONAL_COLUMNS.filter(c => wanted.has(c.key)).map(c => c.key);
  saveSetting('columns', state.columns.join(','));
  if (state.sort.key !== 'default' && !visibleColumns().some(c => c.key === state.sort.key)) {
    state.sort = { key: 'default', dir: 1 };   // the sorted column just went away
  }
}

function sortTracks(list) {
  const { key, dir } = state.sort;
  const copy = list.slice();
  if (key === 'default') return copy;
  const text = v => String(v == null ? '' : v).toLowerCase();
  const column = COLUMNS.find(c => c.key === key);
  const numeric = (column && column.numeric) || key === 'added';
  copy.sort((a, b) => {
    let x, y;
    if (numeric) { x = a[key] || 0; y = b[key] || 0; }
    else { x = text(a[key]); y = text(b[key]); }
    if (x < y) return -dir;
    if (x > y) return dir;
    return text(a.album) < text(b.album) ? -1 : (a.track || 0) - (b.track || 0);
  });
  return copy;
}

/* ---------- tabs ---------- */
function openTab(tab) {
  const existing = state.tabs.find(t => t.id === tab.id);
  if (existing) { Object.assign(existing, tab); state.activeTab = existing.id; }
  else { state.tabs.push(tab); state.activeTab = tab.id; }
  state.sel.clear(); state.anchor = -1;
  renderTabs(); renderList();
}
function closeTab(id) {
  const index = state.tabs.findIndex(t => t.id === id);
  if (index < 0) return;
  state.tabs.splice(index, 1);
  if (state.activeTab === id) {
    const next = state.tabs[Math.min(index, state.tabs.length - 1)];
    state.activeTab = next ? next.id : null;
  }
  renderTabs(); renderList();
}
const currentTab = () => state.tabs.find(t => t.id === state.activeTab) || null;

function renderTabs() {
  $('#tabs').innerHTML = state.tabs.map(tab => `
    <div class="tab ${tab.id === state.activeTab ? 'on' : ''}" data-id="${esc(tab.id)}" title="${esc(tab.title)}">
      ${icon(tab.icon || 'note')}<span class="name">${esc(tab.title)}</span>
      <span class="x" data-close="${esc(tab.id)}">${icon('x')}</span>
    </div>`).join('');
  const tab = currentTab();
  $('#breadcrumbs').innerHTML = (tab ? tab.crumbs || [tab.title] : [])
    .map(c => `<span class="crumb">${esc(c)}</span>`).join('');
}

/* ---------- track list ---------- */
function tracksForTab(tab) {
  if (!tab) return [];
  let list;
  switch (tab.kind) {
    case 'all':      list = state.tracks; break;
    case 'recent':   list = state.tracks.slice().sort((a, b) => b.added - a.added).slice(0, 200); break;
    case 'played':   list = state.tracks.filter(t => t.plays > 0)
                              .sort((a, b) => b.plays - a.plays || b.last_played - a.last_played); break;
    case 'artist':   list = state.tracks.filter(t => t.albumartist === tab.value || t.artist === tab.value); break;
    case 'album':    list = state.tracks.filter(t => t.album === tab.value &&
                              (!tab.artist || t.albumartist === tab.artist || t.artist === tab.artist)); break;
    case 'genre':    list = state.tracks.filter(t => (t.genre || 'Unknown') === tab.value); break;
    case 'folder':   list = state.tracks.filter(t => t.folder === tab.value); break;
    case 'playlist': {
      const playlist = state.playlists.find(p => p.id === tab.value);
      list = playlist ? playlist.tracks.map(id => state.byId.get(id)).filter(Boolean) : [];
      return list;  // playlist order is meaningful, never re-sorted by default
    }
    case 'search':   list = matchTracks(tab.value); break;
    case 'queue':    return player.queue.map(id => state.byId.get(id)).filter(Boolean);
    default:         list = [];
  }
  return sortTracks(list);
}

function matchTracks(query) {
  const terms = String(query || '').toLowerCase().split(/\s+/).filter(Boolean);
  if (!terms.length) return [];
  return state.tracks.filter(t => {
    const hay = `${t.title} ${t.artist} ${t.album} ${t.albumartist} ${t.genre || ''} ${t.filename}`.toLowerCase();
    return terms.every(term => hay.includes(term));
  });
}

function renderList() {
  const tab = currentTab();
  const head = $('#list-head'), wrap = $('#list-wrap'), empty = $('#list-empty');
  const sizer = $('#list-sizer');

  if (!tab || tab.kind === 'custom') {
    head.hidden = true; sizer.hidden = true; empty.hidden = false;
    empty.innerHTML = tab ? tab.html : welcomeHtml();
    state.rows = [];
    updateStatus();
    return;
  }
  head.hidden = false; sizer.hidden = false;

  state.rows = tracksForTab(tab);
  const cols = visibleColumns();
  $('#editor').style.setProperty('--cols', cols.map(c => c.width).join(' '));
  head.innerHTML = cols.map(c => {
    const sorted = state.sort.key === c.key;
    return `<div class="col ${c.cls || ''} ${sorted ? 'sorted' : ''}" data-sort="${c.key}">
      <span>${esc(c.label)}</span><span class="arrow">${sorted ? (state.sort.dir > 0 ? '▲' : '▼') : '▲'}</span></div>`;
  }).join('');

  if (!state.rows.length) {
    sizer.hidden = true; empty.hidden = false;
    empty.innerHTML = emptyHtml(tab);
    updateStatus();
    return;
  }
  empty.hidden = true;
  sizer.style.height = (state.rows.length * ROW_H) + 'px';
  paintRows();
  updateStatus();
}

function paintRows() {
  const wrap = $('#list-wrap');
  const total = state.rows.length;
  const top = wrap.scrollTop;
  const first = Math.max(0, Math.floor(top / ROW_H) - 10);
  const last = Math.min(total, Math.ceil((top + wrap.clientHeight) / ROW_H) + 10);
  const cols = visibleColumns();
  const parts = [];
  for (let i = first; i < last; i++) {
    const t = state.rows[i];
    const playing = player.current && player.current.id === t.id;
    const cells = cols.map(c => {
      const value = cellValue(c, t, i, playing);
      const tip = c.key === 'index' || c.numeric ? '' : ` title="${esc(t[c.key] || '')}"`;
      return `<div class="c ${c.cls || ''}"${tip}>${value}</div>`;
    }).join('');
    parts.push(`<div class="trk ${state.sel.has(i) ? 'sel' : ''} ${playing ? 'cur' : ''}"
      data-i="${i}" data-id="${t.id}" style="top:${i * ROW_H}px">${cells}</div>`);
  }
  $('#list-rows').innerHTML = parts.join('');
}

function cellValue(column, track, index, playing) {
  switch (column.key) {
    case 'index':    return playing ? '<span class="eq"><i></i><i></i><i></i></span>' : (index + 1);
    case 'duration': return fmtTime(track.duration);
    case 'year':
    case 'track':    return track[column.key] || '';
    case 'plays':    return track.plays || '';
    default:         return esc(track[column.key] || '');
  }
}

function emptyHtml(tab) {
  if (tab.kind === 'search') {
    return `${icon('search', 'big')}<h2>No matches</h2>
      <p>Nothing in the library matches <code>${esc(tab.value)}</code>. Try fewer words —
      search looks at title, artist, album, genre and filename.</p>`;
  }
  if (tab.kind === 'queue') {
    return `${icon('queue')}<h2>The queue is empty</h2>
      <p>Double-click a track to start playing, or right-click a selection and choose
      <b>Add to Queue</b>.</p>`;
  }
  return `${icon('note')}<h2>Nothing here</h2><p>This view has no tracks.</p>`;
}

function welcomeHtml() {
  const folder = state.info && state.info.folder;
  if (!folder) {
    return `<img src="/icon.svg" width="72" height="72" alt="">
      <h2>Designate your music folder</h2>
      <p>Cadence indexes one folder and everything beneath it, then re-reads it on every
      launch. Only files whose size or timestamp changed get parsed again, so start-up
      stays fast once the first scan is done.</p>
      <button class="btn" data-act="pick-folder">Choose Folder…</button>
      <p style="font-size:12px;color:var(--fg-faint)">You can also pass it on the command
      line: <code>Cadence.exe --folder "D:\Music"</code></p>`;
  }
  return `<img src="/icon.svg" width="64" height="64" alt="">
    <h2>${esc(state.info.stats.tracks)} tracks indexed</h2>
    <p>From <code>${esc(folder)}</code></p>
    <div style="display:flex;gap:8px">
      <button class="btn" data-act="open-all">Open Library</button>
      <button class="btn quiet" data-act="rescan">Rescan</button>
    </div>`;
}
"""


UI_SCRIPT_VIEWS = r"""
/* ---------- side bar ---------- */
function groupBy(list, keyOf) {
  const map = new Map();
  for (const item of list) {
    const key = keyOf(item) || 'Unknown';
    if (!map.has(key)) map.set(key, []);
    map.get(key).push(item);
  }
  return map;
}

function renderSide() {
  const heading = $('#side-heading'), body = $('#side-body'), actions = $('#side-actions');
  actions.innerHTML = '';
  const addAction = (name, title, act) => {
    actions.insertAdjacentHTML('beforeend',
      `<button title="${esc(title)}" data-act="${act}">${icon(name)}</button>`);
  };

  if (state.view === 'library') {
    heading.textContent = 'Library';
    addAction('refresh', 'Rescan folder', 'rescan');
    addAction('folder', 'Change music folder', 'pick-folder');
    body.innerHTML = librarySide();
  } else if (state.view === 'search') {
    heading.textContent = 'Search';
    body.innerHTML = `
      <div style="padding:8px 10px">
        <input id="search-box" class="sbox" placeholder="Search tracks" autocomplete="off"
          spellcheck="false" value="${esc(state.search)}"
          style="width:100%;background:var(--bg-input);color:var(--fg);border:1px solid var(--border);
                 padding:5px 8px;border-radius:3px;outline:none">
        <div id="search-count" style="margin-top:8px;color:var(--fg-muted);font-size:11px"></div>
      </div>
      <div id="search-side"></div>`;
    const box = $('#search-box');
    box.addEventListener('input', () => { state.search = box.value; runSearch(); });
    box.focus(); box.select();
    runSearch();
  } else if (state.view === 'playlists') {
    heading.textContent = 'Playlists';
    addAction('plus', 'New playlist', 'new-playlist');
    body.innerHTML = state.playlists.length ? state.playlists.map(p => `
      <div class="row ${state.activeTab === 'pl:' + p.id ? 'on' : ''}" data-open="playlist" data-value="${p.id}"
           data-ctx="playlist" data-name="${esc(p.name)}">
        ${icon('playlist')}<span class="label">${esc(p.name)}</span>
        <span class="sub">${p.count}</span></div>`).join('')
      : `<div style="padding:14px 12px;color:var(--fg-muted);font-size:12px;line-height:1.7">
           No playlists yet. Select tracks, right-click, then <b>Add to Playlist</b> —
           or press the <b>+</b> above.</div>`;
  } else if (state.view === 'queue') {
    heading.textContent = 'Queue';
    addAction('x', 'Clear queue', 'clear-queue');
    body.innerHTML = player.queue.length ? player.queue.map((id, i) => {
      const t = state.byId.get(id); if (!t) return '';
      return `<div class="row ${player.pos >= 0 && player.order[player.pos] === i ? 'on' : ''}"
        data-qjump="${i}" title="${esc(t.artist + ' — ' + t.title)}">
        ${icon(player.pos >= 0 && player.order[player.pos] === i ? 'play' : 'note')}
        <span class="label">${esc(t.title)}</span>
        <span class="sub">${fmtTime(t.duration)}</span></div>`;
    }).join('')
      : `<div style="padding:14px 12px;color:var(--fg-muted);font-size:12px;line-height:1.7">
           Queue is empty. Double-click any track to start.</div>`;
  } else if (state.view === 'settings') {
    heading.textContent = 'Settings';
    body.innerHTML = settingsSide();
  }
  $$('#side-body .row').forEach(row => row.setAttribute('tabindex', '0'));
}

function librarySide() {
  if (!state.tracks.length) {
    return `<div style="padding:14px 12px;color:var(--fg-muted);font-size:12px;line-height:1.7">
      ${state.info && state.info.folder
        ? 'No audio files were found in the designated folder.'
        : 'No folder designated yet.'}
      <br><br><button class="btn" data-act="pick-folder" style="width:100%">Choose Folder…</button></div>`;
  }
  const artists = groupBy(state.tracks, t => t.albumartist);
  const genres = groupBy(state.tracks.filter(t => t.genre), t => t.genre);
  const views = [
    ['all', 'library', 'All Tracks', state.tracks.length],
    ['recent', 'clock', 'Recently Added', Math.min(200, state.tracks.length)],
    ['played', 'star', 'Most Played', state.tracks.filter(t => t.plays > 0).length],
  ];

  let html = `<div class="section" data-sec="views"><div class="head">
      ${icon('chev', 'chev')}<span>Views</span></div><div class="body">` +
    views.map(([kind, ic, label, count]) => `
      <div class="row lvl1 ${state.activeTab === kind ? 'on' : ''}" data-open="${kind}">
        ${icon(ic)}<span class="label">${esc(label)}</span><span class="sub">${count}</span></div>`).join('') +
    `</div></div>`;

  html += `<div class="section" data-sec="artists"><div class="head">
      ${icon('chev', 'chev')}<span>Artists</span><span class="count">${artists.size}</span></div><div class="body">`;
  for (const name of Array.from(artists.keys()).sort((a, b) => a.localeCompare(b))) {
    const items = artists.get(name);
    const open = state.expanded.has('ar:' + name);
    html += `<div class="row lvl1 ${open ? '' : 'closed'}" data-twist="ar:${esc(name)}"
        data-open="artist" data-value="${esc(name)}" title="${esc(name)}">
      ${icon('chev', 'twist')}${icon('person')}<span class="label">${esc(name)}</span>
      <span class="sub">${items.length}</span></div>`;
    if (open) {
      const albums = groupBy(items, t => t.album);
      for (const album of Array.from(albums.keys()).sort((a, b) => {
        const ya = albums.get(a)[0].year || 0, yb = albums.get(b)[0].year || 0;
        return ya - yb || a.localeCompare(b);
      })) {
        const tracks = albums.get(album);
        html += `<div class="row lvl2" data-open="album" data-value="${esc(album)}"
            data-artist="${esc(name)}" title="${esc(album)}">
          ${icon('disc')}<span class="label">${esc(album)}</span>
          <span class="sub">${tracks[0].year || tracks.length}</span></div>`;
      }
    }
  }
  html += `</div></div>`;

  if (genres.size) {
    html += `<div class="section collapsed" data-sec="genres"><div class="head">
      ${icon('chev', 'chev')}<span>Genres</span><span class="count">${genres.size}</span></div><div class="body">`;
    for (const name of Array.from(genres.keys()).sort((a, b) => a.localeCompare(b))) {
      html += `<div class="row lvl1" data-open="genre" data-value="${esc(name)}">
        ${icon('note')}<span class="label">${esc(name)}</span>
        <span class="sub">${genres.get(name).length}</span></div>`;
    }
    html += `</div></div>`;
  }
  return html;
}

function settingsSide() {
  const info = state.info || {};
  const stats = info.stats || {};
  return `<div style="padding:10px 12px;font-size:12px;line-height:1.9">
    <div style="color:var(--fg-muted);text-transform:uppercase;font-size:10px;letter-spacing:.5px">
      Music folder</div>
    <div style="font-family:var(--mono);font-size:11px;word-break:break-all;color:${
      info.folder_exists ? 'var(--fg)' : 'var(--err)'}">${esc(info.folder || 'not set')}</div>
    <div style="display:flex;gap:6px;margin:10px 0 16px">
      <button class="btn" data-act="pick-folder" style="flex:1">Change…</button>
      <button class="btn quiet" data-act="rescan">Rescan</button>
    </div>
    <div class="stats">
      <div><b>${stats.tracks || 0}</b><span>Tracks</span></div>
      <div><b>${stats.artists || 0}</b><span>Artists</span></div>
      <div><b>${stats.albums || 0}</b><span>Albums</span></div>
      <div><b>${fmtSpan(stats.seconds)}</b><span>Duration</span></div>
    </div>
    <div style="margin-top:14px;color:var(--fg-muted)">
      ${fmtBytes(stats.bytes)} on disk<br>
      <button class="btn ghost" data-act="settings-tab" style="margin-top:10px;width:100%">
        Open Settings</button>
    </div>
  </div>`;
}

function runSearch() {
  const results = matchTracks(state.search);
  const box = $('#search-count');
  if (box) {
    box.textContent = state.search.trim()
      ? `${results.length} result${results.length === 1 ? '' : 's'}`
      : 'Type to search title, artist, album, genre or filename.';
  }
  const side = $('#search-side');
  if (side) {
    const albums = groupBy(results, t => t.album);
    side.innerHTML = Array.from(albums.keys()).slice(0, 60).map(album => `
      <div class="row lvl1" data-open="album" data-value="${esc(album)}">
        ${icon('disc')}<span class="label">${esc(album)}</span>
        <span class="sub">${albums.get(album).length}</span></div>`).join('');
  }
  if (state.search.trim()) {
    openTab({ id: 'search', kind: 'search', value: state.search, icon: 'search',
              title: `Search: ${state.search}`, crumbs: ['Search', state.search] });
  }
}

/* ---------- settings tab ---------- */
const ACCENTS = [
  ['Deep Blue',    '#05213d,#072c52,#0a3a6b,#0d4c8b,#1060ad,#2a7fd4,#57a6ee'],
  ['Midnight',     '#04182c,#06223d,#082c52,#0a3a6b,#0d4c8b,#1f6fbd,#4a96dc'],
  ['Azure',        '#062036,#08304f,#0a4272,#0c5595,#0e6ec0,#2f8fe0,#66b4f2'],
  ['Steel',        '#101c28,#16283a,#1d3750,#264a6b,#31618c,#4a83b5,#77a8d4'],
  ['Indigo',       '#140f33,#1c1547,#261d63,#332885,#4336ab,#6355d8,#8f86ee'],
  ['Teal',         '#04231f,#063330,#084642,#0a5c57,#0d7a72,#1aa79b,#4fd0c3'],
  ['Slate',        '#12171c,#1a2128,#243039,#31414e,#425767,#5f7c90,#8fa9ba'],
  ['Ocean',        '#04222e,#063243,#08465e,#0a5c7d,#0d78a3,#1aa0d4,#55c3ef'],
  ['Violet',       '#1c0f2e,#281640,#361f58,#472a75,#5b3699,#7a56c4,#a488e4'],
  ['Plum',         '#2a0f22,#3b1630,#501e41,#6a2856,#8a3470,#b4519a,#d986c1'],
  ['Ember',        '#2d1206,#3f1a09,#55240d,#713111,#934218,#c46430,#e69366'],
  ['Amber',        '#2b1d03,#3d2905,#523807,#6d4a09,#8f620d,#bf871c,#e5b451'],
  ['Moss',         '#132008,#1c2e0c,#274011,#345616,#46721e,#63a02f,#93c866'],
  ['Crimson',      '#2e0c14,#40111d,#571728,#731f36,#962a48,#c44866,#e4849a'],
  ['Graphite',     '#1a1a1a,#242424,#313131,#414141,#565656,#7a7a7a,#a8a8a8'],
];

function settingsTabHtml() {
  const info = state.info || {};
  const stats = info.stats || {};
  const accent = (info.settings && info.settings.accent) || 'Deep Blue';
  const theme = document.documentElement.dataset.theme || 'dark';
  return `<div style="width:100%;max-width:720px;text-align:left;align-self:flex-start;
       margin:0 auto;padding:8px 0 30px">
    <div style="display:flex;align-items:center;gap:14px;margin-bottom:22px">
      <img src="/icon.svg" width="52" height="52" alt="">
      <div><h2 style="margin:0">Cadence</h2>
        <div style="color:var(--fg-muted);font-size:12px">Version ${esc(info.version || '')} ·
        single-file music manager</div></div>
    </div>

    <div class="field"><label>Music folder</label>
      <div style="display:flex;gap:8px">
        <input readonly value="${esc(info.folder || 'not set')}" style="flex:1;font-family:var(--mono);font-size:11px">
        <button class="btn" data-act="pick-folder">Change…</button>
        <button class="btn quiet" data-act="rescan">Rescan</button>
        <button class="btn quiet" data-act="rescan-full" title="Re-read every file, ignoring timestamps">Full</button>
      </div>
      <div class="hint">${info.folder_exists ? 'Scanned on every launch; only changed files are re-read.'
        : '<span style="color:var(--err)">This folder is not reachable right now.</span>'}</div>
    </div>

    <div class="field"><label>Theme</label>
      <div style="display:flex;gap:8px">
        <button class="btn ${theme === 'dark' ? '' : 'quiet'}" data-theme-set="dark">Dark</button>
        <button class="btn ${theme === 'light' ? '' : 'quiet'}" data-theme-set="light">Light</button>
      </div></div>

    <div class="field"><label>Accent</label>
      <div class="swatches">${ACCENTS.map(([name, ramp]) => {
        const stops = ramp.split(',');
        return `<button class="swatch ${name === accent ? 'on' : ''}" data-accent="${esc(name)}"
          title="${esc(name)}" style="background:linear-gradient(135deg,${stops[3]},${stops[5]})"></button>`;
      }).join('')}</div>
      <div class="hint">Recolours the status bar, selection, buttons and the active tab marker.</div>
    </div>

    <div class="field"><label>Track list columns</label>
      <div class="checks">${OPTIONAL_COLUMNS.map(c => `
        <label><input type="checkbox" data-col="${c.key}"
          ${state.columns.includes(c.key) ? 'checked' : ''}> ${esc(c.label)}</label>`).join('')}
      </div>
      <div class="hint">Number and Title are always shown. Unticked details are
      hidden from the track list — the tags are still read and still searchable,
      and the Details panel keeps showing everything for the selected track.</div>
    </div>

    <div class="field"><label>Library</label>
      <div class="stats">
        <div><b>${stats.tracks || 0}</b><span>Tracks</span></div>
        <div><b>${stats.artists || 0}</b><span>Artists</span></div>
        <div><b>${stats.albums || 0}</b><span>Albums</span></div>
        <div><b>${fmtSpan(stats.seconds)}</b><span>Total time</span></div>
        <div><b>${fmtBytes(stats.bytes)}</b><span>On disk</span></div>
        <div><b>${state.playlists.length}</b><span>Playlists</span></div>
      </div></div>

    <div class="field"><label>Where things live</label>
      <div class="hint" style="font-family:var(--mono);word-break:break-all;line-height:1.8">
        index &nbsp;${esc(info.db || '')}<br>
        serving &nbsp;${esc(location.origin)}
      </div>
      <div class="hint" style="margin-top:8px">The index is a plain SQLite file. Deleting it
      only discards the cache and your playlists — your audio files are never modified,
      moved or written to by Cadence.</div>
    </div>

    <div class="field"><label>Keyboard</label>
      <div class="hint" style="line-height:2">
        <b>Ctrl+P</b> quick open · <b>Ctrl+Shift+P</b> command palette ·
        <b>Space</b> play/pause · <b>Ctrl+←/→</b> previous/next ·
        <b>S</b> shuffle · <b>R</b> repeat · <b>M</b> mute ·
        <b>Ctrl+B</b> side bar · <b>Ctrl+J</b> panel · <b>Enter</b> play selection ·
        <b>Ctrl+A</b> select all · <b>Delete</b> remove from playlist or queue
      </div></div>
  </div>`;
}
"""


UI_SCRIPT_PLAYER = r"""
/* ---------- playback ---------- */
function buildOrder(startIndex) {
  const n = player.queue.length;
  const indices = Array.from({ length: n }, (_, i) => i);
  if (!player.shuffle) { player.order = indices; player.pos = startIndex; return; }
  for (let i = n - 1; i > 0; i--) {           // Fisher-Yates
    const j = Math.floor(Math.random() * (i + 1));
    [indices[i], indices[j]] = [indices[j], indices[i]];
  }
  if (startIndex >= 0) {
    const at = indices.indexOf(startIndex);
    if (at > 0) { [indices[0], indices[at]] = [indices[at], indices[0]]; }
    player.pos = 0;
  } else player.pos = -1;
  player.order = indices;
}

function playQueue(ids, startIndex) {
  player.queue = ids.slice();
  buildOrder(startIndex == null ? 0 : startIndex);
  playAt(player.pos);
}

function playAt(pos) {
  if (pos < 0 || pos >= player.order.length) return stop();
  player.pos = pos;
  const track = state.byId.get(player.queue[player.order[pos]]);
  if (!track) return next();
  player.current = track;
  audio.src = streamUrl(track.id);
  audio.play().catch(err => {
    if (err && err.name !== 'AbortError') toast('Cannot play: ' + err.message, 'err');
  });
  if (prefOn('countplays')) {
    api('/api/played', { body: { id: track.id } }).catch(() => {});
    track.plays = (track.plays || 0) + 1;
  }
  renderNowPlaying();
  paintRows();
  if (prefOn('follow')) {
    const at = state.rows.findIndex(r => r.id === track.id);
    if (at >= 0) {
      const wrap = $('#list-wrap');
      const top = at * ROW_H, bottom = top + ROW_H;
      if (top < wrap.scrollTop || bottom > wrap.scrollTop + wrap.clientHeight) {
        wrap.scrollTop = top - wrap.clientHeight / 2 + ROW_H;
      }
    }
  }
  if (state.view === 'queue') renderSide();
  document.title = `${track.title} · ${track.artist} — Cadence`;
}

function next(auto) {
  if (player.repeat === 'one' && auto) { audio.currentTime = 0; audio.play(); return; }
  if (player.pos + 1 < player.order.length) return playAt(player.pos + 1);
  if (player.repeat === 'all' && player.order.length) return playAt(0);
  if (auto) stop();
}
function prev() {
  if (audio.currentTime > 3) { audio.currentTime = 0; return; }
  if (player.pos > 0) playAt(player.pos - 1);
  else audio.currentTime = 0;
}
function stop() {
  audio.pause(); audio.removeAttribute('src'); audio.load();
  player.current = null; player.pos = -1;
  renderNowPlaying(); paintRows();
  document.title = 'Cadence';
}
function togglePlay() {
  if (!player.current) {
    if (state.rows.length) {
      const start = state.sel.size ? Math.min(...state.sel) : 0;
      playQueue(state.rows.map(t => t.id), start);
    }
    return;
  }
  if (audio.paused) audio.play(); else audio.pause();
}

function renderNowPlaying() {
  const track = player.current;
  const art = $('#art');
  if (track) {
    $('#np-title').textContent = track.title;
    $('#np-artist').textContent = `${track.artist} — ${track.album}`;
    $('#st-now-label').textContent = `${track.artist} — ${track.title}`;
    $('#st-format-label').textContent = [
      track.codec, track.bitrate ? Math.round(track.bitrate / 1000) + ' kbps' : '',
      track.samplerate ? (track.samplerate / 1000).toFixed(1) + ' kHz' : '',
    ].filter(Boolean).join(' · ') || '-';
    art.innerHTML = track.has_art && prefOn('art')
      ? `<img src="${artUrl(track.id)}" alt="" onerror="this.remove()">`
      : icon('note');
    $('#t-end').textContent = fmtTime(track.duration);
  } else {
    $('#np-title').textContent = 'Nothing playing';
    $('#np-artist').textContent = 'Cadence';
    $('#st-now-label').textContent = 'No track';
    $('#st-format-label').textContent = '-';
    art.innerHTML = icon('note');
    $('#t-now').textContent = '0:00'; $('#t-end').textContent = '0:00';
    $('#seek .fill').style.width = '0%'; $('#seek .knob').style.left = '0%';
  }
  const playing = player.current && !audio.paused;
  $('#btn-play').innerHTML = icon(playing ? 'pause' : 'play');
  $('#st-play').innerHTML = icon(playing ? 'pause' : 'play') +
    `<span id="st-play-label">${playing ? 'Playing' : (player.current ? 'Paused' : 'Stopped')}</span>`;
  $('#btn-shuffle').classList.toggle('on', player.shuffle);
  $('#btn-repeat').classList.toggle('on', player.repeat !== 'off');
  $('#btn-repeat').title = `Repeat: ${player.repeat} (R)`;
  const badge = $('#queue-badge');
  badge.hidden = !player.queue.length;
  badge.textContent = player.queue.length > 99 ? '99+' : player.queue.length;
}

function updateStatus() {
  const total = state.tracks.length;
  $('#st-count-label').textContent = `${total} track${total === 1 ? '' : 's'}`;
  const sel = $('#st-sel');
  if (state.sel.size > 1) {
    sel.hidden = false;
    const seconds = Array.from(state.sel).reduce(
      (sum, i) => sum + ((state.rows[i] && state.rows[i].duration) || 0), 0);
    $('#st-sel-label').textContent = `${state.sel.size} selected · ${fmtSpan(seconds)}`;
  } else sel.hidden = true;
}

function setVolume(value) {
  player.volume = Math.max(0, Math.min(1, value));
  audio.volume = player.muted ? 0 : player.volume;
  $('#vol .fill').style.width = (player.muted ? 0 : player.volume * 100) + '%';
  $('#vol .knob').style.left = (player.muted ? 0 : player.volume * 100) + '%';
  $('#btn-vol').innerHTML = icon(player.muted || !player.volume ? 'mute' : 'vol');
  saveSetting('volume', String(player.volume));
}

/* ---------- panel ---------- */
let panelTab = 'queue';
function togglePanel(force) {
  const panel = $('#panel');
  const open = force == null ? !panel.classList.contains('open') : force;
  panel.classList.toggle('open', open);
  if (open) renderPanel();
}
function renderPanel() {
  $$('#panel-tabs .pt').forEach(b => b.classList.toggle('on', b.dataset.tab === panelTab));
  const body = $('#panel-body');
  if (panelTab === 'output') {
    body.innerHTML = `<pre id="output">${state.outputLog.map(esc).join('\n') ||
      'Output is quiet. Scan activity shows up here.'}</pre>`;
    body.scrollTop = body.scrollHeight;
  } else if (panelTab === 'details') {
    const t = player.current || (state.rows[state.anchor] || state.rows[0]);
    body.innerHTML = t ? `
      <div style="display:flex;gap:16px;padding-top:6px">
        <div style="width:132px;height:132px;flex:0 0 auto;background:var(--bg-input);border-radius:4px;
             display:grid;place-items:center;overflow:hidden">${t.has_art
          ? `<img src="${artUrl(t.id)}" style="width:100%;height:100%;object-fit:cover" alt="">`
          : icon('note')}</div>
        <div style="display:grid;grid-template-columns:auto 1fr;gap:3px 16px;align-content:start;
             font-size:12px;min-width:0">
          ${[['Title', t.title], ['Artist', t.artist], ['Album', t.album],
             ['Album artist', t.albumartist], ['Genre', t.genre || '—'],
             ['Year', t.year || '—'], ['Track', `${t.track || '—'}${t.disc ? ' (disc ' + t.disc + ')' : ''}`],
             ['Duration', fmtTime(t.duration)],
             ['Format', `${t.codec} · ${t.bitrate ? Math.round(t.bitrate / 1000) + ' kbps' : '?'} · ${
               t.samplerate ? (t.samplerate / 1000).toFixed(1) + ' kHz' : '?'} · ${
               t.channels === 1 ? 'mono' : t.channels === 2 ? 'stereo' : (t.channels || '?') + 'ch'}`],
             ['Size', fmtBytes(t.size)], ['Plays', t.plays || 0],
             ['Path', t.path]].map(([k, v]) =>
            `<span style="color:var(--fg-muted)">${esc(k)}</span>
             <span style="overflow:hidden;text-overflow:ellipsis;${k === 'Path'
               ? 'font-family:var(--mono);font-size:11px;word-break:break-all' : ''}">${esc(v)}</span>`).join('')}
        </div></div>` : '<div style="padding-top:10px;color:var(--fg-muted)">Nothing selected.</div>';
  } else {
    body.innerHTML = player.queue.length ? `<div style="padding-top:4px">${
      player.order.map((qi, i) => {
        const t = state.byId.get(player.queue[qi]); if (!t) return '';
        return `<div class="row" data-qjump="${qi}" style="height:22px;padding-left:4px">
          <span style="width:20px;color:var(--fg-faint);font-family:var(--mono);font-size:11px">${
            i === player.pos ? '▶' : i + 1}</span>
          <span class="label" style="${i === player.pos ? 'color:var(--a300);font-weight:600' : ''}">${
            esc(t.title)}</span>
          <span style="color:var(--fg-muted);padding-left:12px">${esc(t.artist)}</span>
          <span class="sub">${fmtTime(t.duration)}</span></div>`;
      }).join('')}</div>` :
      '<div style="padding-top:10px;color:var(--fg-muted)">Queue is empty.</div>';
  }
}

/* ---------- quick pick ---------- */
let quickMode = null, quickItems = [], quickIndex = 0;

function openQuick(mode) {
  quickMode = mode; quickIndex = 0;
  const input = $('#qp-input');
  input.value = mode === 'command' ? '>' : '';
  input.placeholder = mode === 'command'
    ? 'Type a command' : 'Search tracks by title, artist or album';
  $('#qp').classList.add('on'); $('#scrim').classList.add('on');
  input.focus(); input.setSelectionRange(input.value.length, input.value.length);
  refreshQuick();
}
function closeQuick() {
  quickMode = null;
  $('#qp').classList.remove('on'); $('#scrim').classList.remove('on');
  $('#list-wrap').focus();
}

function commandList() {
  return [
    { label: 'Music: Choose Folder…', run: () => pickFolder() },
    { label: 'Music: Rescan Folder', run: () => rescan(false) },
    { label: 'Music: Full Rescan (re-read every file)', run: () => rescan(true) },
    { label: 'View: Toggle Side Bar', key: 'Ctrl+B', run: () => toggleSide() },
    { label: 'View: Toggle Panel', key: 'Ctrl+J', run: () => togglePanel() },
    { label: 'View: Library', key: 'Ctrl+Shift+E', run: () => setView('library') },
    { label: 'View: Search', key: 'Ctrl+Shift+F', run: () => setView('search') },
    { label: 'View: Playlists', key: 'Ctrl+Shift+Y', run: () => setView('playlists') },
    { label: 'View: Queue', key: 'Ctrl+Shift+Q', run: () => setView('queue') },
    { label: 'View: Preferences…', run: () => openPrefs() },
    { label: 'View: All Settings', run: () => openSettingsTab() },
    { label: 'View: Toggle Theme', run: () => toggleTheme() },
    { label: 'Playback: Play / Pause', key: 'Space', run: () => togglePlay() },
    { label: 'Playback: Next Track', key: 'Ctrl+Right', run: () => next() },
    { label: 'Playback: Previous Track', key: 'Ctrl+Left', run: () => prev() },
    { label: 'Playback: Toggle Shuffle', key: 'S', run: () => toggleShuffle() },
    { label: 'Playback: Cycle Repeat', key: 'R', run: () => cycleRepeat() },
    { label: 'Playback: Stop', run: () => stop() },
    { label: 'Queue: Clear', run: () => { player.queue = []; player.order = []; stop(); renderSide(); } },
    { label: 'Queue: Shuffle Whole Library', run: () => {
        if (!state.tracks.length) return;
        player.shuffle = true;
        playQueue(state.tracks.map(t => t.id), Math.floor(Math.random() * state.tracks.length));
        renderNowPlaying();
      } },
    { label: 'Playlist: New Playlist…', run: () => newPlaylist() },
    { label: 'Help: About Cadence', run: () => openSettingsTab() },
  ];
}

function refreshQuick() {
  const raw = $('#qp-input').value;
  const box = $('#qp-results');
  if (quickMode === 'command' || raw.startsWith('>')) {
    const needle = raw.replace(/^>\s*/, '').toLowerCase();
    quickItems = commandList()
      .filter(c => c.label.toLowerCase().includes(needle))
      .map(c => ({ main: c.label, detail: c.key || '', run: c.run }));
  } else {
    const needle = raw.trim();
    const found = needle ? matchTracks(needle).slice(0, 60)
                         : state.tracks.slice(0, 60);
    quickItems = found.map(t => ({
      main: t.title, detail: `${t.artist} — ${t.album}`, id: t.id,
      run: () => playQueue(found.map(x => x.id), found.indexOf(t)),
    }));
  }
  quickIndex = Math.min(quickIndex, Math.max(0, quickItems.length - 1));
  box.innerHTML = quickItems.length ? quickItems.map((item, i) => `
    <div class="qi ${i === quickIndex ? 'on' : ''}" data-qi="${i}">
      ${icon(quickMode === 'command' || raw.startsWith('>') ? 'chev' : 'note')}
      <span class="m">${esc(item.main)}</span>
      <span class="d">${esc(item.detail)}</span></div>`).join('')
    : `<div class="none">No matching ${quickMode === 'command' ? 'commands' : 'tracks'}.</div>`;
  const active = $('#qp-results .qi.on');
  if (active) active.scrollIntoView({ block: 'nearest' });
}
function runQuick() {
  const item = quickItems[quickIndex];
  closeQuick();
  if (item) item.run();
}

/* ---------- context menu ---------- */
function showContext(x, y, items) {
  const menu = $('#ctx');
  menu.innerHTML = items.map(item => item === '-' ? '<hr>' :
    `<button data-ctx-run="${item.id}" ${item.disabled ? 'disabled' : ''}>
      <span>${esc(item.label)}</span>${item.key ? `<span class="key">${esc(item.key)}</span>` : ''}</button>`).join('');
  menu.classList.add('on');
  const rect = menu.getBoundingClientRect();
  menu.style.left = Math.min(x, innerWidth - rect.width - 6) + 'px';
  menu.style.top = Math.min(y, innerHeight - rect.height - 6) + 'px';
  menu._items = items;
}
const hideContext = () => $('#ctx').classList.remove('on');
"""


UI_SCRIPT_PREFS = r"""
/* ---------- preferences ---------- */
// Everything here is a plain on/off (or small value) stored server-side, so a
// choice survives a restart. Defaults reproduce the behaviour before the
// preferences panel existed.
const PREFS = [
  { key: 'follow',     label: 'Follow the playing track',
    hint: 'Scroll the list to keep the current track in view', def: true },
  { key: 'singleclick', label: 'Single click plays',
    hint: 'Otherwise a track starts on double click', def: false },
  { key: 'art',        label: 'Cover art in the player', def: true },
  { key: 'animate',    label: 'Animate the playing indicator', def: true },
  { key: 'compact',    label: 'Compact rows', hint: 'Tighter row height', def: false },
  { key: 'scanlaunch', label: 'Rescan the folder on launch', def: true },
  { key: 'countplays', label: 'Count plays', def: true },
  { key: 'wincontrols', label: "Cadence's own window controls",
    hint: 'Turn off if you would rather only have the ones Windows draws', def: true },
  { key: 'updates',    label: 'Check for updates',
    hint: 'Asks GitHub once a day whether a newer release exists', def: true },
];
const prefOn = key => {
  const entry = PREFS.find(p => p.key === key);
  const stored = state.prefs[key];
  return stored === undefined ? (entry ? entry.def : false) : stored === '1';
};
function setPref(key, on) {
  state.prefs[key] = on ? '1' : '0';
  saveSetting('pref_' + key, on ? '1' : '0');
  applyPrefs();
}
function applyPrefs() {
  const controls = $('#wincontrols');
  if (controls) controls.hidden = !prefOn('wincontrols');
  document.documentElement.style.setProperty('--row-h', prefOn('compact') ? '20px' : '24px');
  document.documentElement.classList.toggle('no-anim', !prefOn('animate'));
  ROW_H = prefOn('compact') ? 20 : 24;
  if (state.rows.length) renderList();
  renderNowPlaying();
}

/* ---------- equalizer ---------- */
// A ten-band peaking filter chain on the audio element. The graph is built the
// first time the equalizer is switched on: routing an element through Web Audio
// is one-way, so an untouched equalizer leaves playback completely alone.
const EQ_BANDS = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
const EQ_PRESETS = {
  'Flat':         [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  'Bass boost':   [6, 5, 4, 2, 0, 0, 0, 0, 0, 0],
  'Bass cut':     [-6, -5, -3, -1, 0, 0, 0, 0, 0, 0],
  'Vocal':        [-2, -2, 0, 2, 4, 4, 3, 1, 0, 0],
  'Treble boost': [0, 0, 0, 0, 0, 1, 3, 4, 5, 5],
  'Loudness':     [5, 4, 2, 0, -1, -1, 0, 2, 4, 5],
  'Radio':        [-4, -3, 0, 3, 4, 3, 2, 0, -2, -4],
};

const eq = { ctx: null, source: null, preampNode: null, filters: [], limiterNode: null,
             gains: EQ_BANDS.map(() => 0), preamp: 0, on: false, limiter: false };

function eqBuild() {
  if (eq.ctx) return true;
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) { toast('This browser has no Web Audio support', 'err'); return false; }
  try {
    eq.ctx = new Ctx();
    eq.source = eq.ctx.createMediaElementSource(audio);
    eq.preampNode = eq.ctx.createGain();
    eq.filters = EQ_BANDS.map((hz, i) => {
      const f = eq.ctx.createBiquadFilter();
      f.type = 'peaking';
      f.frequency.value = hz;
      f.Q.value = 1.1;
      f.gain.value = 0;
      return f;
    });
    eq.limiterNode = eq.ctx.createDynamicsCompressor();
    eq.limiterNode.threshold.value = -3;
    eq.limiterNode.knee.value = 0;
    eq.limiterNode.ratio.value = 20;
    eq.limiterNode.attack.value = 0.003;
    eq.limiterNode.release.value = 0.25;
    let node = eq.source.connect(eq.preampNode);
    eq.filters.forEach(f => { node = node.connect(f); });
    node.connect(eq.limiterNode).connect(eq.ctx.destination);
    // The limiter sits in the chain permanently; with ratio 20 and a -3 dB
    // threshold it only engages once something is actually pushed into it, and
    // switching it "off" simply moves the threshold above any real signal.
    eqApply();
    return true;
  } catch (err) {
    toast('Could not start the equalizer: ' + err.message, 'err');
    eq.ctx = null;
    return false;
  }
}

function eqApply() {
  if (!eq.ctx) return;
  if (eq.ctx.state === 'suspended') eq.ctx.resume().catch(() => {});
  eq.filters.forEach((f, i) => { f.gain.value = eq.on ? eq.gains[i] : 0; });
  eq.preampNode.gain.value = eq.on ? Math.pow(10, eq.preamp / 20) : 1;
  eq.limiterNode.threshold.value = eq.limiter ? -3 : 0;
  eq.limiterNode.ratio.value = eq.limiter ? 20 : 1;
}

function eqSave() {
  saveSetting('eq', JSON.stringify({ on: eq.on, limiter: eq.limiter,
                                     preamp: eq.preamp, gains: eq.gains }));
}
function eqLoad(raw) {
  if (!raw) return;
  try {
    const saved = JSON.parse(raw);
    eq.on = !!saved.on;
    eq.limiter = !!saved.limiter;
    eq.preamp = Number(saved.preamp) || 0;
    if (Array.isArray(saved.gains) && saved.gains.length === EQ_BANDS.length) {
      eq.gains = saved.gains.map(g => Math.max(-12, Math.min(12, Number(g) || 0)));
    }
  } catch { /* a corrupt setting just means defaults */ }
  if (eq.on) { eqBuild(); eqApply(); }
}

const hz = f => f >= 1000 ? (f / 1000) + 'k' : String(f);

// Drawn rather than a native vertical <input type=range>: those depend on
// browser-specific behaviour that has changed more than once, and where it is
// missing the control collapses to an unusably narrow horizontal slider. This
// behaves the same everywhere, and takes the keyboard too.
const EQ_RANGE = 12;

function faderValue(element, clientY) {
  const rect = element.getBoundingClientRect();
  const ratio = 1 - (clientY - rect.top) / rect.height;   // top of the track is the boost end
  const value = (ratio * 2 - 1) * EQ_RANGE;
  return Math.max(-EQ_RANGE, Math.min(EQ_RANGE, Math.round(value * 2) / 2));
}

function drawFader(element, value) {
  const fromTop = (1 - (value + EQ_RANGE) / (EQ_RANGE * 2)) * 100;
  element.querySelector('.knob').style.top = `calc(${fromTop}% - 4px)`;
  const fill = element.querySelector('.fill');
  // Fill from the midpoint towards the knob, so a cut reads as clearly as a boost.
  fill.style.top = Math.min(50, fromTop) + '%';
  fill.style.height = Math.abs(50 - fromTop) + '%';
  element.setAttribute('aria-valuenow', value);
}

function setBand(index, value) {
  eq.gains[index] = Math.max(-EQ_RANGE, Math.min(EQ_RANGE, value));
  const fader = $(`[data-eq-band="${index}"]`);
  if (fader) drawFader(fader, eq.gains[index]);
  const label = $(`[data-eq-db="${index}"]`);
  if (label) label.textContent = (eq.gains[index] > 0 ? '+' : '') + eq.gains[index];
  const preset = $('[data-eq-preset]');
  if (preset) preset.value = '';
  eqApply();
}

function drawAllFaders() {
  EQ_BANDS.forEach((_, i) => {
    const fader = $(`[data-eq-band="${i}"]`);
    if (fader) drawFader(fader, eq.gains[i]);
  });
}

/* ---------- preferences popup ---------- */
function prefsHtml() {
  const theme = document.documentElement.dataset.theme || 'dark';
  const themes = THEMES.map(t => [t, THEME_NAMES[t]]);
  const update = state.update || {};
  return `
    <div class="prefcols">
      <section>
        <h4>${icon('sliders')} Equalizer</h4>
        <label class="sw"><input type="checkbox" data-pref-eq="on" ${eq.on ? 'checked' : ''}>
          <span>Enable equalizer</span></label>
        <div class="eq ${eq.on ? '' : 'off'}">
          <div class="eqrow">
            ${EQ_BANDS.map((f, i) => `
              <div class="band">
                <div class="fader" data-eq-band="${i}" tabindex="0" role="slider"
                     aria-label="${hz(f)} hertz" aria-valuemin="-12" aria-valuemax="12"
                     aria-valuenow="${eq.gains[i]}">
                  <div class="fill"></div><div class="knob"></div>
                </div>
                <span class="db" data-eq-db="${i}">${eq.gains[i] > 0 ? '+' : ''}${eq.gains[i]}</span>
                <span class="hz">${hz(f)}</span>
              </div>`).join('')}
          </div>
          <div class="eqfoot">
            <label>Preset
              <select data-eq-preset>
                <option value="">Custom</option>
                ${Object.keys(EQ_PRESETS).map(n => `<option value="${esc(n)}">${esc(n)}</option>`).join('')}
              </select></label>
            <label>Preamp
              <input type="range" data-eq-preamp min="-12" max="12" step="0.5" value="${eq.preamp}">
              <span data-eq-preamp-db>${eq.preamp > 0 ? '+' : ''}${eq.preamp} dB</span></label>
          </div>
          <label class="sw"><input type="checkbox" data-pref-eq="limiter" ${eq.limiter ? 'checked' : ''}>
            <span>Limiter <em>stops boosted bands from clipping</em></span></label>
        </div>
      </section>

      <section>
        <h4>${icon('settings')} Behaviour</h4>
        ${PREFS.map(p => `
          <label class="sw"><input type="checkbox" data-pref="${p.key}" ${prefOn(p.key) ? 'checked' : ''}>
            <span>${esc(p.label)}${p.hint ? ` <em>${esc(p.hint)}</em>` : ''}</span></label>`).join('')}
      </section>

      <section>
        <h4>${icon('library')} Track list columns</h4>
        <div class="checks">${OPTIONAL_COLUMNS.map(c => `
          <label><input type="checkbox" data-col="${c.key}"
            ${state.columns.includes(c.key) ? 'checked' : ''}> ${esc(c.label)}</label>`).join('')}
        </div>
        <div class="hint" style="margin-top:6px">Number and Title always stay. A hidden
        tag is still read and still searchable, and the Details panel keeps showing
        everything for the selected track.</div>
      </section>

      <section>
        <h4>${icon('star')} Appearance</h4>
        <label class="pick">Theme
          <select data-theme-pick>
            ${themes.map(([v, n]) =>
              `<option value="${v}" ${v === theme ? 'selected' : ''}>${n}</option>`).join('')}
          </select></label>
        <label class="pick">Accent
          <select data-accent-pick>
            ${ACCENTS.map(([n]) => `<option value="${esc(n)}"
              ${n === ((state.info.settings || {}).accent || 'Deep Blue') ? 'selected' : ''}>${esc(n)}</option>`).join('')}
          </select></label>
        <div class="swatches" style="margin-top:8px">${ACCENTS.map(([name, ramp]) => {
          const stops = ramp.split(',');
          return `<button class="swatch ${name === ((state.info.settings || {}).accent || 'Deep Blue') ? 'on' : ''}"
            data-accent="${esc(name)}" title="${esc(name)}"
            style="background:linear-gradient(135deg,${stops[3]},${stops[5]})"></button>`;
        }).join('')}</div>
      </section>

      <section>
        <h4>${icon('download')} Updates</h4>
        <div class="updbox" id="updbox">${updateHtml(update)}</div>
      </section>
    </div>`;
}

function updateHtml(update) {
  if (update.checking) return `<div class="hint">Checking GitHub…</div>`;
  if (update.error) return `<div class="hint" style="color:var(--err)">${esc(update.error)}</div>
    <button class="btn quiet" data-act="check-update">Try again</button>`;
  if (!update.checked) {
    return `<div class="hint">Running version ${esc(state.info.version)}.</div>
      <button class="btn quiet" data-act="check-update">Check now</button>`;
  }
  if (!update.available) {
    return `<div class="hint">${icon('check')} Version ${esc(state.info.version)} is the latest.</div>
      <button class="btn quiet" data-act="check-update">Check again</button>`;
  }
  return `<div class="hint"><b>Version ${esc(update.latest)}</b> is available
      (you have ${esc(state.info.version)}).</div>
    ${update.notes ? `<div class="relnotes">${esc(update.notes).slice(0, 400)}</div>` : ''}
    <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:8px">
      ${update.frozen && update.asset
        ? `<button class="btn" data-act="install-update">Download and install</button>` : ''}
      <a class="btn quiet" href="${esc(update.url)}" target="_blank" rel="noreferrer noopener">
        Open release page</a>
    </div>
    ${update.frozen && update.asset
      ? `<div class="hint" style="margin-top:8px">Installs beside the running program and keeps
         the current version as <code>Cadence-previous.exe</code>.</div>` : ''}`;
}

function openPrefs() {
  showModal('Preferences', prefsHtml(),
    `<span class="note">Cadence ${esc(state.info.version)}</span>
     <button class="btn quiet" data-act="all-settings">All settings…</button>
     <button class="btn" data-act="modal-close">Done</button>`);
  $('#modal').classList.add('wide');
  drawAllFaders();
  if (prefOn('updates')) checkUpdate(true);
}

async function checkUpdate(quiet) {
  state.update = Object.assign({}, state.update, { checking: true });
  const box = $('#updbox');
  if (box) box.innerHTML = updateHtml(state.update);
  try {
    const result = await api('/api/update/check' + (quiet ? '?cached=1' : ''));
    state.update = Object.assign({ checked: true, checking: false }, result);
  } catch (err) {
    state.update = { checking: false, error: err.message };
  }
  const after = $('#updbox');
  if (after) after.innerHTML = updateHtml(state.update);
  const chip = $('#st-update');
  if (chip) {
    chip.hidden = !state.update.available;
    if (state.update.available) $('#st-update-label').textContent = 'Update ' + state.update.latest;
  }
}
"""


UI_SCRIPT_WIRE = r"""
/* ---------- actions ---------- */
function setView(view) {
  state.view = view;
  $$('.act').forEach(b => b.classList.toggle('on', b.dataset.view === view));
  if (view === 'settings') { openPrefs(); return; }   // the cog is a popup now
  $('#side').classList.remove('hidden');
  renderSide();
}
function toggleSide() { $('#side').classList.toggle('hidden'); }
const THEMES = ['dark', 'black', 'nord', 'gruvbox', 'solarized-dark',
               'light', 'paper', 'solarized-light'];
const THEME_NAMES = {
  'dark': 'Dark', 'black': 'Black', 'nord': 'Nord', 'gruvbox': 'Gruvbox',
  'solarized-dark': 'Solarized Dark', 'light': 'Light', 'paper': 'Paper',
  'solarized-light': 'Solarized Light',
};
function toggleTheme() {
  const current = document.documentElement.dataset.theme || 'dark';
  setTheme(THEMES[(THEMES.indexOf(current) + 1) % THEMES.length]);
}
function setTheme(theme) {
  applyTheme(theme); saveSetting('theme', theme);
  if (currentTab() && currentTab().id === 'settings') openSettingsTab();
}
function applyTheme(theme) {
  if (!THEMES.includes(theme)) theme = 'dark';
  document.documentElement.dataset.theme = theme;
  $('#st-theme-label').textContent = THEME_NAMES[theme];
}
function applyAccent(name) {
  const entry = ACCENTS.find(a => a[0] === name) || ACCENTS[0];
  const stops = entry[1].split(',');
  ['--a900', '--a800', '--a700', '--a600', '--a500', '--a400', '--a300']
    .forEach((token, i) => document.documentElement.style.setProperty(token, stops[i]));
  document.documentElement.style.setProperty('--sel', stops[1]);
}
const saveSetting = (key, value) => api('/api/setting', { body: { key, value } }).catch(() => {});

function toggleShuffle() {
  player.shuffle = !player.shuffle;
  if (player.queue.length) {
    const playingIndex = player.pos >= 0 ? player.order[player.pos] : 0;
    buildOrder(playingIndex);
  }
  renderNowPlaying();
  if (state.view === 'queue') renderSide();
  toast(player.shuffle ? 'Shuffle on' : 'Shuffle off');
}
function cycleRepeat() {
  player.repeat = { off: 'all', all: 'one', one: 'off' }[player.repeat];
  renderNowPlaying();
  toast(`Repeat: ${player.repeat}`);
}
function openSettingsTab() {
  openTab({ id: 'settings', kind: 'custom', icon: 'settings', title: 'Settings',
            crumbs: ['Cadence', 'Settings'], html: settingsTabHtml() });
}
function openWelcome() {
  openTab({ id: 'welcome', kind: 'custom', icon: 'note', title: 'Welcome',
            crumbs: ['Cadence'], html: welcomeHtml() });
}

const SOURCES = {
  all:      () => ({ id: 'all', kind: 'all', icon: 'library', title: 'All Tracks', crumbs: ['Library', 'All Tracks'] }),
  recent:   () => ({ id: 'recent', kind: 'recent', icon: 'clock', title: 'Recently Added', crumbs: ['Library', 'Recently Added'] }),
  played:   () => ({ id: 'played', kind: 'played', icon: 'star', title: 'Most Played', crumbs: ['Library', 'Most Played'] }),
  artist:   v => ({ id: 'ar:' + v, kind: 'artist', value: v, icon: 'person', title: v, crumbs: ['Library', 'Artists', v] }),
  album:    (v, artist) => ({ id: 'al:' + (artist || '') + ':' + v, kind: 'album', value: v, artist,
                              icon: 'disc', title: v, crumbs: ['Library', artist || 'Albums', v].filter(Boolean) }),
  genre:    v => ({ id: 'gn:' + v, kind: 'genre', value: v, icon: 'note', title: v, crumbs: ['Library', 'Genres', v] }),
  playlist: v => { const p = state.playlists.find(x => x.id === +v) || { name: 'Playlist' };
                   return { id: 'pl:' + v, kind: 'playlist', value: +v, icon: 'playlist',
                            title: p.name, crumbs: ['Playlists', p.name] }; },
  queue:    () => ({ id: 'queue', kind: 'queue', icon: 'queue', title: 'Queue', crumbs: ['Queue'] }),
};

function selectedTracks() {
  return Array.from(state.sel).sort((a, b) => a - b).map(i => state.rows[i]).filter(Boolean);
}

async function refreshTracks() {
  const data = await api('/api/tracks');
  state.tracks = data.tracks;
  state.byId = new Map(state.tracks.map(t => [t.id, t]));
  state.info = await api('/api/state');
  renderSide(); renderList(); updateStatus();
}
async function refreshPlaylists() {
  const data = await api('/api/playlists');
  state.playlists = data.playlists;
  if (state.view === 'playlists') renderSide();
}

async function rescan(full) {
  try {
    await api('/api/scan', { body: { full: !!full } });
    out(full ? 'Full rescan started' : 'Rescan started');
    watchScan();
  } catch (err) { toast(err.message, 'err'); }
}

function watchScan() {
  clearInterval(state.scanTimer);
  $('#st-scan').hidden = false;
  let seen = 0;
  state.scanTimer = setInterval(async () => {
    let status;
    try { status = await api('/api/scan/status'); } catch { return; }
    (status.log || []).slice(seen).forEach(line => out(line));
    seen = (status.log || []).length;
    $('#st-scan-label').textContent = status.running
      ? `Scanning ${status.found} file${status.found === 1 ? '' : 's'}` : 'Scanning';
    if (!status.running) {
      clearInterval(state.scanTimer);
      $('#st-scan').hidden = true;
      await refreshTracks();
      // After a first successful scan, drop the user straight into the library
      // instead of leaving them on the welcome page they have finished with.
      const tab = currentTab();
      if (state.tracks.length && (!tab || tab.id === 'welcome')) {
        closeTab('welcome');
        openTab(SOURCES.all());
        setView('library');
      }
      if (status.error) toast(status.error, 'err');
      else toast(`Scan finished · ${status.added} added, ${status.updated} updated`, 'ok');
    }
  }, 600);
}

/* ---------- modals ---------- */
function closeModal() {
  $('#modal').classList.remove('on', 'wide'); $('#scrim').classList.remove('on');
}
function showModal(title, bodyHtml, footHtml) {
  $('#modal-title').textContent = title;
  $('#modal-body').innerHTML = bodyHtml;
  $('#modal-foot').innerHTML = footHtml;
  $('#modal').classList.add('on'); $('#scrim').classList.add('on');
}

let pickPath = '';
async function pickFolder(startAt) {
  const data = await api('/api/browse?path=' + encodeURIComponent(startAt || pickPath ||
    (state.info && state.info.folder) || ''));
  pickPath = data.path || '';
  const crumbs = [];
  if (pickPath) {
    const parts = pickPath.split(/[\\/]/).filter(Boolean);
    let acc = pickPath.startsWith('/') ? '' : '';
    parts.forEach((part, i) => {
      acc = pickPath.startsWith('/') ? acc + '/' + part
                                     : (i === 0 ? part + '\\' : acc + part + '\\');
      crumbs.push(`<button data-pick="${esc(acc)}">${esc(part)}</button><span>›</span>`);
    });
  }
  const rows = (data.places || []).map(p =>
      `<div class="row" data-pick="${esc(p.path)}">${icon('pin')}<span class="label">${esc(p.name)}</span></div>`)
    .concat(data.parent ? [`<div class="row" data-pick="${esc(data.parent)}">${icon('folder')}
      <span class="label">..</span></div>`] : [])
    .concat((data.dirs || []).map(d =>
      `<div class="row" data-pick="${esc(d.path)}">${icon('folder')}<span class="label">${esc(d.name)}</span></div>`));

  showModal('Designate music folder',
    `${pickPath ? `<div class="crumbbar"><button data-pick="">${icon('pin')}</button>${crumbs.join('')}</div>` : ''}
     ${data.error ? `<div style="color:var(--err);margin-bottom:10px">${esc(data.error)}</div>` : ''}
     <div class="dirlist">${rows.join('') ||
       '<div style="padding:12px;color:var(--fg-muted)">No sub-folders here.</div>'}</div>
     <div class="hint" style="margin-top:10px">Cadence indexes the folder you pick and everything
     beneath it. Files are only read, never changed.</div>`,
    `<span class="note">${esc(pickPath || 'Pick a location')}${
       data.audio ? ` · ${data.audio} audio file${data.audio === 1 ? '' : 's'} here` : ''}</span>
     <button class="btn quiet" data-act="modal-close">Cancel</button>
     <button class="btn" data-act="use-folder" ${pickPath ? '' : 'disabled style="opacity:.4"'}>
       Use This Folder</button>`);
}

async function useFolder() {
  try {
    await api('/api/folder', { body: { path: pickPath } });
    closeModal();
    out(`Music folder set to ${pickPath}`);
    state.info = await api('/api/state');
    watchScan();
  } catch (err) { toast(err.message, 'err'); }
}

function newPlaylist(seedIds) {
  showModal('New playlist',
    `<div class="field"><label>Name</label>
      <input id="pl-name" placeholder="Late night" autocomplete="off">
      <div class="hint">${seedIds && seedIds.length
        ? `${seedIds.length} selected track${seedIds.length === 1 ? '' : 's'} will be added.`
        : 'Starts empty — add tracks by right-clicking a selection.'}</div></div>`,
    `<button class="btn quiet" data-act="modal-close">Cancel</button>
     <button class="btn" data-act="create-playlist">Create</button>`);
  const input = $('#pl-name');
  input.focus();
  input._seed = seedIds || [];
  input.addEventListener('keydown', e => { if (e.key === 'Enter') createPlaylist(); });
}
async function createPlaylist() {
  const input = $('#pl-name');
  const name = (input.value || '').trim();
  if (!name) { input.focus(); return; }
  try {
    const res = await api('/api/playlist/create', { body: { name, tracks: input._seed || [] } });
    closeModal(); await refreshPlaylists();
    setView('playlists'); openTab(SOURCES.playlist(res.id));
    toast(`Playlist "${name}" created`, 'ok');
  } catch (err) { toast(err.message, 'err'); }
}

function addToPlaylistMenu(x, y, tracks) {
  const ids = tracks.map(t => t.id);
  const items = [{ id: 'pl-new', label: 'New Playlist…' }];
  if (state.playlists.length) items.push('-');
  state.playlists.forEach(p => items.push({ id: 'pl-' + p.id, label: p.name }));
  showContext(x, y, items);
  $('#ctx')._handler = id => {
    if (id === 'pl-new') return newPlaylist(ids);
    const playlistId = +id.slice(3);
    api('/api/playlist/add', { body: { id: playlistId, tracks: ids } })
      .then(res => { refreshPlaylists(); toast(`${res.added} added`, 'ok'); })
      .catch(err => toast(err.message, 'err'));
  };
}

/* ---------- menus ---------- */
const MENUS = {
  file: [
    { label: 'Choose Music Folder…', act: () => pickFolder() },
    { label: 'Rescan Folder', key: 'F5', act: () => rescan(false) },
    { label: 'Full Rescan', act: () => rescan(true) },
    '-',
    { label: 'New Playlist…', act: () => newPlaylist() },
    { label: 'Export Current View as .m3u8', act: () => exportCurrent() },
    '-',
    { label: 'Exit', act: () => quitApp() },
  ],
  edit: [
    { label: 'Find', key: 'Ctrl+Shift+F', act: () => setView('search') },
    { label: 'Quick Open', key: 'Ctrl+P', act: () => openQuick('file') },
    { label: 'Command Palette', key: 'Ctrl+Shift+P', act: () => openQuick('command') },
    '-',
    { label: 'Select All', key: 'Ctrl+A', act: () => selectAll() },
    { label: 'Copy Path', key: 'Ctrl+C', act: () => copyPaths() },
  ],
  view: [
    { label: 'Toggle Side Bar', key: 'Ctrl+B', act: () => toggleSide() },
    { label: 'Toggle Panel', key: 'Ctrl+J', act: () => togglePanel() },
    '-',
    { label: 'Library', key: 'Ctrl+Shift+E', act: () => setView('library') },
    { label: 'Search', key: 'Ctrl+Shift+F', act: () => setView('search') },
    { label: 'Playlists', key: 'Ctrl+Shift+Y', act: () => setView('playlists') },
    { label: 'Queue', key: 'Ctrl+Shift+Q', act: () => setView('queue') },
    '-',
    { label: 'Toggle Theme', act: () => toggleTheme() },
    { label: 'Preferences…', act: () => openPrefs() },
    { label: 'All Settings', act: () => openSettingsTab() },
  ],
  play: [
    { label: 'Play / Pause', key: 'Space', act: () => togglePlay() },
    { label: 'Next Track', key: 'Ctrl+Right', act: () => next() },
    { label: 'Previous Track', key: 'Ctrl+Left', act: () => prev() },
    { label: 'Stop', act: () => stop() },
    '-',
    { label: 'Shuffle', key: 'S', act: () => toggleShuffle() },
    { label: 'Repeat', key: 'R', act: () => cycleRepeat() },
    { label: 'Mute', key: 'M', act: () => { player.muted = !player.muted; setVolume(player.volume); } },
    '-',
    { label: 'Shuffle Whole Library', act: () => {
        if (!state.tracks.length) return toast('Library is empty');
        player.shuffle = true;
        playQueue(state.tracks.map(t => t.id), Math.floor(Math.random() * state.tracks.length));
      } },
  ],
  help: [
    { label: 'About Cadence', act: () => openSettingsTab() },
    { label: 'Keyboard Shortcuts', act: () => openSettingsTab() },
    { label: 'Welcome', act: () => openWelcome() },
  ],
};

function buildMenus() {
  for (const [name, items] of Object.entries(MENUS)) {
    $('#menu-' + name).innerHTML = items.map((item, i) => item === '-' ? '<hr>' :
      `<button data-menu-run="${name}:${i}"><span>${esc(item.label)}</span>${
        item.key ? `<span class="key">${esc(item.key)}</span>` : ''}</button>`).join('');
  }
}
const closeMenus = () => $$('.menu').forEach(m => m.classList.remove('open'));

function selectAll() {
  state.sel = new Set(state.rows.map((_, i) => i));
  paintRows(); updateStatus();
}
function copyPaths() {
  const paths = selectedTracks().map(t => t.path).join('\n');
  if (!paths) return;
  navigator.clipboard.writeText(paths)
    .then(() => toast('Path copied', 'ok'))
    .catch(() => toast('Clipboard blocked by the browser', 'err'));
}
function exportCurrent() {
  if (!state.rows.length) return toast('Nothing to export');
  const tab = currentTab();
  const ids = (state.sel.size ? selectedTracks() : state.rows).map(t => t.id).join(',');
  const url = `/api/export?ids=${ids}&name=${encodeURIComponent(tab ? tab.title : 'playlist')}` +
              `&t=${encodeURIComponent(state.token)}`;
  const link = document.createElement('a');
  link.href = url; link.download = ''; document.body.appendChild(link); link.click(); link.remove();
}
async function quitApp() {
  clearInterval(heartbeatTimer);
  await api('/api/quit', { body: {} }).catch(() => {});
  try { window.close(); } catch { /* the browser may refuse; the page below says so */ }
  document.body.innerHTML =
    `<div class="empty" style="height:100vh"><h2>Cadence has stopped</h2>
     <p>The server was shut down. You can close this window.</p></div>`;
}

/* ---------- events ---------- */
function wire() {
  $('#app-icon').src = '/icon.svg';
  buildMenus();

  document.addEventListener('click', async e => {
    const menuButton = e.target.closest('.menu>button');
    if (menuButton) {
      const menu = menuButton.parentElement;
      const wasOpen = menu.classList.contains('open');
      closeMenus();
      if (!wasOpen) menu.classList.add('open');
      e.stopPropagation();
      return;
    }
    const menuRun = e.target.closest('[data-menu-run]');
    if (menuRun) {
      const [name, index] = menuRun.dataset.menuRun.split(':');
      closeMenus(); MENUS[name][+index].act();
      return;
    }
    if (!e.target.closest('#ctx')) { closeMenus(); }

    const ctxRun = e.target.closest('[data-ctx-run]');
    if (ctxRun) {
      const id = ctxRun.dataset.ctxRun;
      const handler = $('#ctx')._handler;
      hideContext();
      if (handler) handler(id);
      return;
    }
    hideContext();

    const act = e.target.closest('[data-act]');
    if (act) {
      const name = act.dataset.act;
      if (name === 'pick-folder') pickFolder();
      else if (name === 'rescan') rescan(false);
      else if (name === 'rescan-full') rescan(true);
      else if (name === 'modal-close') closeModal();
      else if (name === 'use-folder') useFolder();
      else if (name === 'create-playlist') createPlaylist();
      else if (name === 'new-playlist') newPlaylist();
      else if (name === 'open-all') openTab(SOURCES.all());
      else if (name === 'settings-tab') openSettingsTab();
      else if (name === 'clear-queue') { player.queue = []; player.order = []; stop(); renderSide(); }
      else if (name === 'all-settings') { closeModal(); openSettingsTab(); }
      else if (name === 'check-update') checkUpdate(false);
      else if (name === 'install-update') installUpdate();
      return;
    }

    const themeSet = e.target.closest('[data-theme-set]');
    if (themeSet) {
      applyTheme(themeSet.dataset.themeSet);
      saveSetting('theme', themeSet.dataset.themeSet);
      openSettingsTab();
      return;
    }
    const accentSet = e.target.closest('[data-accent]');
    if (accentSet) {
      applyAccent(accentSet.dataset.accent);
      saveSetting('accent', accentSet.dataset.accent);
      state.info.settings = Object.assign({}, state.info.settings, { accent: accentSet.dataset.accent });
      openSettingsTab();
      return;
    }
    const pick = e.target.closest('[data-pick]');
    if (pick) { pickFolder(pick.dataset.pick); return; }

    const activity = e.target.closest('.act');
    if (activity) { setView(activity.dataset.view); return; }

    const tabClose = e.target.closest('[data-close]');
    if (tabClose) { closeTab(tabClose.dataset.close); return; }
    const tab = e.target.closest('.tab');
    if (tab) { state.activeTab = tab.dataset.id; state.sel.clear(); renderTabs(); renderList(); return; }

    const sectionHead = e.target.closest('.section>.head');
    if (sectionHead) { sectionHead.parentElement.classList.toggle('collapsed'); return; }

    const jump = e.target.closest('[data-qjump]');
    if (jump) {
      const at = player.order.indexOf(+jump.dataset.qjump);
      if (at >= 0) playAt(at);
      renderSide(); if ($('#panel').classList.contains('open')) renderPanel();
      return;
    }

    const twist = e.target.closest('[data-twist]');
    const open = e.target.closest('[data-open]');
    if (twist && e.target.closest('.twist')) {
      const key = twist.dataset.twist;
      state.expanded.has(key) ? state.expanded.delete(key) : state.expanded.add(key);
      renderSide();
      return;
    }
    if (open) {
      const kind = open.dataset.open;
      if (twist && !state.expanded.has(twist.dataset.twist)) state.expanded.add(twist.dataset.twist);
      const maker = SOURCES[kind];
      if (maker) openTab(maker(open.dataset.value, open.dataset.artist));
      renderSide();
      return;
    }

    const quickItem = e.target.closest('[data-qi]');
    if (quickItem) { quickIndex = +quickItem.dataset.qi; runQuick(); return; }

    const column = e.target.closest('[data-sort]');
    if (column) {
      const key = column.dataset.sort;
      if (key === 'index') { state.sort = { key: 'default', dir: 1 }; }
      else if (state.sort.key === key) state.sort.dir *= -1;
      else state.sort = { key, dir: 1 };
      state.sel.clear(); renderList();
      return;
    }
  });

  document.addEventListener('pointerdown', e => {
    const fader = e.target.closest('[data-eq-band]');
    if (!fader) return;
    e.preventDefault();
    fader.focus();
    const index = +fader.dataset.eqBand;
    const track = value => setBand(index, value);
    track(faderValue(fader, e.clientY));
    fader.setPointerCapture(e.pointerId);
    const move = ev => track(faderValue(fader, ev.clientY));
    const up = () => {
      fader.removeEventListener('pointermove', move);
      fader.removeEventListener('pointerup', up);
      fader.removeEventListener('pointercancel', up);
      eqSave();
    };
    fader.addEventListener('pointermove', move);
    fader.addEventListener('pointerup', up);
    fader.addEventListener('pointercancel', up);
  });

  document.addEventListener('keydown', e => {
    const fader = e.target.closest && e.target.closest('[data-eq-band]');
    if (!fader) return;
    const index = +fader.dataset.eqBand;
    const step = e.shiftKey ? 0.5 : 1;
    const moves = { ArrowUp: step, ArrowDown: -step, PageUp: 3, PageDown: -3 };
    if (e.key in moves) { setBand(index, eq.gains[index] + moves[e.key]); }
    else if (e.key === 'Home') { setBand(index, EQ_RANGE); }
    else if (e.key === 'End') { setBand(index, -EQ_RANGE); }
    else if (e.key === '0') { setBand(index, 0); }
    else return;
    e.preventDefault(); e.stopPropagation();
    eqSave();
  }, true);

  document.addEventListener('input', e => {
    const pre = e.target.closest('[data-eq-preamp]');
    if (pre) {
      eq.preamp = parseFloat(pre.value);
      const label = $('[data-eq-preamp-db]');
      if (label) label.textContent = (eq.preamp > 0 ? '+' : '') + eq.preamp + ' dB';
      eqApply(); eqSave();
    }
  });

  document.addEventListener('change', e => {
    const eqBox = e.target.closest('[data-pref-eq]');
    if (eqBox) {
      const which = eqBox.dataset.prefEq;
      if (which === 'on') {
        eq.on = eqBox.checked;
        if (eq.on && !eqBuild()) { eq.on = false; eqBox.checked = false; }
        const panel = $('.eq');
        if (panel) panel.classList.toggle('off', !eq.on);
      } else {
        eq.limiter = eqBox.checked;
        if (eq.limiter) eqBuild();
      }
      eqApply(); eqSave();
      return;
    }
    const preset = e.target.closest('[data-eq-preset]');
    if (preset) {
      const chosen = EQ_PRESETS[preset.value];
      if (chosen) {
        chosen.forEach((g, i) => setBand(i, g));
        preset.value = preset.value;   // setBand clears it; this is a deliberate choice
        eqSave();
      }
      return;
    }
    const pref = e.target.closest('[data-pref]');
    if (pref) { setPref(pref.dataset.pref, pref.checked); return; }
    const themePick = e.target.closest('[data-theme-pick]');
    if (themePick) { setTheme(themePick.value); return; }
    const accentPick = e.target.closest('[data-accent-pick]');
    if (accentPick) {
      applyAccent(accentPick.value);
      saveSetting('accent', accentPick.value);
      state.info.settings = Object.assign({}, state.info.settings, { accent: accentPick.value });
      $$('.swatch').forEach(s => s.classList.toggle('on', s.dataset.accent === accentPick.value));
      return;
    }
    const box = e.target.closest('[data-col]');
    if (!box) return;
    setColumn(box.dataset.col, box.checked);
    // The same tick appears in the popup and the settings tab; mirror it to
    // whichever copy the user is not currently looking at.
    $$(`[data-col="${box.dataset.col}"]`).forEach(other => { other.checked = box.checked; });
    // The box the user just clicked already shows the right state, so redrawing
    // the page would only throw away their scroll position and detach the other
    // boxes mid-interaction. Refresh the cached markup instead; track list tabs
    // rebuild their columns when they are next shown.
    const tab = state.tabs.find(t => t.id === 'settings');
    if (tab) tab.html = settingsTabHtml();
  });

  $('#win-close').addEventListener('click', quitApp);
  $('#win-max').addEventListener('click', () => {
    const root = document.documentElement;
    if (root.requestFullscreen) root.requestFullscreen().catch(() => {});
    syncWindowControls();
  });
  $('#win-restore').addEventListener('click', () => {
    if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
    syncWindowControls();
  });
  document.addEventListener('fullscreenchange', syncWindowControls);
  syncWindowControls();

  $('#command-center').addEventListener('click', () => openQuick('file'));
  $('#scrim').addEventListener('click', () => { closeQuick(); closeModal(); });
  $('#st-theme').addEventListener('click', toggleTheme);
  $('#st-update').addEventListener('click', openPrefs);
  $('#st-version').addEventListener('click', openPrefs);
  $('#activity .act[data-view="settings"]').title = 'Preferences';
  $('#st-play').addEventListener('click', togglePlay);
  $('#st-count').addEventListener('click', () => openTab(SOURCES.all()));
  $('#st-now').addEventListener('click', () => { setView('queue'); togglePanel(true); });
  $('#st-scan').addEventListener('click', () => { togglePanel(true); panelTab = 'output'; renderPanel(); });
  $('#st-sel').addEventListener('click', () => { togglePanel(true); panelTab = 'details'; renderPanel(); });
  $('#st-format').addEventListener('click', () => { togglePanel(true); panelTab = 'details'; renderPanel(); });

  $('#btn-play').addEventListener('click', togglePlay);
  $('#btn-next').addEventListener('click', () => next());
  $('#btn-prev').addEventListener('click', prev);
  $('#btn-shuffle').addEventListener('click', toggleShuffle);
  $('#btn-repeat').addEventListener('click', cycleRepeat);
  $('#btn-vol').addEventListener('click', () => { player.muted = !player.muted; setVolume(player.volume); });
  $('#panel-close').addEventListener('click', () => togglePanel(false));
  $$('#panel-tabs .pt').forEach(button =>
    button.addEventListener('click', () => { panelTab = button.dataset.tab; renderPanel(); }));

  // list interaction
  const wrap = $('#list-wrap');
  wrap.addEventListener('scroll', () => { if (state.rows.length) paintRows(); });
  wrap.addEventListener('mousedown', e => {
    const row = e.target.closest('.trk');
    if (!row) { if (!e.shiftKey && !e.ctrlKey) { state.sel.clear(); paintRows(); updateStatus(); } return; }
    const index = +row.dataset.i;
    if (e.shiftKey && state.anchor >= 0) {
      state.sel.clear();
      for (let i = Math.min(index, state.anchor); i <= Math.max(index, state.anchor); i++) state.sel.add(i);
    } else if (e.ctrlKey || e.metaKey) {
      state.sel.has(index) ? state.sel.delete(index) : state.sel.add(index);
      state.anchor = index;
    } else {
      if (!(e.button === 2 && state.sel.has(index))) { state.sel.clear(); state.sel.add(index); }
      state.anchor = index;
    }
    paintRows(); updateStatus();
    if ($('#panel').classList.contains('open') && panelTab === 'details') renderPanel();
    if (prefOn('singleclick') && e.button === 0 && !e.shiftKey && !e.ctrlKey && !e.metaKey) {
      playQueue(state.rows.map(t => t.id), index);
    }
  });
  wrap.addEventListener('dblclick', e => {
    const row = e.target.closest('.trk');
    if (!row) return;
    playQueue(state.rows.map(t => t.id), +row.dataset.i);
  });
  wrap.addEventListener('contextmenu', e => {
    const row = e.target.closest('.trk');
    if (!row) return;
    e.preventDefault();
    const tracks = selectedTracks();
    const tab = currentTab();
    const many = tracks.length > 1;
    showContext(e.clientX, e.clientY, [
      { id: 'play', label: many ? `Play ${tracks.length} Tracks` : 'Play', key: 'Enter' },
      { id: 'queue', label: 'Add to Queue' },
      { id: 'next', label: 'Play Next' },
      '-',
      { id: 'addpl', label: 'Add to Playlist…' },
      ...(tab && tab.kind === 'playlist' ? [{ id: 'rmpl', label: 'Remove from Playlist', key: 'Del' }] : []),
      ...(tab && tab.kind === 'queue' ? [{ id: 'rmq', label: 'Remove from Queue', key: 'Del' }] : []),
      '-',
      { id: 'album', label: 'Go to Album', disabled: many },
      { id: 'artist', label: 'Go to Artist', disabled: many },
      '-',
      { id: 'details', label: 'Show Details' },
      { id: 'reveal', label: 'Reveal in File Manager', disabled: many },
      { id: 'copy', label: 'Copy Path', key: 'Ctrl+C' },
      { id: 'export', label: 'Export as .m3u8' },
    ]);
    $('#ctx')._handler = id => {
      const first = tracks[0];
      if (id === 'play') playQueue(state.rows.map(t => t.id), state.anchor);
      else if (id === 'queue') {
        player.queue.push(...tracks.map(t => t.id));
        buildOrder(player.pos >= 0 ? player.order[player.pos] : 0);
        renderNowPlaying(); if (state.view === 'queue') renderSide();
        toast(`${tracks.length} added to queue`, 'ok');
      } else if (id === 'next') {
        const at = player.pos >= 0 ? player.pos + 1 : 0;
        player.queue.push(...tracks.map(t => t.id));
        const added = tracks.map((_, i) => player.queue.length - tracks.length + i);
        player.order.splice(at, 0, ...added);
        renderNowPlaying(); toast('Playing next', 'ok');
      } else if (id === 'addpl') addToPlaylistMenu(e.clientX, e.clientY, tracks);
      else if (id === 'rmpl') removeFromPlaylist(tab.value, tracks.map(t => t.id));
      else if (id === 'rmq') removeFromQueue(tracks.map(t => t.id));
      else if (id === 'album') openTab(SOURCES.album(first.album, first.albumartist));
      else if (id === 'artist') openTab(SOURCES.artist(first.albumartist));
      else if (id === 'details') { togglePanel(true); panelTab = 'details'; renderPanel(); }
      else if (id === 'reveal') api('/api/reveal', { body: { id: first.id } })
        .then(r => toast(r.error ? r.error : 'Opened ' + r.folder, r.error ? 'err' : 'ok'));
      else if (id === 'copy') copyPaths();
      else if (id === 'export') exportCurrent();
    };
  });

  // sidebar context menu (playlists)
  $('#side-body').addEventListener('contextmenu', e => {
    const row = e.target.closest('[data-ctx="playlist"]');
    if (!row) return;
    e.preventDefault();
    const id = +row.dataset.value, name = row.dataset.name;
    showContext(e.clientX, e.clientY, [
      { id: 'open', label: 'Open' },
      { id: 'play', label: 'Play' },
      '-',
      { id: 'rename', label: 'Rename…' },
      { id: 'delete', label: 'Delete' },
    ]);
    $('#ctx')._handler = action => {
      const playlist = state.playlists.find(p => p.id === id);
      if (action === 'open') openTab(SOURCES.playlist(id));
      else if (action === 'play') { if (playlist && playlist.tracks.length) playQueue(playlist.tracks, 0); }
      else if (action === 'rename') {
        showModal('Rename playlist',
          `<div class="field"><label>Name</label><input id="pl-rename" value="${esc(name)}"></div>`,
          `<button class="btn quiet" data-act="modal-close">Cancel</button>
           <button class="btn" id="do-rename">Rename</button>`);
        $('#pl-rename').focus(); $('#pl-rename').select();
        $('#do-rename').addEventListener('click', async () => {
          await api('/api/playlist/rename', { body: { id, name: $('#pl-rename').value } });
          closeModal(); await refreshPlaylists(); renderTabs();
        });
      } else if (action === 'delete') {
        showModal('Delete playlist',
          `<p>Delete <b>${esc(name)}</b>? The audio files are untouched — only the
           playlist entry goes away.</p>`,
          `<button class="btn quiet" data-act="modal-close">Cancel</button>
           <button class="btn" id="do-delete">Delete</button>`);
        $('#do-delete').addEventListener('click', async () => {
          await api('/api/playlist/delete', { body: { id } });
          closeModal(); closeTab('pl:' + id); await refreshPlaylists();
          toast('Playlist deleted', 'ok');
        });
      }
    };
  });

  // sliders
  function dragBar(bar, onMove) {
    const compute = e => {
      const rect = bar.getBoundingClientRect();
      return Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    };
    bar.addEventListener('mousedown', e => {
      bar.classList.add('dragging');
      onMove(compute(e), false);
      const move = ev => onMove(compute(ev), false);
      const up = ev => {
        bar.classList.remove('dragging');
        onMove(compute(ev), true);
        removeEventListener('mousemove', move); removeEventListener('mouseup', up);
      };
      addEventListener('mousemove', move); addEventListener('mouseup', up);
    });
  }
  dragBar($('#seek'), (ratio, done) => {
    const duration = audio.duration || (player.current && player.current.duration) || 0;
    player.seeking = !done;
    $('#seek .fill').style.width = (ratio * 100) + '%';
    $('#seek .knob').style.left = (ratio * 100) + '%';
    $('#t-now').textContent = fmtTime(ratio * duration);
    if (done && duration) audio.currentTime = ratio * duration;
  });
  dragBar($('#vol'), ratio => { player.muted = false; setVolume(ratio); });

  // resizers
  function dragEdge(grip, onMove) {
    grip.addEventListener('mousedown', e => {
      e.preventDefault();
      const move = ev => onMove(ev);
      const up = () => { removeEventListener('mousemove', move); removeEventListener('mouseup', up); };
      addEventListener('mousemove', move); addEventListener('mouseup', up);
    });
  }
  dragEdge($('#side-grip'), e => {
    const width = Math.max(170, Math.min(innerWidth * 0.6, e.clientX - 48));
    $('#side').style.width = width + 'px';
    saveSetting('sidebar_width', String(Math.round(width)));
  });
  dragEdge($('#panel-grip'), e => {
    const rect = $('#editor').getBoundingClientRect();
    const height = Math.max(80, Math.min(rect.height - 160, rect.bottom - 62 - e.clientY));
    $('#panel').style.height = height + 'px';
    saveSetting('panel_height', String(Math.round(height)));
  });

  // audio element
  audio.addEventListener('timeupdate', () => {
    if (player.seeking) return;
    const duration = audio.duration || 0;
    const ratio = duration ? audio.currentTime / duration : 0;
    $('#seek .fill').style.width = (ratio * 100) + '%';
    $('#seek .knob').style.left = (ratio * 100) + '%';
    $('#t-now').textContent = fmtTime(audio.currentTime);
    if (duration) $('#t-end').textContent = fmtTime(duration);
  });
  audio.addEventListener('progress', () => {
    if (audio.buffered.length && audio.duration) {
      const end = audio.buffered.end(audio.buffered.length - 1);
      $('#seek .buf').style.width = (end / audio.duration * 100) + '%';
    }
  });
  audio.addEventListener('ended', () => next(true));
  audio.addEventListener('play', renderNowPlaying);
  audio.addEventListener('pause', renderNowPlaying);
  audio.addEventListener('error', () => {
    if (!audio.src) return;
    const track = player.current;
    toast(`Cannot decode ${track ? track.filename : 'this file'} — the browser engine may not ` +
          'support that codec.', 'err');
    out(`playback error: ${track ? track.path : '?'}`);
    setTimeout(() => next(true), 400);
  });

  // quick pick
  $('#qp-input').addEventListener('input', refreshQuick);
  $('#qp-input').addEventListener('keydown', e => {
    if (e.key === 'ArrowDown') { quickIndex = Math.min(quickIndex + 1, quickItems.length - 1); refreshQuick(); e.preventDefault(); }
    else if (e.key === 'ArrowUp') { quickIndex = Math.max(0, quickIndex - 1); refreshQuick(); e.preventDefault(); }
    else if (e.key === 'Enter') { runQuick(); e.preventDefault(); }
    else if (e.key === 'Escape') closeQuick();
  });

  // keyboard
  addEventListener('keydown', e => {
    const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
    if (e.key === 'Escape') { closeQuick(); closeModal(); hideContext(); closeMenus(); return; }
    const ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && e.shiftKey && e.code === 'KeyP') { e.preventDefault(); return openQuick('command'); }
    if (ctrl && e.code === 'KeyP') { e.preventDefault(); return openQuick('file'); }
    if (ctrl && e.shiftKey && e.code === 'KeyE') { e.preventDefault(); return setView('library'); }
    if (ctrl && e.shiftKey && e.code === 'KeyF') { e.preventDefault(); return setView('search'); }
    if (ctrl && e.shiftKey && e.code === 'KeyY') { e.preventDefault(); return setView('playlists'); }
    if (ctrl && e.shiftKey && e.code === 'KeyQ') { e.preventDefault(); return setView('queue'); }
    if (ctrl && e.code === 'KeyB') { e.preventDefault(); return toggleSide(); }
    if (ctrl && e.code === 'KeyJ') { e.preventDefault(); return togglePanel(); }
    if (e.key === 'F5') { e.preventDefault(); return rescan(false); }
    if (typing) return;
    if (ctrl && e.code === 'KeyA') { e.preventDefault(); return selectAll(); }
    if (ctrl && e.code === 'KeyC') { e.preventDefault(); return copyPaths(); }
    if (ctrl && e.code === 'ArrowRight') { e.preventDefault(); return next(); }
    if (ctrl && e.code === 'ArrowLeft') { e.preventDefault(); return prev(); }
    if (e.code === 'Space') { e.preventDefault(); return togglePlay(); }
    if (e.code === 'KeyS') return toggleShuffle();
    if (e.code === 'KeyR') return cycleRepeat();
    if (e.code === 'KeyM') { player.muted = !player.muted; return setVolume(player.volume); }
    if (e.code === 'Enter' && state.sel.size) {
      return playQueue(state.rows.map(t => t.id), Math.min(...state.sel));
    }
    if (e.code === 'Delete' && state.sel.size) {
      const tab = currentTab();
      const ids = selectedTracks().map(t => t.id);
      if (tab && tab.kind === 'playlist') return removeFromPlaylist(tab.value, ids);
      if (tab && tab.kind === 'queue') return removeFromQueue(ids);
    }
    if (e.code === 'ArrowDown' || e.code === 'ArrowUp') {
      e.preventDefault();
      const step = e.code === 'ArrowDown' ? 1 : -1;
      const next_ = Math.max(0, Math.min(state.rows.length - 1,
        (state.anchor < 0 ? (step > 0 ? -1 : 0) : state.anchor) + step));
      state.anchor = next_;
      if (!e.shiftKey) state.sel.clear();
      state.sel.add(next_);
      $('#list-wrap').scrollTop = Math.max(
        Math.min($('#list-wrap').scrollTop, next_ * ROW_H),
        (next_ + 1) * ROW_H - $('#list-wrap').clientHeight);
      paintRows(); updateStatus();
    }
  });

  addEventListener('resize', () => { if (state.rows.length) paintRows(); });
  addEventListener('contextmenu', e => { if (!e.target.closest('.trk, [data-ctx]')) e.preventDefault(); });
}

/* ---------- keep-alive ---------- */
// Cadence is a background process with a browser window in front of it. Closing
// that window tells the process nothing, so the page checks in and the server
// stops once nobody is checking in. On the way out we say so explicitly, which
// shortens the wait; a reload checks straight back in and cancels it.
const CLIENT_ID = Math.random().toString(36).slice(2) + Date.now().toString(36);
let heartbeatTimer = null;

function heartbeat(closing) {
  const url = `/api/heartbeat?t=${encodeURIComponent(state.token)}`;
  const body = JSON.stringify({ id: CLIENT_ID, closing: !!closing });
  if (closing && navigator.sendBeacon) {
    // A normal fetch is cancelled as the page tears down; a beacon is not.
    navigator.sendBeacon(url, new Blob([body], { type: 'application/json' }));
    return;
  }
  fetch(url, { method: 'POST', body, keepalive: true,
               headers: { 'Content-Type': 'application/json' } }).catch(() => {});
}

function startHeartbeat() {
  heartbeat(false);
  clearInterval(heartbeatTimer);
  heartbeatTimer = setInterval(() => heartbeat(false), 5000);
  addEventListener('pagehide', () => heartbeat(true));
  addEventListener('beforeunload', () => heartbeat(true));
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) heartbeat(false);
  });
}

function syncWindowControls() {
  // A web page cannot minimise its own window, so the amber control is a
  // restore-down: it leaves full screen, and greys out when there is none.
  const full = !!document.fullscreenElement;
  $('#win-restore').disabled = !full;
  $('#win-max').disabled = full;
}

async function installUpdate() {
  const update = state.update || {};
  if (!update.available || !update.asset) return;
  showModal('Install update',
    `<p>Download <b>Cadence ${esc(update.latest)}</b> and restart?</p>
     <div class="hint" style="margin-top:10px;line-height:1.8">
       The new build is written beside the running program. Your current version is kept
       as <code>Cadence-previous.exe</code>, so you can rename it back if anything is wrong.
       Cadence closes and reopens itself to finish.<br><br>
       Your library index, playlists and settings are untouched.</div>`,
    `<button class="btn quiet" data-act="modal-close">Cancel</button>
     <button class="btn" id="do-install">Download and install</button>`);
  $('#do-install').addEventListener('click', async () => {
    $('#do-install').textContent = 'Downloading…';
    $('#do-install').disabled = true;
    try {
      const result = await api('/api/update/install', { body: {} });
      showModal('Restarting',
        `<p>Downloaded ${esc(result.size_mb)} MB. Cadence is restarting into
         version ${esc(update.latest)}.</p>
         <div class="hint" style="margin-top:10px">If the window does not come back on its
         own, start Cadence again from where you keep it.</div>`, '');
      setTimeout(() => { try { window.close(); } catch {} }, 2500);
    } catch (err) {
      closeModal();
      toast('Update failed: ' + err.message, 'err');
    }
  });
}

async function removeFromPlaylist(playlistId, ids) {
  await api('/api/playlist/remove', { body: { id: playlistId, tracks: ids } });
  await refreshPlaylists();
  state.sel.clear(); renderList();
  toast(`${ids.length} removed from playlist`, 'ok');
}
function removeFromQueue(ids) {
  const playing = player.pos >= 0 ? player.queue[player.order[player.pos]] : null;
  player.queue = player.queue.filter(id => !ids.includes(id));
  const at = playing == null ? 0 : Math.max(0, player.queue.indexOf(playing));
  buildOrder(at);
  state.sel.clear(); renderList(); renderSide(); renderNowPlaying();
}

/* ---------- boot ---------- */
async function boot() {
  wire();
  try {
    state.info = await api('/api/state');
  } catch (err) {
    document.body.innerHTML = `<div class="empty" style="height:100vh"><h2>Cannot reach Cadence</h2>
      <p>${esc(err.message)}. Reload the page from the link the program printed.</p></div>`;
    return;
  }
  $('#st-version-label').textContent = 'v' + state.info.version;
  const settings = state.info.settings || {};
  applyTheme(settings.theme || 'dark');
  applyAccent(settings.accent || 'Deep Blue');
  // An empty string is a real choice (every optional column off), so only an
  // absent setting falls back to the defaults.
  state.columns = settings.columns === undefined
    ? DEFAULT_COLUMNS.slice()
    : settings.columns.split(',').filter(Boolean);
  state.prefs = {};
  for (const [key, value] of Object.entries(settings)) {
    if (key.startsWith('pref_')) state.prefs[key.slice(5)] = value;
  }
  applyPrefs();
  eqLoad(settings.eq);
  if (settings.sidebar_width) $('#side').style.width = settings.sidebar_width + 'px';
  if (settings.panel_height) $('#panel').style.height = settings.panel_height + 'px';
  player.volume = settings.volume ? parseFloat(settings.volume) : 0.8;
  setVolume(player.volume);

  await refreshTracks();
  await refreshPlaylists();

  if (!state.info.folder) openWelcome();
  else if (state.tracks.length) openTab(SOURCES.all());
  else openWelcome();

  renderNowPlaying();
  startHeartbeat();
  if (state.info.scanning) watchScan();
  if (prefOn('updates')) setTimeout(() => checkUpdate(true), 2500);
  out(`${state.info.app} ${state.info.version} ready · ${state.info.stats.tracks} tracks indexed`);
}
boot();
"""


def render_page(token: str) -> str:
    return (
        "<!doctype html>\n<html lang=\"en\" data-theme=\"dark\">\n<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        "<meta name=\"color-scheme\" content=\"dark light\">\n"
        f"<title>{APP_NAME}</title>\n"
        "<link rel=\"icon\" href=\"/favicon.ico\" sizes=\"16x16 32x32 48x48 256x256\">\n"
        "<link rel=\"icon\" type=\"image/png\" sizes=\"256x256\" href=\"/icon.png\">\n"
        "<link rel=\"apple-touch-icon\" href=\"/icon.png?size=180\">\n"
        "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/icon.svg\">\n"
        + UI_STYLE + "\n</head>\n<body>\n" + UI_BODY
        + "<script>window.CADENCE_TOKEN=" + json.dumps(token) + ";</script>\n"
        + "<script>\n" + UI_SCRIPT_CORE + UI_SCRIPT_VIEWS + UI_SCRIPT_PLAYER
        + UI_SCRIPT_PREFS + UI_SCRIPT_WIRE + "\n</script>\n</body>\n</html>\n"
    )


# ---------------------------------------------------------------------------
# Launcher
# ---------------------------------------------------------------------------

def free_port(preferred: int) -> int:
    for candidate in (preferred, 0):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                probe.bind(("127.0.0.1", candidate))
                return probe.getsockname()[1]
        except OSError:
            continue
    raise RuntimeError("no free port on the loopback interface")


def _no_window_flags() -> dict:
    """Keep a console window from flashing when a windowed build spawns a child."""
    if sys.platform == "win32":
        return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)}
    return {}


def open_window(url: str, server: "Server | None" = None, frameless: bool = False) -> None:
    """Prefer a chromeless browser window so it reads as an app, not a tab.

    `frameless` launches the browser in kiosk mode, which drops the operating
    system's own title bar and window buttons entirely and leaves Cadence's as
    the only ones on screen. The cost is a window that fills the display and
    cannot be dragged or resized, so it is opt-in.
    """
    candidates: list[list[str]] = []
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA", "")
        program_files = os.environ.get("PROGRAMFILES", r"C:\Program Files")
        program_files_x86 = os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")
        for exe in (
            os.path.join(local, r"Google\Chrome\Application\chrome.exe"),
            os.path.join(program_files, r"Google\Chrome\Application\chrome.exe"),
            os.path.join(program_files_x86, r"Google\Chrome\Application\chrome.exe"),
            os.path.join(program_files_x86, r"Microsoft\Edge\Application\msedge.exe"),
            os.path.join(program_files, r"Microsoft\Edge\Application\msedge.exe"),
            os.path.join(local, r"BraveSoftware\Brave-Browser\Application\brave.exe"),
        ):
            if os.path.isfile(exe):
                candidates.append([exe])
    elif sys.platform == "darwin":
        for exe in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
                    "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"):
            if os.path.isfile(exe):
                candidates.append([exe])
    else:
        for name in ("google-chrome", "chromium", "chromium-browser", "brave-browser",
                     "microsoft-edge"):
            found = shutil.which(name)
            if found:
                candidates.append([found])

    profile = os.path.join(config_dir(), "window")
    for command in candidates:
        try:
            process = subprocess.Popen(
                command + (["--kiosk"] if frameless else [])
                + [f"--app={url}", f"--user-data-dir={profile}",
                   "--window-size=1280,820", "--no-first-run",
                   "--no-default-browser-check"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                **_no_window_flags())
            if server is not None:
                server.browser = process
            return
        except Exception:
            continue
    try:
        webbrowser.open(url)
    except Exception:
        log("open this address in a browser:", url)


def close_window(server: "Server") -> None:
    """Shut the app window we launched, if it is still up."""
    browser = getattr(server, "browser", None)
    if browser is None or browser.poll() is not None:
        return
    try:
        browser.terminate()
    except Exception:
        pass


def watch_windows(server: "Server") -> None:
    """Stop the server once every window has gone away.

    Nothing tells a background process that its window was closed, so the page
    checks in every few seconds and this gives up when nobody is checking in.
    A server that has never had a window stays up: that is the --no-browser
    case, where something else is driving it.
    """
    while True:
        time.sleep(2.0)
        now = time.time()
        server.clients = {cid: until for cid, until in server.clients.items() if until > now}
        if server.seen_client and not server.clients:
            log("window closed - stopping")
            server.shutdown()
            return


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog=APP_NAME,
        description=f"{APP_NAME} - a single-file music manager with a VS Code style interface.")
    parser.add_argument("folder", nargs="?", help="music folder to designate and scan")
    parser.add_argument("--folder", dest="folder_opt", help="same as the positional argument")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"port (default {DEFAULT_PORT})")
    parser.add_argument("--rescan", action="store_true", help="re-read every file, ignoring timestamps")
    parser.add_argument("--no-browser", action="store_true", help="start the server without opening a window")
    parser.add_argument("--no-scan", action="store_true", help="skip the launch scan")
    parser.add_argument("--keep-alive", action="store_true",
                        help="keep running after the window is closed")
    parser.add_argument("--frameless", action="store_true",
                        help="open without the operating system's title bar and window "
                             "buttons, leaving only Cadence's (fills the screen)")
    parser.add_argument("--write-icon", metavar="PATH", nargs="?", const="Cadence.ico",
                        help="write the app icon as a multi-resolution .ico and exit")
    parser.add_argument("--write-png", metavar="PATH", nargs="?", const="Cadence.png",
                        help="write the app icon as a 512px PNG and exit")
    parser.add_argument("--write-svg", metavar="PATH", nargs="?", const="Cadence.svg",
                        help="write the app icon as SVG and exit")
    parser.add_argument("--version", action="version", version=f"{APP_NAME} {APP_VERSION}")
    if sys.stdout is None:      # frozen with --noconsole: give argparse somewhere to write
        sys.stdout = io.StringIO()
    if sys.stderr is None:
        sys.stderr = io.StringIO()
    args = parser.parse_args(argv)

    if args.write_icon:
        log("wrote", build_ico(args.write_icon))
        return 0
    if args.write_png:
        log("wrote", build_png(args.write_png))
        return 0
    if args.write_svg:
        with open(args.write_svg, "w", encoding="utf-8") as fh:
            fh.write(ICON_SVG)
        log("wrote", args.write_svg)
        return 0

    library = Library(os.path.join(config_dir(), "library.db"))
    folder = args.folder_opt or args.folder
    if folder:
        folder = os.path.abspath(os.path.expanduser(folder))
        if not os.path.isdir(folder):
            log(f"error: not a folder: {folder}")
            return 2
        library.set_setting("music_folder", folder)
    else:
        folder = library.get_setting("music_folder")

    token = secrets.token_urlsafe(24)
    port = free_port(args.port)
    server = Server(("127.0.0.1", port), Handler, library, token)
    url = f"http://127.0.0.1:{port}/?t={urllib.parse.quote(token)}"

    # The designated folder is pulled in on every launch, in the background so
    # the window opens immediately and fills in as tracks are found.
    scan_on_launch = library.get_setting("pref_scanlaunch", "1") != "0"
    if folder and not args.no_scan and (scan_on_launch or args.rescan):
        log("scanning", folder)
        library.scan_async(folder, full=args.rescan)
    elif folder and not scan_on_launch:
        log("launch scan is switched off in preferences")
    elif not folder:
        log("no music folder designated yet - pick one in the window")

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    if not args.keep_alive:
        threading.Thread(target=watch_windows, args=(server,), daemon=True).start()
    log(f"{APP_NAME} {APP_VERSION} listening on {url}")
    if not args.no_browser:
        threading.Timer(0.4, open_window, args=(url, server, args.frameless)).start()
    try:
        while thread.is_alive():
            thread.join(0.5)
    except KeyboardInterrupt:
        log("shutting down")
        server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
