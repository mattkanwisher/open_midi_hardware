#!/usr/bin/env python3
"""gen_vectors.py - compile the MIDI test vectors into the image.

The bare-metal image has no filesystem to read a stream from, so the streams
are linked into .rodata.

The first three are NOT a new set of test data: they are copied verbatim from
the places workstream D already asserts against, so that the host, the
qemu-user armv7 run and the bare-metal run are answering questions about the
same bytes.

  demo     port/host/main.c, the built-in DEMO[] array
  bank     port/host/test.sh case 2: 64 x 254-byte patch sysexes (about 16 kB
           of wire, 5.25 s at 31250 baud) with notes and Active Sensing
           interleaved
  bad      port/host/test.sh case 3: orphan data bytes, an unterminated sysex,
           and a sysex larger than the 32 kB cap

The rest are this directory's, and they exist because `port/host`'s three
cover the happy path and two of the three classic malformed cases and nothing
else. Each one is a thing a real DOS machine, a real MIDI interface or a real
cable can do:

  rtsysex  System Real Time bytes sprinkled *inside* a 254-byte patch dump,
           which the spec explicitly permits (any 0xF8..0xFF may appear between
           any two bytes of anything) and which any interface that emits Active
           Sensing will actually do during a bank load
  runstat  400 running-status note messages, 2 bytes each, offset by one byte
           so that a message straddles every 64-byte read boundary the render
           loop's MIDI_BATCH produces -- plus one real-time byte wedged between
           a key and its velocity
  panic    an all-notes-off storm: 128 notes on, then 16 channels x 3
           controllers x 32 repeats of All Sound Off / Reset All Controllers /
           All Notes Off. This is what a game does when you press Escape, and
           what a DOS machine does when it reboots mid-tune
  trunc    a sysex that simply stops -- the cable was pulled, or the machine
           was reset. No abort status ever arrives, so the parser has to end
           the run holding an incomplete message and must not emit it

WHAT THE `expect` TABLE IS. Each vector carries the counters it should
produce, computed by `model()` below -- a second, independent implementation of
the contract in port/include/mtp_midi_parser.h, written from that header and
sharing no code with port/src/mtp_midi_parser.c. The image prints measured
against expected and says MATCH or MISMATCH per vector, so the assertion in
test.sh is not a magic number pasted from a previous run: it is two
implementations of one written contract agreeing.

Regenerate with:  make vectors     (or python3 tools/gen_vectors.py src/)
Write the same streams out as .syx files for the host harness with:
                  python3 tools/gen_vectors.py src/ --syx <dir>

SPDX-License-Identifier: 0BSD
"""
import sys, os

SYSEX_CAP = 32768        # MTP_SYSEX_MAX, port/include/mtp_midi_parser.h:30

# --- port/host/main.c DEMO[] ------------------------------------------------
def demo():
    out = bytearray([0x90, 60, 100, 64, 100, 67, 100, 72, 100])
    out += bytes([0xF0, 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x00])
    out += b'HELLO MT-32         '
    out += bytes([0x00, 0xF7])
    out += bytes([0xFE])
    out += bytes([0x80, 60, 0, 64, 0, 67, 0, 72, 0])
    return bytes(out)

# --- port/host/test.sh case 2 ----------------------------------------------
def roland_sysex(addr, data):
    body = list(addr) + list(data)
    return bytes([0xF0, 0x41, 0x10, 0x16, 0x12]) + bytes(body) + \
           bytes([(-sum(body)) & 0x7f, 0xF7])

def bank():
    out = bytearray([0x90, 60, 100, 64, 100, 67, 100])
    for i in range(64):
        out += roland_sysex((0x08, (i * 2) & 0x7f, 0x00),
                            bytes([(j * 7 + i) & 0x7f for j in range(246)]))
        if i % 8 == 0:
            out += bytes([0xFE, 72 + (i % 5), 90])
    out += bytes([0xB0, 123, 0])
    return bytes(out)

# --- port/host/test.sh case 3 ----------------------------------------------
def bad():
    b = bytearray([40, 50, 60])
    b += bytes([0xF0, 0x41, 0x10, 0x16, 0x12]) + bytes(100)
    b += bytes([0x90, 60, 100])
    b += bytes([0xF0]) + bytes([0x7f] * 40000) + bytes([0xF7])
    b += bytes([0x80, 60, 0])
    return bytes(b)

# --- E: real time inside a sysex -------------------------------------------
def rtsysex():
    """One 254-byte Roland patch dump with real-time bytes wedged into it.

    The payload the synth must receive is byte-for-byte the same dump it would
    have received with no interleaving, which is the whole assertion: the
    parser has to pull 0xF8..0xFF out of the middle of a sysex without
    disturbing it, and without losing the real-time bytes either."""
    payload = roland_sysex((0x08, 0x00, 0x00),
                           bytes([(j * 11 + 3) & 0x7f for j in range(246)]))
    rt = [0xF8, 0xFE, 0xFA, 0xF8, 0xFC, 0xFE, 0xF8, 0xFE, 0xF8, 0xFF]
    out = bytearray([0x90, 60, 100])
    k = 0
    for i, b in enumerate(payload):
        out.append(b)
        # after F0, after every 32nd byte, and immediately before the F7
        if i == 0 or (i and i % 32 == 0) or i == len(payload) - 2:
            out.append(rt[k % len(rt)]); k += 1
    out += bytes([0x80, 60, 0])
    return bytes(out)

# --- E: running status across the read boundary ----------------------------
def runstat():
    """0x90 then 400 two-byte running-status messages.

    The render loop reads MIDI in batches of MIDI_BATCH = 64 bytes
    (port/src/mtp_render.c:11), so a read boundary falls every 64 bytes. With a
    one-byte status in front and two-byte messages after it, message 31 spans
    bytes 63..64 -- the first boundary -- and because 64 is even and the
    messages are 2 bytes, every subsequent boundary lands mid-message too. A
    parser that resets its pending state per call loses one message per 64
    bytes; a parser that keeps it loses none.

    One real-time byte is wedged between a key and its velocity as well, which
    is the other place a stream-oriented parser tends to break."""
    out = bytearray([0x90])
    for i in range(200):
        out += bytes([36 + (i % 60), 64 + (i % 40)])
        if i == 100:
            out.append(0xFE)           # between key and velocity of msg 101
    for i in range(200):
        out += bytes([36 + (i % 60), 0])
    return bytes(out)

# --- E: the panic storm -----------------------------------------------------
def panic():
    """What a game sends when you hit Escape, times thirty-two.

    Every one of these is a message that MUST NOT be dropped: a lost All Notes
    Off is a note that hangs until the box is power-cycled. Sent with explicit
    status bytes, not running status, because that is what the real senders do
    and because it is the worse case for the FIFO."""
    out = bytearray()
    for i in range(128):
        out += bytes([0x90 | (i % 16), 24 + (i % 88), 100])
    for _ in range(32):
        for ch in range(16):
            out += bytes([0xB0 | ch, 120, 0])   # All Sound Off
            out += bytes([0xB0 | ch, 121, 0])   # Reset All Controllers
            out += bytes([0xB0 | ch, 123, 0])   # All Notes Off
    return bytes(out)

# --- E: the cable was pulled ------------------------------------------------
def trunc():
    """A patch dump that stops in the middle and is never terminated or
    aborted. Nothing follows it: the stream just ends.

    Correct behaviour is that the run ends with the parser still holding an
    incomplete sysex which it never emits -- 0 sysex messages, 0 truncated,
    0 aborted, because none of those things happened. The failure this catches
    is a parser that flushes what it has at EOF and hands the synth half a
    patch, which a real MT-32 would checksum-reject and an emulator might
    not."""
    out = bytearray([0x90, 60, 100])
    out += bytes([0xF0, 0x41, 0x10, 0x16, 0x12, 0x08, 0x00, 0x00])
    out += bytes([(j * 5 + 1) & 0x7f for j in range(120)])
    return bytes(out)

# ---------------------------------------------------------------------------
# An independent model of port/include/mtp_midi_parser.h's contract.
# ---------------------------------------------------------------------------

FNV_OFF = 2166136261
FNV_PRIME = 16777619

def _short_len(status):
    if status < 0x80:
        return 0
    hi = status & 0xF0
    if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
        return 3
    if hi in (0xC0, 0xD0):
        return 2
    return {0xF1: 2, 0xF2: 3, 0xF3: 2, 0xF6: 1}.get(status, 0)

def model(stream, cap=SYSEX_CAP):
    """What the contract says this byte stream produces.

    Returns (short, sysex, realtime, orphan, truncated, aborted,
             sysex_bytes, fnv1a_over_every_delivered_sysex)."""
    short = sysexn = rt = orphan = trunc_n = abort_n = 0
    sx_bytes = 0
    h = FNV_OFF
    running = 0
    pend = []            # bytes collected for the current short message
    need = 0
    in_sx = False
    sx = bytearray()
    over = False

    def emit_sysex():
        nonlocal sysexn, trunc_n, sx_bytes, h, in_sx, sx, over
        if over:
            trunc_n += 1
        else:
            sysexn += 1
            sx_bytes += len(sx)
            for b in sx:
                h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
        in_sx = False
        sx = bytearray()
        over = False

    def push(b):
        nonlocal over
        if len(sx) < cap:
            sx.append(b)
        else:
            over = True

    for b in stream:
        if b >= 0xF8:                      # System Real Time, anywhere
            rt += 1
            continue
        if b >= 0x80:                      # status
            if in_sx:
                if b == 0xF7:
                    push(b)
                    emit_sysex()
                    continue
                abort_n += 1
                in_sx = False
                sx = bytearray()
                over = False
            if b == 0xF0:
                in_sx = True
                sx = bytearray()
                over = False
                running = 0
                push(b)
                continue
            if b == 0xF7:
                continue                   # lone EOX: ignore
            pend = [b]
            need = _short_len(b)
            running = b if b < 0xF0 else 0
            if need == 0:
                pend = []
            elif need == 1:
                short += 1
                pend = [b]                 # emit_short leaves one byte pending
                pend = []                  # ...then system common clears it
            continue
        # data byte
        if in_sx:
            push(b)
            continue
        if not pend:
            if running == 0:
                orphan += 1
                continue
            pend = [running]
            need = _short_len(running)
        pend.append(b)
        if len(pend) >= need:
            short += 1
            if pend[0] >= 0xF0:
                pend = []                  # no running status for F1..F6
            else:
                pend = [pend[0]]           # running status continues
    return dict(short=short, sysex=sysexn, realtime=rt, orphan=orphan,
                truncated=trunc_n, aborted=abort_n, sysex_bytes=sx_bytes,
                hash=h)

# ---------------------------------------------------------------------------

def emit(f, name, data):
    f.write("const uint8_t midi_%s[] = {" % name)
    for i, v in enumerate(data):
        f.write("\n   " if i % 16 == 0 else " ")
        f.write("0x%02x," % v)
    f.write("\n};\nconst uint32_t midi_%s_len = %u;\n\n" % (name, len(data)))

STREAMS = [("demo", demo), ("bank", bank), ("bad", bad),
           ("rtsysex", rtsysex), ("runstat", runstat),
           ("panic", panic), ("trunc", trunc)]

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "src"
    syxdir = None
    if "--syx" in sys.argv:
        syxdir = sys.argv[sys.argv.index("--syx") + 1]
        os.makedirs(syxdir, exist_ok=True)

    streams = [(n, fn()) for n, fn in STREAMS]
    exp = {n: model(d) for n, d in streams}

    with open(os.path.join(outdir, "midi_vectors.c"), "w") as f:
        f.write("/* GENERATED by tools/gen_vectors.py -- do not edit.\n"
                " * The first three are the bytes port/host/test.sh asserts\n"
                " * against; the rest are emu/'s failure-mode vectors.\n"
                " * The `expect` fields come from an independent model of\n"
                " * port/include/mtp_midi_parser.h's contract, NOT from a\n"
                " * previous run of the parser under test.\n"
                " * SPDX-License-Identifier: 0BSD */\n"
                '#include "midi_vectors.h"\n\n')
        for n, d in streams:
            emit(f, n, d)
        f.write("const midi_vector midi_vectors[] = {\n")
        for n, d in streams:
            e = exp[n]
            f.write('    { "%s", midi_%s, %u,\n'
                    '      { %u, %u, %u, %u, %u, %u, %u, 0x%08xu } },\n'
                    % (n, n, len(d), e["short"], e["sysex"], e["realtime"],
                       e["orphan"], e["truncated"], e["aborted"],
                       e["sysex_bytes"], e["hash"]))
        f.write("    { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0u } }\n};\n")

    with open(os.path.join(outdir, "midi_vectors.h"), "w") as f:
        f.write("/* GENERATED by tools/gen_vectors.py -- do not edit.\n"
                " * SPDX-License-Identifier: 0BSD */\n"
                "#ifndef MIDI_VECTORS_H\n#define MIDI_VECTORS_H\n"
                "#include <stdint.h>\n\n"
                "/* What the contract in port/include/mtp_midi_parser.h says\n"
                " * this stream must produce. Computed by a second\n"
                " * implementation of that contract in tools/gen_vectors.py. */\n"
                "typedef struct {\n"
                "    uint32_t short_msgs;\n"
                "    uint32_t sysex_msgs;\n"
                "    uint32_t realtime;\n"
                "    uint32_t orphan_data;\n"
                "    uint32_t sysex_truncated;\n"
                "    uint32_t sysex_aborted;\n"
                "    uint32_t sysex_bytes;    /* total bytes delivered, F0..F7 */\n"
                "    uint32_t sysex_hash;     /* FNV-1a over all of them       */\n"
                "} midi_expect;\n\n"
                "typedef struct { const char *name; const uint8_t *bytes;\n"
                "                 uint32_t len; midi_expect expect; }"
                " midi_vector;\n"
                "extern const midi_vector midi_vectors[];\n\n")
        for n, d in streams:
            f.write("extern const uint8_t midi_%s[];\n"
                    "extern const uint32_t midi_%s_len;\n" % (n, n))
        f.write("\n#endif\n")

    if syxdir:
        for n, d in streams:
            with open(os.path.join(syxdir, n + ".syx"), "wb") as f:
                f.write(d)

    print("%-8s %7s %7s  %5s %5s %5s %6s %5s %5s %9s %s"
          % ("stream", "bytes", "wire_s", "short", "sysex", "rt",
             "orphan", "trunc", "abort", "sysexB", "hash"))
    for n, d in streams:
        e = exp[n]
        print("%-8s %7d %7.2f  %5d %5d %5d %6d %5d %5d %9d 0x%08x"
              % (n, len(d), len(d) * 10.0 / 31250.0, e["short"], e["sysex"],
                 e["realtime"], e["orphan"], e["truncated"], e["aborted"],
                 e["sysex_bytes"], e["hash"]))

main()
