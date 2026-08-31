# ShaderToyX

[![CI](https://github.com/vinay/ShaderToyX/actions/workflows/ci.yml/badge.svg)](https://github.com/vinay/ShaderToyX/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/vinay/ShaderToyX?include_prereleases)](https://github.com/vinay/ShaderToyX/releases/latest)

A small, native [Shadertoy](https://www.shadertoy.com)-style shader playground for Windows.
Write a GLSL `mainImage()` function in the built-in editor, hit **F5**, and watch it run on
your GPU — no browser, no dependencies, one executable.

ShaderToyX is written in plain C-style C++ against Win32 and OpenGL 3.3 Core. It has no
third-party libraries: the GL loader, the editor panel and the renderer are all in the
five source files under `ShaderToyX/src/`.

> ShaderToyX is an independent project and is not affiliated with or endorsed by
> Shadertoy or Beautypi.

## Features

- Live GLSL editing with the same entry point and uniforms as Shadertoy
  (`mainImage`, `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iMouse`, `iDate`,
  `iSampleRate`, `iChannelTime`, `iChannelResolution`, `iChannel0`–`iChannel3`)
- GLSL syntax highlighting, auto-indent on Enter, and a brace-based
  re-indenter (**Ctrl+Shift+F**) — built on the stock Windows RichEdit
  control, no third-party editor component
- Multi-pass rendering: an **Image** tab plus up to four **Buffer** tabs (A–D),
  each backed by a ping-pong pair of RGBA16F framebuffers
- **Sound shaders**: a Sound tab with Shadertoy's `mainSound()` entry point,
  evaluated on the GPU and streamed to the default output via WASAPI;
  the speaker toolbar button mutes/unmutes
- Compile errors shown per tab, with line numbers matching your code
- Pause / resume and reset-time controls, FPS and resolution readout
- One-click **MP4 recording** of the canvas (H.264 + AAC via Media Foundation,
  hardware-encoded where available) — no capture software needed
- Per-monitor DPI aware
- Toggle the editor panel with **F1** for a full-window canvas
- Borderless fullscreen with **F11** or the fullscreen button (**Esc** to exit)

## Download

Prebuilt 64-bit binaries are on the
[Releases page](https://github.com/vinay/ShaderToyX/releases/latest). Download
`ShaderToyX-<version>-win64.zip`, extract it anywhere, and run `ShaderToyX.exe`. There is no
installer and nothing is written outside the folder you extract to. A matching
`-symbols.zip` with the `.pdb` is attached to each release for crash analysis.

ShaderToyX needs a GPU driver with OpenGL 3.3 support, which every Windows 10/11 machine
with up-to-date graphics drivers has.

## Building

**Requirements:** Windows 10 or later and the MSVC C++ toolchain (Visual Studio 2026 or the
standalone Build Tools, with the *Desktop development with C++* workload). `build.bat` works
with any recent MSVC version; the `.vcxproj` targets the `v145` toolset, so older Visual
Studio versions will need to retarget it to build from the IDE.

### From the command line

`build.bat` calls `cl.exe` directly (no MSBuild) and writes the executables to a `build/`
directory in the repository root. It expects the MSVC **x64** toolchain on `PATH`, so run it
from an *x64 Native Tools Command Prompt for VS*, or call `vcvarsall.bat x64` first:

```
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd \path\to\ShaderToyX
build.bat
```

```
build.bat            Build Debug and Release
build.bat debug      Build Debug only        -> build\debug\ShaderToyX.exe
build.bat release    Build Release only      -> build\release\ShaderToyX.exe
build.bat package    Build Release and zip it -> build\ShaderToyX-<ver>-win64.zip
build.bat clean      Delete the build\ directory
```

The script refuses to run if `cl.exe` is missing or the toolchain on `PATH` targets
something other than x64.

`build.bat package [version]` produces the same zip files that a GitHub release contains.
The version defaults to `git describe --tags`, so `build.bat package v0.1.0` and a plain
`build.bat package` on a tagged commit give identical output.

### From Visual Studio

Open `ShaderToyX.slnx`, pick a configuration, and build. Output goes to the usual
`ShaderToyX\x64\<Configuration>\` folder.

Debug builds request a debug OpenGL context and log driver messages to the Visual Studio
*Output* window (via `OutputDebugString`) when the driver supports `KHR_debug`.

## Using it

| Action | How |
| --- | --- |
| Compile the current shader | **F5** or the **Compile** button |
| Re-indent the current tab | **Ctrl+Shift+F** (rewrites leading whitespace only) |
| Show / hide the editor panel | **F1** |
| Enter / leave fullscreen | **F11** or the fullscreen button; **Esc** leaves |
| Add a buffer or sound pass | The **+** button on the tab bar opens a menu |
| Remove a buffer or sound pass | The **x** on the tab |
| Mute / unmute sound | The speaker button |
| Record the canvas to MP4 | The record button (click again to stop) |
| Pause / resume time | The pause button |
| Reset `iTime` and `iFrame` to zero | The rewind button |
| Feed the mouse to `iMouse` | Click and drag on the canvas |

The editor panel keeps a separate source buffer and error log for each tab.
Compiling (F5) rebuilds every visible tab.

Recording writes `ShaderToyX_<date>_<time>.mp4` next to the executable
(falling back to your Videos folder if that location is not writable). The
sound shader's audio is included when the Sound tab is active at record
start; the speaker mute only affects monitoring, not the recording. Pausing
time also pauses the recording, so the file has no frozen stretches.
Resizing the window (including entering or leaving fullscreen) stops the
recording, since an H.264 stream cannot change resolution mid-file.

The re-indenter is deliberately conservative: it only rewrites each line's
leading whitespace from brace/paren depth (block-comment interiors are left
alone, preprocessor lines go to column 0), so it can never alter the code
itself. One consequence: single-statement `if`/`else` bodies without braces
are flattened to the surrounding depth.

### Shadertoy compatibility notes

- `iMouse` follows Shadertoy's convention: `.xy` is the drag position, `.z` is the click x
  (positive while the button is held, negative after release) and `.w` is the click y
  (positive only on the frame the button went down).
- `iChannel0`–`iChannel3` are always bound to Buffer A–D respectively. Textures, video,
  audio, keyboard and cubemap inputs are not supported.
- **Buffer ordering differs from Shadertoy.** Here every buffer pass samples the
  *previous* frame's output of every buffer, including buffers that ran earlier in the
  same frame. On Shadertoy, Buffer B reading Buffer A sees A's *current* frame. Shaders
  that chain buffers within a frame will be one frame behind per hop.
- `iTimeDelta` keeps reporting the real frame time while paused.
- Resizing the window recreates the buffers, which clears their contents.
- **Sound shaders** run at 44.1 kHz, 16-bit stereo, generated on the GPU in
  ~1.5-second blocks a few seconds ahead of playback. Unlike Shadertoy there is
  no 3-minute cap — the stream plays for as long as the app runs. Recompiling
  swaps the sound in place without rewinding; the reset button rewinds it to 0
  along with `iTime`. `iChannel0`–`iChannel3` are bound to Buffer A–D as usual,
  but a sound block samples whatever those buffers held when the block was
  generated (up to a few seconds early). `iTime`-style per-frame uniforms are
  available but only meaningful per block; use the `time` argument instead.

## Continuous integration and releases

Every push and pull request to `master` runs the [CI workflow](.github/workflows/ci.yml)
on a Windows runner: it builds Debug and Release with `build.bat`, builds the `.vcxproj`
with MSBuild as a sanity check, and uploads the Release executable as a workflow artifact.

Pushing a tag that starts with `v` runs the [Release workflow](.github/workflows/release.yml),
which builds, packages with `build.bat package <tag>`, and publishes a GitHub Release with
auto-generated notes and the two zip files attached. To cut a release:

```
git tag v0.1.0
git push origin v0.1.0
```

## Project layout

```
ShaderToyX/src/
  main.cpp      Win32 window, OpenGL context, main loop, uniform setup
  editor.*      Editor panel: tabs, toolbar, RichEdit code box with GLSL highlighting
  renderer.*    Full-screen quad and ping-pong framebuffers for the buffer passes
  audio.*       WASAPI playback for sound shaders (shared-mode render stream)
  recorder.*    MP4 recording via the Media Foundation sink writer (H.264 + AAC)
  shader.*      Wraps user code in a Shadertoy-compatible fragment shader and links it
  gl_lite.*     Minimal OpenGL 3.3 function loader (only what the app uses)
ShaderToyX/
  ShaderToyX.vcxproj    Visual Studio project (IDE builds)
  ShaderToyX.manifest   Application manifest (per-monitor DPI awareness)
.github/workflows/      CI and release automation
build.bat               Command-line build and packaging script
```

## License

ShaderToyX is released under the [MIT License](LICENSE).
