# `desktop/corpus/` — real music to push the pipeline with

Seven MIDI files, fetched not committed, pinned to commit hashes, every one of
them under a licence that was read out of its own repository before it went on
the list. They exist so that this project can be driven by something a person
wrote rather than by something a test generator emitted.

```sh
./desktop/corpus/fetch.sh                       # ~390 kB, from GitHub, verified
python3 desktop/corpus/scan.py desktop/corpus/files/*.mid
./desktop/ab.sh --corpus map24 --roms ~/mt32roms
./desktop/corpus/partials.sh                    # the measurement, no ROMs needed
```

---

## Two questions, and only one of them needs this

This matters more than anything else on the page, so it is first.

**"Do we render the same audio as Munt?"** — `desktop/ab.sh`'s question. For
this one **the MIDI content is very nearly irrelevant.** Both sides are handed
the same file; any material at all exercises the comparison. A denser file is
worth slightly more because it visits more of the emulator, and a longer one
because a divergence has more chances to appear, but a scale would do. This
corpus is a convenience for that question, not a prerequisite.

**"Does this sound like a real MT-32?"** — a completely different question, and
**nothing in this directory helps with it.** Every file here is General MIDI,
written for a software mixer or an OPL chip. None of it carries MT-32 sysex,
none of it uses the MT-32's channel and patch conventions, and a General MIDI
program change means something different on an MT-32 from what its author
intended. The only honest route to that question is the one
[`../README.md`](../README.md) already documents: capture the stream from a
game *you own* through DOSBox Staging, DOSBox-X, ScummVM or MAME, with
`arecordmidi` or the emulator's own MIDI capture, and render that. A public
domain string quartet tells you nothing about how this product sounds, and
neither does a Doom track.

Do not let the two blur. This corpus is about **load**: polyphony, length,
event rate. That is a real and open question — see below — and it is one this
repository can answer without a single byte of anyone's copyrighted ROM.

---

## What was actually looked for, and why

`bench/ANALYSIS.md` § 9.1(b) leaves a term of the gate's cost line blank:

```
armv7-a instructions per output frame = 489.9 + 511.5 x (sounding partials)
```

> "A real score's cost is `fixed + per-partial x (partials that passage
> sounds)`, and this repository cannot learn the second factor: it is a
> property of the timbres in Roland's control ROM and of what the composer
> wrote."

Half of that is permanently true and half of it is not. The timbre half is
Roland's and stays unknown. **The composer half is sitting in any MIDI file.**
So the corpus was chosen against four criteria, in this order:

1. **Polyphony**, because the cost line is a line in partials and nothing else
   in this repository moves along it.
2. **Length** — minutes, not seconds, so that ring occupancy, underruns and
   drift have somewhere to happen.
3. **Musical**, so that a person with ROMs and speakers can judge an A/B by
   ear rather than by hash.
4. **Not parser edge cases.** `desktop/conform/vectors.py` already covers
   running status, interleaved real-time bytes and a 40 000-byte sysex against
   `MTP_SYSEX_MAX`, synthetically and reproducibly. Real files were not
   collected to do that job again.

143 candidate files were scanned to pick these seven. The selection is recorded
per row in [`MANIFEST.tsv`](MANIFEST.tsv), and the measurements behind it are
in [`../FINDINGS.md` § 11](../FINDINGS.md).

---

## Licensing: the same standard as the ROMs

This repository refuses to contain MT-32 ROMs because they are Roland's. The
same rule applies to music. **Most game and pop MIDI on the internet is
copyrighted, including essentially everything on the classic MIDI archives**,
and a file being easy to download is not a licence.

So every row of `MANIFEST.tsv` carries the source repository, the commit, the
path, the sha256, the SPDX identifier, the file in that repository that states
it, and who holds the copyright — and each of those was read at the pinned
commit, not inferred from a project's reputation.

| | |
|---|---|
| `freedoom/freedoom` | **BSD-3-Clause**, `COPYING.adoc` at `d14dbbee`: "Copyright © 2001-2024 Contributors to the Freedoom project… Redistribution and use in source and binary forms, with or without modification, are permitted provided that…". `CREDITS-MUSIC` names the composer of every track and `README.adoc` states that contributed work "may not place additional restrictions compared to the normal Freedoom license" |
| `OpenTTD/OpenMSX` | **GPL-2.0-only**, `LICENSE` at `312ca0ae` and `README.md` § 5.0: "The OpenMSX music set for OpenTTD Copyright (C) 2010-2021 OpenMSX Authors … and is licensed under GPL v2". `src/themes.list` names the composer per file, and `docs/redfarn_music_grant.txt` is the correspondence in which one of them agreed to the GPL in writing |

**Nothing is committed.** `fetch.sh` downloads each file from the pinned commit
and checks its sha256; `files/` is in `.gitignore`. Two reasons, and the second
is the stronger:

- the root `.gitignore` already says this is how this repository treats
  upstream material — "Upstream clones are read here, never committed … Each
  workstream records the commit hash it used in its own README so the reading
  is reproducible." `bench/vendor` and `boot/vendor` work exactly this way.
- two of the seven are **GPL-2.0-only**. Committing them would put GPL'd data
  inside a tree that is otherwise 0BSD, which is a licensing decision nobody
  asked anyone to make, and a fetch script makes it unnecessary. `fetch.sh`
  also downloads each source repository's own licence text beside the files,
  because both licences require the notice to travel with the work.

---

## What is in it

`./desktop/corpus/fetch.sh --list` prints this from the manifest. In short:

| name | from | s | peak notes | why it is here |
|---|---|---|---|---|
| `map24` | Freedoom | 235 | 29 | the densest peak of the 143 scanned |
| `map18` | Freedoom | 521 | 21 | the longest — nearly nine minutes |
| `dm07` | Freedoom | 144 | 20 | the fastest event rate, 60.6 note-ons/s |
| `inter` | Freedoom | 75 | 16 | the only sysex in all 143 files |
| `e1m1` | Freedoom | 186 | 9 | the light end, deliberately |
| `rolling` | OpenMSX | 195 | 32 | the only file whose note count alone reaches 32 |
| `journey` | OpenMSX | 84 | 24 | short and dense: the one to run first |

"peak notes" is notes sounding simultaneously on the MIDI channels a
factory-reset MT-32 listens to. Munt's `Synth.cpp:897-903` sets `chanAssign` to
`{1,2,…,9}` at reset — parts 1-8 on MIDI channels 2-9, rhythm on channel 10 —
so **channel 1 and channels 11-16 are ignored entirely**, and a 16-channel
General MIDI file asks an MT-32 for less than it asks a GM module for.
`scan.py` measures both and prints the difference.

---

## The tools

### `fetch.sh` — get it, verified

```sh
./desktop/corpus/fetch.sh            # fetch what is missing, verify everything
./desktop/corpus/fetch.sh --list     # what the manifest says; fetch nothing
./desktop/corpus/fetch.sh --force    # re-download
```

One host, `raw.githubusercontent.com`, and the commit hash is in every URL, so
the bytes cannot drift. A sha256 mismatch deletes the file rather than keeping
it.

### `scan.py` — what the score asks for, with no ROMs and no rendering

```sh
python3 desktop/corpus/scan.py desktop/corpus/files/*.mid
python3 desktop/corpus/scan.py --tsv desktop/corpus/files/*.mid
```

Duration, event count, note-ons per second, sysex, the channels used, and
**notes sounding simultaneously** as a peak and as time-weighted percentiles —
time-weighted because an untimed median is dominated by the instant around
every chord change. It honours the damper pedal (CC64) the way the MT-32 does,
and CC120/CC123. It reads the file with `ab/midiprep.py`'s SMF parser, so
there is one SMF reader in this directory and not two.

It also prints the **partial demand bound**: an MT-32 timbre uses one to four
partials, so N notes demand between N and 4N partials, against a machine that
has 32.

### `probe.py` + `partials.sh` — what a partial manager actually holds

A fabricated control ROM carries no timbres: `common.partialMute` is zero, so a
note-on allocates **no partials at all** and `ab_ref --partial-log` would
honestly report zero for three minutes. `probe.py` does what
`bench/rtf_synth.cpp` does — the Timbre Temporary Area is RAM, not ROM, so it
writes a timbre of exactly K partials into all eight melodic parts over sysex
and then lets the score play. The real allocator, the real stealing.

```sh
./desktop/corpus/partials.sh                 # every file, K = 1..4
./desktop/corpus/partials.sh map24 rolling
./desktop/ab.sh --corpus map24 --count-partials --probe-partials 4
```

Three choices in that preamble are choices and not facts, and `probe.py`'s
header spells all of them out: which channels the eight melodic parts listen
to, that the rhythm part is switched off (its timbres would come from ROM
rhythm timbres a fabricated ROM does not have), and that **program changes are
stripped**. That last one was found by running it: with program changes left
in, every file reported exactly zero partials, because `Part::setProgram`
calls `resetTimbre`, which copies the all-zero ROM timbre back over the one the
preamble had just written. Game MIDI sends a program change on every channel in
its first bar. With real ROMs, none of this applies: use `--keep-programs`, or
better, do not probe at all.

---

## What this cannot tell you

- **Nothing about sound.** No MT-32 ROM has ever been in this repository and no
  session that wrote any of this code has heard one. There is no claim here
  about how anything sounds, and there will not be one until somebody with
  their own legally dumped ROMs listens and writes down what they heard.
- **Not what an MT-32 allocates.** `partials.sh` measures what the partial
  manager holds when every timbre uses K partials. The timbres a real MT-32
  would pick for a real game are in Roland's control ROM. K = 1 is a floor and
  K = 4 a ceiling; the answer for a real timbre set is between the rows.
- **Not MT-32 material.** See the top of this page.

---

## What could not be obtained, so that nobody repeats it

Every one of these was tried from this container.

| Source | What happened |
|---|---|
| `mutopiaproject.org`, `imslp.org` | `curl: (56) CONNECT tunnel failed, response 403`. The egress proxy blocks them |
| `vgmusic.com`, `bitmidi.com`, `musescore.org`, `colinraffel.com`, `magenta.tensorflow.org` | connection refused, established earlier in the same session. Assume any non-GitHub host is blocked |
| `api.github.com` | 403 for every repository but this one. `git clone` and `raw.githubusercontent.com` are the whole acquisition channel |
| `MutopiaProject/MutopiaProject` @ `2144afd6` | **reachable, and it has no MIDI.** 17 137 files, of which 10 476 `.ily` and 5 681 `.ly`: LilyPond sources. The `.mid` files Mutopia publishes are built artefacts and live on the website, which is blocked. No `lilypond` in this container and no way to install one |
| `cuthbertLab/music21` @ `ddf7c3eb` | reachable; 24 `.mid`, BSD-3-Clause (`LICENSE`, © 2006-2026 Michael Scott Asato Cuthbert). 21 of them are `midi/testPrimitive/test01..21.mid` — synthetic parser fixtures, which is the one thing criterion 4 says not to collect. `omr/k525MIDIMvt1.mid` is a Mozart quartet movement: legally usable, but four-voice string writing is *less* dense than anything already chosen, so it earns no place |
| `jazz-soft/test-midi-files` @ `ee79d9e3` | reachable, 76 `.mid`, but they are generated test cases ("test-c-major-scale", "test-all-gm-sounds", "test-corrupt-file-extra-byte"). `conform/vectors.py` covers that ground deliberately and better |
| `mido/mido`, `craigsapp/midifile`, `FluidSynth/fluidsynth` | reachable; **no `.mid` files at HEAD**. They are libraries that parse MIDI, not libraries that ship it |
| `Wohlstand/libADLMIDI`, `MuseScore/MuseScore`, `craigsapp/bach-370-chorales` | reachable but not pursued: the first ships game music whose licence is not stated per file, the second ships `.mscz` and needs MuseScore to render, the third is Humdrum `**kern` and needs a converter that is not here |
| MT-32-native material of any kind | **none exists that is freely licensed**, and none was looked for after the first two searches, because a game's MT-32 stream is the game's. That is what the emulator capture route in `../README.md` is for |
