# Building MCP DAW from source

MCP DAW is C++20 on [JUCE 8.0.15](https://juce.com) with CMake. JUCE is downloaded automatically
the first time you configure (or point `JUCE_PATH` at a local checkout). The first build compiles
JUCE too and takes several minutes; later builds are incremental.

Outputs (in the build folder):

| Path | What |
| --- | --- |
| `MCPDAW_artefacts/Release/MCP-DAW(.exe)` | The app |
| `MCPDAW_artefacts/Release/mcp-daw-bridge(.exe)` | The MCP stdio bridge (copied next to the app) |
| `mcpdaw_tests_artefacts/Release/mcpdaw_tests(.exe)` | Headless test suite |

## Windows 10/11

Install:

* **Visual Studio 2022** (Community is free) or **Build Tools for Visual Studio 2022**, with the
  *Desktop development with C++* workload. It includes CMake and Ninja.
* **Git**.

Then from a normal command prompt in the repository folder:

```bat
scripts\build-windows.cmd            :: Release build + tests
scripts\build-windows.cmd Debug
```

The script finds Visual Studio, configures `build\Release` with Ninja and runs the tests. To use a
local JUCE checkout set `JUCE_PATH` first; to build elsewhere set `BUILD_DIR`.

Manual equivalent, from the *x64 Native Tools Command Prompt for VS 2022*:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\mcpdaw_tests_artefacts\Release\mcpdaw_tests.exe
```

The C++ runtime is linked statically, so the resulting exes run without the Visual C++
Redistributable.

## Linux (Ubuntu 22.04 or later)

```bash
sudo apt update
sudo apt install build-essential git ninja-build pkg-config \
    libasound2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev
```

CMake 3.25 or newer is required. Ubuntu 24.04's `cmake` package is fine; Ubuntu 22.04 ships 3.22,
so install a current one first:

```bash
sudo snap install cmake --classic        # or: pip install --user cmake
```

Build and test:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/mcpdaw_tests_artefacts/Release/mcpdaw_tests
build/MCPDAW_artefacts/Release/MCP-DAW
```

Audio uses ALSA (on desktop Ubuntu it plays through PulseAudio/PipeWire automatically). ASIO and
JACK are deliberately not used, on any platform.

On Linux the MCP client apps are Claude Code, the Codex CLI and other MCP-capable tools. Point them
at `build/MCPDAW_artefacts/Release/mcp-daw-bridge` (Help → MCP / AI control setup shows ready-made
configs). One known difference: the "MCP: ready · last …" status may keep showing the previous
client after it hands back the connection, because Linux pipes don't report disconnects.

## CMake options

| Option | Default | |
| --- | --- | --- |
| `JUCE_PATH` | *(empty)* | Use a local JUCE 8.0.15 checkout instead of downloading |
| `MCPDAW_BUILD_APP` | `ON` | Build the GUI app |
| `MCPDAW_BUILD_TESTS` | `ON` | Build the test runner |

## Packaging and releases

The app zip (`MCP-DAW-<version>-win64.zip`) is made by `tools/package_release.py` from a Release
build: the two exes, the demo, the Samples Starter Kit, the Python client with example scripts,
the guides as web pages and the licences (layout at the top of the script).

```bat
pip install markdown
python tools\package_release.py --build build\Release         :: -> dist\MCP-DAW-0.4.0-win64.zip
```

GitHub Actions (`.github/workflows/build.yml`) does this for you:

* **Every push to `main` and every pull request:** Windows and Ubuntu 22.04 builds, the test suite,
  a demo check, and the Windows zip (under *Artifacts* on the run's page, kept for 90 days).
* **A version tag** (`git tag v0.4.0` then `git push origin v0.4.0`): the same, then a **draft**
  GitHub Release named after the tag, with the zip attached and the notes taken from this version's
  section of `CHANGELOG.md`. Check it on the *Releases* page and press *Publish*.

To release a new version: raise `VERSION` in `CMakeLists.txt`, add a `## <version>` section to
`CHANGELOG.md`, push, then push the tag `v<version>`.

## Keeping the docs in step

`docs/TOOLS.md` is generated from the build's own tool list:

```bash
python tools/gen_tool_reference.py --effects --bridge build/MCPDAW_artefacts/Release/mcp-daw-bridge
```

(`--effects` opens MCP DAW to read the effect parameters.) The app icon comes from
`tools/make_icon.py` (needs Pillow).
