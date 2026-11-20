# Dominatrix

A master-bus glue compressor (VST3 / AU) by **Castle Danger Music Company**.

- **DISCIPLINE:** one knob. Threshold (follows the mix's level), ratio, program-dependent attack, two-stage auto release and makeup are all automatic. Sidechain is high-passed at 90 Hz and stereo linked.
- **SATURATION** with five types: TAPE, TUBE, IRON, CONSOLE, WHIP (level-compensated, crossfaded switching)
- **ANALOG:** wow & flutter, head bump, HF roll-off, L/R mismatch, crosstalk, component non-linearity, hiss & hum, slow drift
- **OUTPUT + CEILING (-1 dB / -6 dB / OFF):** replaces the limiter. OUTPUT drives a soft, transparent clipper; a final look-ahead stage makes the ceiling a true-peak ceiling.
- **WIDTH (1.1):** mid/side stereo width 0-200 % (lows below 120 Hz never widened, loudness compensated), BASS MONO switch, PHASE correlation meter
- **Extras:** MIX (phase-aligned parallel), QUALITY (2x / 4x / 8x oversampling, latency reported to the host), MATCH (level-matched listening), GR needle meter, live read-out of the automatic settings, IN / CLIP / OUT meters, short-term + integrated LUFS, factory + user presets

See `controls.txt` for every control and `readme.txt` for installing.

## Building

Pushing to `main` runs GitHub Actions, which builds macOS (VST3 + AU, universal) and Windows (VST3), then:

- uploads the builds as workflow artifacts, and
- pushes the release zips to the `downloads-macos` / `downloads-windows` branches.

A Linux job runs the headless checks (`test/snapshot.cpp`): ceiling and true-peak tests for every saturation type and quality setting, transparency, DISCIPLINE behaviour and level independence, saturation and ANALOG character, MIX alignment, WIDTH / BASS MONO / phase meter, MATCH, LUFS accuracy, latency and presets. It also renders the editor to PNGs (the `gui-snapshots` artifact).

Local build:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Dominatrix_VST3
```

Built with JUCE 8.0.15 (same as DREAD, Possessed, Dead Air and Cutthroat).
