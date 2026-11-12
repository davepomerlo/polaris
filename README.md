# Polaris — AI mix & master assistant (working title)

A VST3 plugin for the mix bus and master. It listens to your track, explains what's off in plain words,
and builds a starting chain you can drive with one knob or edit in full.

Inspired by the *ideas* behind one-knob mix-bus chains, Ozone's Master Assistant and FabFilter's
workflow. No code, presets, names or look are copied from any of them.

## Download

Get the latest build from **[Releases](https://github.com/davepomerlo/polaris/releases/latest)**:

| System | File | Contains |
| --- | --- | --- |
| Windows (FL Studio, Ableton, Cubase, Reaper...) | `Polaris-…-Windows.zip` | VST3 plugin + standalone app |
| macOS | `Polaris-…-macOS.zip` | VST3 + Audio Unit + standalone app |
| Linux x64 (Reaper, Bitwig, Ardour...) | `Polaris-…-Linux-x64.tar.gz` | VST3 plugin + standalone app |

Each download has an `INSTALL.txt`. On Windows: copy `Polaris.vst3` to `C:\Program Files\Common Files\VST3\`,
rescan plugins in your DAW, and load Polaris last on the Master track.

Builds are early and unsigned: Windows SmartScreen may warn about the standalone app, and macOS needs one
Terminal command (in `INSTALL.txt`) before it will load the plugin.

## What's in this version (Phase 0 + first pass of Phase 1)

| Part | Status |
| --- | --- |
| Chain: input → 6-band EQ → 3-band glue comp → saturation → stereo/mono-bass → true-peak limiter | Working |
| **Drive** macro (one knob pushes EQ, glue, saturation and limiter) | Working, numbers to tune by ear |
| Character: Clean / Warm / Punchy saturation (anti-aliased, zero latency) | Working |
| Loudness meters: momentary, short-term, integrated LUFS, LRA, true peak (BS.1770-4) | Working, unit-tested |
| Gain-matched **Compare** (hear the dry mix at the same loudness) | Working |
| **Listen** → measurements → suggestion cards with Apply / Apply all | Working (rules engine v1) |
| **Learn reference** → use a track you love as the tonal target | Working |
| Spectrum with target band, your analysed mix, and draggable EQ nodes | Working |
| Simple mode / Pro mode (all module controls) | Working |
| Undo / Redo (one knob move or Apply all = one step, 100 steps) | Working, unit-tested |
| Snapshots A/B/C/D (saved with your project; Shift-click copies the current sound into a slot) | Working |
| Dynamic EQ bands, linear phase, dither, ML models | Not yet (see the build plan in the spec) |

## Build it yourself on Windows 11 (optional)

You need:

- **Visual Studio 2022** (free Community edition) with the *Desktop development with C++* workload
- **CMake** 3.22+ (included with Visual Studio, or from cmake.org)
- **Git**

Then, in *x64 Native Tools Command Prompt for VS 2022*:

```bat
git clone https://github.com/davepomerlo/polaris.git
cd polaris
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The first configure downloads JUCE 8 (about 200 MB), so it takes a few minutes.

The plugin ends up in `build\Polaris_artefacts\Release\VST3\Polaris.vst3`. Copy that folder to
`C:\Program Files\Common Files\VST3\`, then in FL Studio open **Options → Manage plugins → Find more plugins**.
Load Polaris on the **Master** mixer track (last slot).

There's also a standalone app in `build\Polaris_artefacts\Release\Standalone\` for quick tests.

## How to use it

1. Put Polaris last on your master. Pick a **Target** (Streaming −14, Loud −9, Club −7 LUFS) and a **Style**.
2. Play the loudest 20–30 seconds of your song and press **Listen**. Press again to stop early.
3. Read the cards. **Apply** the ones that sound right, or **Apply all**.
4. Ride **Drive** for more glue, density and loudness. Toggle **Compare** to check you're improving the
   mix, not just making it louder.
5. Want your own target? Play a finished track you love through Polaris and press **Learn reference**.
   Style switches to *My reference*.

Use **A/B/C/D** to compare settings: tweak on A, click B (it starts as a copy), change something, then flip
between them. **Undo/Redo** cover every change, including snapshot switches.

In **Pro mode**, drag the numbered EQ nodes on the spectrum (scroll on a node for Q, double-click to reset).

## Code layout

```
Source/core/        JUCE-free DSP + assistant (unit-tested, portable)
  MasterChain.h     the chain and the Drive mapping (applyDrive)
  Limiter.h         lookahead true-peak limiter
  GlueCompressor.h  3-band LR4 glue compressor
  Saturator.h       ADAA saturation (Clean/Warm/Punchy)
  StereoTool.h      mono bass + width
  CorrectiveEq.h    6-band EQ
  LoudnessMeter.*   BS.1770-4 / EBU R128 meter
  Analyzer.*        Listen measurements (spectrum, loudness, correlation, clipping...)
  Assistant.*       target curves + suggestion rules
  ParamHistory.h    undo/redo history with settle-time grouping
Source/             JUCE plugin: processor (audio + analysis thread) and editor
Source/ui/          spectrum, meters, assistant cards, module panels
tests/              core unit tests
```

The audio thread never allocates or locks. Analysis (meters, spectrum, Listen) runs on a background
thread fed by lock-free FIFOs.

## Tuning notes

- **Drive curve:** `applyDrive()` in `Source/core/MasterChain.h`. The numbers are a starting point — tune
  them against masters you like.
- **Style targets:** `targetCurve()` in `Source/core/Assistant.cpp`. Heuristic slopes; *Learn reference*
  usually beats them.
- **Suggestion rules:** `makeSuggestions()` in the same file. Every move is capped (EQ ±4 dB, broad Q).

## Run the core tests without JUCE

```bash
cmake -B build-core -DPOLARIS_CORE_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Making a release

Push a version tag and GitHub Actions builds Windows, macOS and Linux and publishes a release with the downloads:

```bash
git tag v0.2.0
git push origin v0.2.0
```

## Licence notes

JUCE 8 is dual-licensed (AGPLv3 or commercial). If you plan to sell Polaris closed-source, check JUCE's
current licence tiers first. Pick the final product name after a trademark search.
