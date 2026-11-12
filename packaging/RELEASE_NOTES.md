Early test build of **Polaris**, an AI mix & master assistant plugin (VST3 on Windows, macOS and Linux, plus AU on macOS).

### Download
- **Windows** (FL Studio, Ableton, Cubase, Reaper...): `Polaris-…-Windows.zip`: VST3 plugin + standalone app
- **macOS**: `Polaris-…-macOS.zip`: VST3 + Audio Unit + standalone app
- **Linux** (Reaper, Bitwig, Ardour...): `Polaris-…-Linux-x64.tar.gz`: VST3 plugin + standalone app

Each download has an `INSTALL.txt`. Short version for Windows: copy `Polaris.vst3` to
`C:\Program Files\Common Files\VST3\`, rescan plugins, and load Polaris last on your Master track.

### What's in it
- Chain: 6-band EQ → 3-band glue compressor → saturation (Clean/Warm/Punchy) → mono bass + width → true-peak limiter
- **Drive**: one knob that pushes the whole chain
- **Listen**: analyses your mix and suggests EQ, glue, loudness and stereo moves you can apply or skip
- **Learn reference**: use a track you love as the tonal target
- Loudness meters (LUFS, true peak, LRA), gain-matched **Compare**, **Undo/Redo**, **A/B/C/D** snapshots

### Known limits
- Early build: settings and suggestion rules are still being tuned by ear.
- Builds are unsigned. Windows SmartScreen may warn about the standalone app; macOS needs the `xattr` step in INSTALL.txt.
