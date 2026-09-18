#!/usr/bin/env python3
"""midiprep.py - turn one MIDI input into the three shapes the A/B rig needs.

The input is either a Standard MIDI File (it starts with "MThd") or a raw
stream of MIDI wire bytes -- a capture off a serial line, a `.syx`, or one of
`conform/vectors.py`'s vectors. Whichever it is, this writes three files into
OUTDIR, all carrying exactly the same events:

    <name>.evt   one event per line: "<microseconds> <hex>", for ab_ref
    <name>.mid   format 0, 1 tick = 10 microseconds, for mt32emu-smf2wav
    <name>.raw   the wire bytes, in order, for mt32-desktop --midi-raw
                 (written only when every event is at t=0, because a file of
                 raw bytes cannot carry time)

The point of the `.mid` is that Munt's own renderer takes SMF and nothing else,
so an A/B against it needs the stream in that container. The point of the fixed
10 us tick is that there is then no tempo map left for two readers to walk
differently: an input SMF's tempo changes are resolved here, once, into
absolute microseconds, and every leg is handed the same absolute times. 10 us
is 0.48 samples at 48 kHz, so the grid is finer than any renderer can place an
event on; write_smf() says why it is not finer still.

RAW INPUT IS DELIVERED AT t=0, ALL OF IT. That is deliberate and it is what
makes the comparison sample-exact: every renderer sees every byte before the
first frame, so nothing depends on when a thread woke up. It is not what a
cable does. For a stream whose timing matters, capture an SMF (see README.md,
"From an emulator to an A/B") and pass that.

WHAT THIS DOES NOT DO. It is not `port/src/mtp_midi_parser.c` and does not
pretend to be: it has no sysex size cap, no orphan-byte accounting and no
framing errors, because its job is to state what the stream MEANS so that
several renderers can be given the same thing. A stream that exercises the
parser's limits -- `conform/vectors.py`'s `bad`, with its 40 000-byte sysex
against MTP_SYSEX_MAX of 32768 -- will therefore be rendered differently by
our pipeline and by a reference, and that difference is the parser doing its
job, not a synthesis difference. conform.sh is where those limits are tested.

    python3 midiprep.py INPUT OUTDIR [--name NAME]

SPDX-License-Identifier: 0BSD
"""
import os
import sys

# Microseconds per SMF tick in the normalised .mid, and therefore the grid the
# .evt is quantised to. See write_smf().
TICK_US = 10



# ----------------------------------------------------------------- raw in --

def _data_len(status):
    hi = status & 0xF0
    if hi in (0xC0, 0xD0):
        return 1
    if hi == 0xF0:
        return {0xF1: 1, 0xF2: 2, 0xF3: 1}.get(status, 0)
    return 2


def parse_raw(data):
    """Wire bytes -> [(0, bytes), ...]. Running status expanded, sysex kept
    whole, real-time bytes passed through wherever they appear -- including
    inside a sysex, which is legal and which a UART really does deliver."""
    out = []
    orphans = 0
    status = 0
    i, n = 0, len(data)

    def take(i, need):
        """Collect `need` data bytes from i, emitting any real-time byte that
        interrupts them. Returns (bytes, i, complete)."""
        got = bytearray()
        while len(got) < need and i < n:
            c = data[i]
            if c >= 0xF8:
                out.append((0, bytes([c])))
                i += 1
                continue
            if c & 0x80:
                return bytes(got), i, False     # a new status truncates it
            got.append(c)
            i += 1
        return bytes(got), i, len(got) == need

    while i < n:
        b = data[i]

        if b >= 0xF8:                            # real time, anywhere
            out.append((0, bytes([b])))
            i += 1
            continue

        if b == 0xF0:
            buf = bytearray([0xF0])
            i += 1
            while i < n:
                c = data[i]
                if c >= 0xF8:
                    out.append((0, bytes([c])))
                    i += 1
                    continue
                if c == 0xF7:
                    buf.append(c)
                    i += 1
                    break
                if c & 0x80:
                    break                        # unterminated; status ends it
                buf.append(c)
                i += 1
            out.append((0, bytes(buf)))
            status = 0                           # sysex clears running status
            continue

        if b == 0xF7:                            # EOX with no sysex open
            orphans += 1
            i += 1
            continue

        if b & 0x80:                             # a status byte
            need = _data_len(b)
            i += 1
            if need == 0:
                out.append((0, bytes([b])))
                status = 0
                continue
            body, i, ok = take(i, need)
            if ok:
                out.append((0, bytes([b]) + body))
            else:
                orphans += len(body)
            status = b if b < 0xF0 else 0
            continue

        # a data byte: running status, or an orphan
        if status:
            need = _data_len(status)
            body, i, ok = take(i, need)
            if ok:
                out.append((0, bytes([status]) + body))
            else:
                orphans += len(body)
        else:
            orphans += 1
            i += 1

    return out, orphans


# ----------------------------------------------------------------- SMF in --

def _varlen(buf, i):
    v = 0
    while True:
        b = buf[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse_smf(data):
    """SMF -> [(microseconds, bytes), ...], tempo map walked."""
    if data[:4] != b'MThd':
        raise ValueError("not an SMF")
    hlen = int.from_bytes(data[4:8], 'big')
    fmt = int.from_bytes(data[8:10], 'big')
    ntrks = int.from_bytes(data[10:12], 'big')
    division = int.from_bytes(data[12:14], 'big')
    pos = 8 + hlen

    events = []           # (tick, seq, bytes or None, tempo or 0)
    seq = 0
    for _ in range(ntrks):
        while pos < len(data) and data[pos:pos + 4] != b'MTrk':
            # skip an unknown chunk, exactly as desktop_midi_smf.c does
            skip = int.from_bytes(data[pos + 4:pos + 8], 'big')
            pos += 8 + skip
        if pos >= len(data):
            break
        tlen = int.from_bytes(data[pos + 4:pos + 8], 'big')
        p, end = pos + 8, pos + 8 + tlen
        pos = end
        tick = 0
        status = 0
        while p < end:
            delta, p = _varlen(data, p)
            tick += delta
            b = data[p]
            if b == 0xFF:                     # meta
                p += 1
                mtype = data[p]
                p += 1
                mlen, p = _varlen(data, p)
                body = data[p:p + mlen]
                p += mlen
                if mtype == 0x51 and mlen == 3:
                    events.append((tick, seq, None,
                                   (body[0] << 16) | (body[1] << 8) | body[2]))
                    seq += 1
                continue
            if b in (0xF0, 0xF7):             # sysex, or an escape
                p += 1
                mlen, p = _varlen(data, p)
                body = data[p:p + mlen]
                p += mlen
                if b == 0xF0:
                    events.append((tick, seq, bytes([0xF0]) + body, 0))
                else:
                    events.append((tick, seq, bytes(body), 0))
                seq += 1
                continue
            if b & 0x80:
                status = b
                p += 1
            n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
            msg = bytes([status]) + data[p:p + n]
            p += n
            events.append((tick, seq, msg, 0))
            seq += 1

    events.sort(key=lambda e: (e[0], e[1]))

    out = []
    tempo = 500000                            # 120 bpm, the SMF default
    if division & 0x8000:                     # SMPTE
        fps = 256 - (division >> 8)
        tpf = division & 0xFF
        us_per_tick = 1000000.0 / (fps * (tpf or 1))
    else:
        us_per_tick = float(tempo) / float(division or 1)
    last_tick, t_us = 0, 0.0
    for tick, _, msg, tmp in events:
        t_us += (tick - last_tick) * us_per_tick
        last_tick = tick
        if tmp:
            if not division & 0x8000:
                tempo = tmp
                us_per_tick = float(tempo) / float(division or 1)
            continue
        if msg:
            out.append((int(t_us + 0.5), msg))
    return out, fmt, ntrks, division


# ---------------------------------------------------------------- SMF out --

def quantise(events):
    """Put every event on the TICK_US grid, so the .evt and the .mid carry
    identical times rather than times that differ by a rounding."""
    return [((t + TICK_US // 2) // TICK_US * TICK_US, m) for t, m in events]


def write_varlen(buf, v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append(0x80 | (v & 0x7F))
        v >>= 7
    buf += bytes(reversed(out))


def write_smf(path, events):
    """format 0, division 1000, tempo 10 000 us/quarter -> 1 tick = TICK_US.

    The tick is not 1 us, for two reasons. A delta time is at most four
    variable-length bytes, so 268 435 455 ticks: at 1 us that is a 268-second
    ceiling on the gap between two events, which a captured game stream can
    exceed while somebody reads the intro text. And there is no need -- 10 us
    is 0.48 samples at 48 kHz, below what a renderer can place an event on
    anyway. quantise() puts the .evt file on the same grid, so every leg is
    given the same times to the microsecond rather than nearly the same."""
    trk = bytearray()
    write_varlen(trk, 0)
    trk += bytes([0xFF, 0x51, 0x03, 0x00, 0x27, 0x10])      # 10000 us/quarter
    last = 0
    for t_us, msg in events:
        delta = (t_us - last) // TICK_US
        if delta > 0x0FFFFFFF:
            raise ValueError("a gap of %.1f s between events does not fit an "
                             "SMF delta time at %d us per tick"
                             % ((t_us - last) / 1e6, TICK_US))
        write_varlen(trk, delta)
        last = t_us
        if msg[0] == 0xF0:
            trk.append(0xF0)
            write_varlen(trk, len(msg) - 1)
            trk += msg[1:]
        elif msg[0] >= 0xF0:
            trk.append(0xF7)                 # escape: real time, F7, anything
            write_varlen(trk, len(msg))
            trk += msg
        else:
            trk += msg                       # explicit status, never running
    write_varlen(trk, 0)
    trk += bytes([0xFF, 0x2F, 0x00])
    with open(path, 'wb') as f:
        f.write(b'MThd' + (6).to_bytes(4, 'big') + (0).to_bytes(2, 'big')
                + (1).to_bytes(2, 'big') + (1000).to_bytes(2, 'big'))
        f.write(b'MTrk' + len(trk).to_bytes(4, 'big') + bytes(trk))


# -------------------------------------------------------------------- main --

def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    src, outdir = argv[1], argv[2]
    name = os.path.splitext(os.path.basename(src))[0]
    if "--name" in argv:
        name = argv[argv.index("--name") + 1]
    os.makedirs(outdir, exist_ok=True)

    data = open(src, 'rb').read()
    if data[:4] == b'MThd':
        events, fmt, ntrks, division = parse_smf(data)
        kind = "smf format %d, %d track(s), division %d" % (fmt, ntrks, division)
        orphans = 0
    else:
        events, orphans = parse_raw(data)
        kind = "raw wire bytes, delivered at t=0"

    events = quantise(events)
    span = events[-1][0] if events else 0
    sysexes = sum(1 for _, m in events if m[0] == 0xF0)
    biggest = max([len(m) for _, m in events if m[0] == 0xF0] or [0])

    evt = os.path.join(outdir, name + ".evt")
    with open(evt, 'w') as f:
        f.write("# midiprep from %s\n" % os.path.abspath(src))
        f.write("# %s\n" % kind)
        f.write("# events %d  span_us %d\n" % (len(events), span))
        for t_us, msg in events:
            f.write("%d %s\n" % (t_us, msg.hex()))
    mid = os.path.join(outdir, name + ".mid")
    write_smf(mid, events)

    # Read the SMF back with the SMF reader in this same file and check it
    # carries the events the .evt does. That does not prove libsmf agrees --
    # only mt32emu-smf2wav running can do that -- but it does mean a
    # mis-written length or delta is caught here rather than showing up as a
    # "divergence" between two renderers later.
    back, _, _, _ = parse_smf(open(mid, 'rb').read())
    if back != events:
        bad = next((i for i in range(min(len(back), len(events)))
                    if back[i] != events[i]), min(len(back), len(events)))
        print("midiprep: INTERNAL ERROR: %s does not read back as written "
              "(%d events out vs %d in, first difference at %d)"
              % (mid, len(back), len(events), bad))
        return 1
    if span == 0:
        with open(os.path.join(outdir, name + ".raw"), 'wb') as f:
            f.write(b''.join(m for _, m in events))

    print("midiprep: %s" % kind)
    print("  %-14s %d bytes" % ("input", len(data)))
    print("  %-14s %d (%d sysex, largest %d bytes)"
          % ("events", len(events), sysexes, biggest))
    print("  %-14s %.3f s" % ("span", span / 1e6))
    if orphans:
        print("  %-14s %d byte(s) with no status -- dropped here; the port's "
              "parser counts them" % ("orphan data", orphans))
    if biggest > 32768:
        print("  WARNING: a sysex of %d bytes is larger than MTP_SYSEX_MAX "
              "(32768). Our pipeline will truncate it and a reference will "
              "not: expect a divergence that is the parser, not the synth."
              % biggest)
    print("  wrote %s, %s%s" % (name + ".evt", name + ".mid",
                                ", " + name + ".raw" if span == 0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
