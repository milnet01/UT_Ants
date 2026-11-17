<!-- ants-spec-format: 1 -->
# UTA-0056 — mix the game's sounds so speech stays audible

**Status:** shipped (2026-11-17), green on the matrix. No review: contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it was the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0056 (user-request-2026-09-05, widened 2026-09-08).
**Blocker for:** UTA-0467.  **Pairs with:** UTA-0478.

**Layman:** The game gets a sound mixer. The announcer and warnings stay clear over the loudest gunfire, and a burst of flak shrapnel sounds like one solid hit instead of a distorted roar.

## 1. Goal

`uaudio` exists. It turns UT's stored sounds into playable clips and mixes
any number of play requests into one stereo stream. Speech ducks the other
sounds, and voice limits keep one weapon from saturating the output. The
client opens a sound device and feeds it from the mixer. The measurements
the roadmap item asks for are in a test, so the rule is checked rather
than re-argued.

## 2. Problem

Nothing in the engine plays sound. `upkg::Sound` returns a sound's payload
undecoded, and its header leaves decoding to `uaudio`
(`src/upkg/Sound.h`). UTA-0019 shipped the weapons without sound, waiting
for this item (`docs/specs/UTA-0019-weapon-set.md` § 9).

The user reported two defects in UT99 with the same weapon, the flak
cannon (UTA-0056's body):

1. **Priority.** A loud weapon drowns the announcer. Speech must stay
   intelligible whatever else plays.
2. **Voice count.** Eight chunks per shot (`weapons/stock.weapons`,
   `[fire flak-cannon primary]`, `count = 8`) each make an impact sound.
   Dozens of near-simultaneous voices sum past full scale. The result clips
   and muddies, even when nothing competes for priority.

Each needs its own mechanism. A mixer that solves one still has the defect.

## 3. Scope decisions (agreed with the user)

The user made these on 2026-11-17, choosing the recommended option each
time:

1. **Four groups, in priority order:** speech (announcer, warnings,
   objectives); weapons and impacts; world (doors, pickups, footsteps,
   ambience); music. Each has its own volume.
2. **Ducking is a fixed dip.** While speech plays, every group below it
   drops by a set amount. It is not a compressor keyed on the speech bus.
3. **The dip recovers in about half a second** after speech ends.
4. **On by default; the player can turn it off** and set how deep the dip
   is.

Already decided elsewhere and binding here:

5. **Sound keeps the wall clock** when the weapon wheel slows the game
   (`docs/specs/UTA-0020-weapon-wheel.md` § 3 decision 4). The mixer has
   no game-time input and no pitch shift for slow motion.
6. **`uaudio` mixes on SDL3's device** (`docs/design.md` § The stack, and
   what it rules out, the *SDL3 audio* row). OpenAL Soft was rejected there.

Settled by this spec (the session's call, cheap to redo):

7. **No SDL inside `uaudio`.** It follows `uinput`'s pattern
   (`src/uinput/CMakeLists.txt`): the client owns the device and hands the
   library a buffer to fill. The unit tests then build on the leg with no
   SDL.
8. **No positional sound yet.** A voice takes a gain and a left-right pan
   from its caller. Distance falloff and direction from the listener come
   with the first caller that needs them (UTA-0467).
9. **The game plays nothing yet.** `docs/design.md` rule 2 keeps `upkg`
   out of the runtime programs, so the client cannot read a sound package.
   Getting sounds into a map's bundle is a format change, filed as
   UTA-0478. Until then a tool, `ut-mix`, renders a mix from the install's
   packages to a WAV file, so the result can be heard.

## 4. Design

### 4.1 Files

| File | Holds |
|---|---|
| `src/uaudio/Wav.h`, `Wav.cpp` | `decodeWav`: RIFF WAV bytes to a `Clip` |
| `src/uaudio/Mixer.h`, `Mixer.cpp` | `Group`, `Settings`, `PlayRequest`, `Mixer` |
| `src/uaudio/CMakeLists.txt` | `uta_uaudio`, linking `uta_core` alone, asserted at configure time in the form `src/ugame/CMakeLists.txt` uses |
| `apps/ut-ants/Audio.h`, `Audio.cpp` | The SDL3 device: opens it, and fills it from a `Mixer` in SDL's stream callback |
| `tools/ut-mix/` | `ut-mix`, and the `flak-announcer` scenario as a small library the real-asset test shares |
| `tests/unit/WavTest.cpp`, `tests/unit/MixerTest.cpp` | Unit tests |
| `tests/real/RealMixTest.cpp` | The measurements of § 4.7, on the install's sounds |

### 4.2 Clips

`decodeWav(std::span<const std::byte>) -> uta::Result<Clip>`. A `Clip` holds
its sample rate, its channel count (1 or 2) and its frames as `float` in
[-1, 1].

Accepted: format tag 1 (PCM), 8-bit unsigned or 16-bit signed, mono or
stereo, any sample rate from 4000 to 96000 Hz. 8-bit maps `(s - 128) / 128`
and 16-bit maps `s / 32768`.

Stock content is almost all of that kind. Scanning every RIFF `fmt ` chunk
in the install's `Sounds/*.uax` (a throwaway Python byte scan, 2026-11-17)
found 4547 sounds in 142 packages. Of those, 4541 are PCM in 8- or 16-bit,
mono or stereo, at rates from 8000 to 48000 Hz; 22050 Hz 16-bit mono is the
commonest. The other six are IMA ADPCM (tag 17, four), MS ADPCM (tag 2,
one) and MP3 (tag 85, one). They are refused, with an error naming the
format tag, and they play as silence. Decoding them waits until a map is
found to need one.

Refused too: a missing `fmt ` or `data` chunk, a chunk running past the
payload's end, a channel count other than 1 or 2, a sample size other than
8 or 16 bits. Each error names its reason.

### 4.3 The mixer

`Mixer` runs at 48000 Hz, stereo, `float`. `mix(std::span<float>)` fills
interleaved left-right frames. The client asks SDL for that format, and SDL
converts to the device's own.

`play(const Clip&, PlayRequest) -> VoiceId` starts a voice at the next
`mix`. `PlayRequest` carries the group, a gain (default 1), a pan (-1 left
to 1 right, default 0), and a priority within the group (default 0).

A clip played by the mixer must outlive its voices. The caller keeps its
clips in a store that only grows during play, and calls `stopAll()` before
freeing one. `ut-mix` and the tests hold clips for the run's length.

Each voice steps through its clip at `clipRate / 48000` frames per output
frame, interpolating linearly between neighbours. A mono clip feeds both
channels. Pan uses a constant-power law.

Output, before the limiter, is the sum over voices of: sample × voice gain
× pan × group volume × group duck gain × master volume.

### 4.4 Ducking

While at least one speech voice plays and `Settings::duck` is on, each of
the three lower groups moves its duck gain to `Settings::duckDb`. Speech
itself is never ducked.

| Setting | Default | Range |
|---|---|---|
| `duck` | on | on or off |
| `duckDb` | -12 dB | 0 to -24 dB |
| attack, to the dip | 50 ms | fixed |
| release, back to 0 dB | 500 ms | fixed |

The gain moves linearly in decibels, per output frame. Attack starts at the
frame the first speech voice starts. Release starts at the frame the last
speech voice ends. A new line during release attacks again from wherever
the gain has reached. With `duck` off, every duck gain is 1.

The 50 ms attack is short enough to land before a line's first syllable
and long enough not to click. The -12 dB default is a starting point;
§ 4.7's measurement can move it.

### 4.5 Voice limits

A play request goes through three checks, in this order:

1. **Collapse.** If a voice of the same clip started within the last 40 ms
   of mixer time, no new voice starts. The existing voice's gain becomes
   `min(2, sqrt(g_old² + g_new²))`. Its returned `VoiceId` is the existing
   voice's. Requests made between two `mix` calls share one start time, so
   eight chunk impacts in one tick collapse to one voice.
2. **Per-clip cap.** At most 4 voices of one clip play at once (settable
   per clip, from 1 to 64). A request over the cap steals that clip's
   oldest voice.
3. **Total cap.** At most 64 voices play at once. A request when all are
   busy steals one. The victim is chosen by lowest group (music, then
   world, then weapons, then speech), then lowest priority, then oldest.
   It never steals from a higher group than the request's own. If no voice
   can be stolen, the request is dropped and returns an invalid `VoiceId`.

A stolen voice stops at once. A cut mid-waveform clicks; a short fade is
the first thing to add if that is heard.

### 4.6 The limiter

A peak limiter on the master output guarantees that no sample leaves the
mixer outside [-1, 1], whatever is played. Its gain drops at once to
`1 / peak` on a frame whose peak exceeds 1. It then returns towards 1 over
100 ms. The voice limits in § 4.5 do the real work; the limiter is the
guarantee.

Three readouts let a test see the work without a device:

- `Mixer::stats()` returns each group's duck gain now, the limiter's
  deepest gain reduction since the last call, and the requests dropped.
- `Mixer::voicesOf(const Clip&)` counts that clip's playing voices.
- `mix` takes an optional `GroupTaps`: four caller-owned buffers, each
  filled with that group's own contribution before the limiter.

### 4.7 The measurements

The roadmap item asks for two measurements, stated by whoever builds the
mixer. Both run in the `flak-announcer` scenario at default settings:

- **Firing:** the flak cannon's primary, continuously for 4 s: one fire
  sound every 0.77 s (`[fire flak-cannon primary]`, `refire = 0.77`).
  Each shot's 8 chunk impacts follow it at 15 ms steps, in the weapons
  group. The fire sound is `UnrealShare.uax`'s `shot1` and the impact
  `Chunkhit1`, both in its `flak` group. Both names were found in the
  package's name table (`strings`); that UT99's flak cannon plays these
  two is unverified, and UTA-0467 settles the real mapping.
- **Speech:** `Announcer.uax`'s `cd5min` at 1 s, in the speech group.

The scenario must show three results:

1. **Speech margin ≥ 6 dB.** Over the announcer line, after its 50 ms
   attack, the speech group's RMS exceeds the RMS of the other three
   groups' sum (after their duck) by at least 6 dB.
2. **The limiter barely works.** Its deepest gain reduction over the run is
   no more than 6 dB.
3. **The shrapnel is one hit.** No more than 4 voices of `Chunkhit1` play
   at any frame.

Measured 2026-11-17 at the defaults above: speech margin 10.2 dB, limiter
3.3 dB, 3 impact voices. With the duck off the margin is -1.8 dB.

If a target fails on the real sounds, the build changes the defaults
(`duckDb`, a group's default volume, the caps) until it holds. It then
records the change here (`write-spec` Step 8). It never lowers a target.

### 4.8 The client's device

`apps/ut-ants/Audio.cpp` initialises `SDL_INIT_AUDIO` and opens the default
playback device with an audio stream at 48000 Hz, stereo, `float`. SDL's
stream callback calls `Mixer::mix` for as many frames as SDL asks.

`Mixer` is not thread-safe. The game thread wraps every `play`, `stopAll`
and settings change in `SDL_LockAudioStream`, which excludes the callback.
`mix` allocates nothing, takes no lock and does no I/O, so the callback
never waits on the game thread.

If the device does not open, the client prints one line, naming SDL's
error, and runs silent. A headless run with no sound server is the common
case.

### 4.9 `ut-mix`

`ut-mix --install <UT dir> [--duck on|off] [--duck-db <dB>] --out <file.wav>`
renders the `flak-announcer` scenario to a 16-bit stereo WAV at 48000 Hz. It
prints the three figures of § 4.7. It exists so the mix can be heard
before the game plays anything (§ 3 item 9). It links `upkg`, which rule 2
allows for a tool.

## 5. Invariants

- **INV-1** — `decodeWav` returns a clip whose samples match the payload's PCM, mapped as § 4.2 says, for 8- and 16-bit, mono and stereo. It refuses every other format tag and every malformed chunk § 4.2 lists, with an error naming the reason. *Test:* `WavTest`, on synthetic payloads built in the test.
  *Breaks when:* the 8-bit bias is dropped, or a tag-17 payload's bytes are read as PCM.
- **INV-2** — A clip at rate r, played at gain 1 and pan 0 with nothing else playing, lasts `ceil(frames × 48000 / r)` output frames, give or take one, and a constant clip yields that constant scaled by the centre pan gain. *Test:* `MixerTest`, at 11025, 22050 and 44100 Hz.
  *Breaks when:* the step uses the clip's frame count instead of its rate, or interpolation reads past the clip's end.
- **INV-3** — Each group's volume and the master volume scale only that group's voices or the whole output, as § 4.3's product says. *Test:* `MixerTest`, a constant clip in each group with each volume set in turn.
  *Breaks when:* a group's volume is applied to the wrong group, or the master volume is applied twice.
- **INV-4** — With `duck` on, a speech voice brings the other three groups' duck gain to `duckDb` within 50 ms of its start. The gain returns to 0 dB within 500 ms of the last speech voice's end. Speech's own gain is never ducked, and with `duck` off no group is. *Test:* `MixerTest`, reading duck gain per group from `stats()` at frame steps.
  *Breaks when:* a weapons voice triggers the dip, release starts at the first speech voice's end, or speech is ducked too.
- **INV-5** — No more than a clip's cap of its voices plays at once. A request over the cap stops that clip's oldest voice. *Test:* `MixerTest`, five requests of one clip at 100 ms intervals, cap 4.
  *Breaks when:* the cap counts voices across all clips, or the newest voice is stolen. The 100 ms spacing keeps § 4.5's collapse out of the fixture.
- **INV-6** — Two requests of one clip within 40 ms of mixer time leave one voice, with gain `min(2, sqrt(g1² + g2²))`. Requests 41 ms apart leave two. *Test:* `MixerTest`.
  *Breaks when:* the window is measured in wall time, or the gains add linearly.
- **INV-7** — With all 64 voices busy, a request steals by § 4.5 item 3's order and never from a higher group. A music request with 64 speech voices playing is dropped. *Test:* `MixerTest`, with distinct clips so neither the collapse nor the per-clip cap fires.
  *Breaks when:* the oldest voice is stolen regardless of group.
- **INV-8** — No output sample lies outside [-1, 1] for any input. *Test:* `MixerTest`, 64 full-scale square-wave voices of distinct clips at gain 2.
  *Breaks when:* the limiter is removed, or it releases between a frame's two channels.
- **INV-9** — `mix` allocates no memory. *Test:* `MixerTest`, counting global `operator new` calls across a `mix` of 64 playing voices.
  *Breaks when:* a voice list grows inside `mix`, or a `std::vector` is built per call.
- **INV-10** — `flak-announcer` meets § 4.7's three targets at default settings. *Test:* `RealMixTest`, on the real-asset tier.
  *Breaks when:* the duck is off, or `duckDb` is 0. Not when the collapse window is 0: the per-clip cap alone holds result 3 (measured 2026-11-17: 4 voices, limiter 3.7 dB). INV-6 grades the collapse.
- **INV-11** — `uta_uaudio` links `uta_core` and nothing else, and `uta_ugame` still links no `uta_uaudio`. *Test:* configure time, `src/uaudio/CMakeLists.txt` and the existing check in `src/ugame/CMakeLists.txt`.
  *Breaks when:* `uaudio` links SDL3, or `ugame` links `uaudio` to play a sound.

## 6. Failure modes

| What fails | What happens |
|---|---|
| No audio device, or no sound backend compiled into SDL | One line on stderr; the client runs silent |
| A sound in a format § 4.2 refuses | `decodeWav`'s error; the caller plays nothing for that sound |
| More requests than voices | § 4.5 steals or drops; the drop is counted in `stats()` |
| A clip freed while its voice plays | Undefined. § 4.3 makes the caller call `stopAll` first |
| The game thread holds the stream lock too long | The device underruns and a gap is heard. Each lock covers only `play` calls |
| `ut-mix` cannot find a named sound | Exits non-zero, naming the package and the sound |

## 7. Tests

- `tests/unit/WavTest.cpp` (INV-1) and `tests/unit/MixerTest.cpp`
  (INV-2 to INV-9), label `unit`, with no SDL and no install.
- `tests/real/RealMixTest.cpp` (INV-10), in `uta_real_asset_tests`, label
  `real-assets`. It reads the two packages through `upkg` and runs the
  scenario through the library `ut-mix` uses.
- INV-11 is the configure-time link check, so every build runs it.
- `scripts/mutations/` gains a `uaudio` file, one mutation per invariant
  from INV-1 to INV-9, each one an invariant's *Breaks when:*.
- The client's device is checked by hand: start `ut-ants` and see that it
  prints no audio error. Nothing plays until UTA-0478.

## 8. Alternatives considered (and rejected)

- **A compressor keyed on the speech bus.** It ducks only as far as needed,
  so a quiet scene barely changes. The user chose the fixed dip
  (§ 3 item 2): it is predictable and cheaper, and the test can check it
  exactly.
- **Mixing on the game thread and pushing to SDL each frame.** It needs no
  lock. A long frame, such as a map load or a hitch, would starve the
  device and make a gap; the callback keeps sound going through a hitch.
- **One `SDL_AudioStream` per voice, letting SDL resample.** It puts SDL
  in `uaudio` (§ 3 item 7) and makes the mixer untestable without a
  device.
- **Resampling each clip to 48000 Hz once, at load.** It makes mixing a
  plain sum. Linear interpolation per voice costs little at 64 voices, and
  it leaves room for per-voice pitch, which UT's `PlaySound` takes.
- **Only a limiter, no voice caps.** A limiter alone pumps the whole mix
  down on every flak shot, ducking speech with it. That is the "it got
  quieter because something loud happened" defect the item forbids.

## 9. Out of scope

- Baking sounds into a map's bundle, and any runtime path from a map to a
  clip: UTA-0478.
- Which UT sound plays in which group, and weapon sounds: UTA-0467.
- Positional sound: distance falloff, direction, Doppler (§ 3 item 8).
- Music playback. UT's music is tracker modules in `Music/*.umx`; the
  music group exists here so they have a place.
- A settings menu and saved settings. `Settings` lives in memory with its
  defaults until UTA-0120 and UTA-0297 give the player the switch and the
  slider.
- The six compressed stock sounds (§ 4.2).

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1 | `WavTest` |
| INV-2, INV-3, INV-4, INV-5, INV-6, INV-7, INV-8, INV-9 | `MixerTest` |
| INV-10 | `RealMixTest`, run by hand: nothing runs the real tier automatically |
| INV-11 | Configure time, every build |
| § 4.8, the device opens and the callback never waits | Partial: by hand for the first half; nothing checks the second |
| § 4.7, speech "audible" to a listener | Partial: INV-10 checks a level margin; that 6 dB means intelligible is unverified until heard |

## 11. Cross-doc impact

- `docs/design.md` § What may depend on what gains a rule: `uaudio`
  depends on `core` alone and contains no SDL.
- `docs/specs/UTA-0019-weapon-set.md` § 9 and UTA-0467 point at this item
  for sound; UTA-0467's body gains UTA-0478 as its other blocker.
- `README.md`'s build prerequisites already name the sound backends'
  headers; nothing changes there.

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract reviews).
Building it is the reviewer. Record:
`docs/reviews/UTA-0056-audio-mixer-loop-log.md`.

## 13. Resource cost

Mixing 64 voices at 48000 Hz is about three million interpolated samples a
second, on the audio thread, not the frame. The scenario's 4 s mixed in
5 to 8 ms (2026-11-17, `RealMixTest` and `ut-mix`, on a machine at load
15), about 0.2 % of one core. `RealMixTest` prints the figure.
