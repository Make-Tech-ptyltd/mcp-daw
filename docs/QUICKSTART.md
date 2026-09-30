# MCP DAW — Quick Start

MCP DAW is a loop and one-shot music arranger that you and an AI (Claude, ChatGPT and others) can
work on **together, in the same project, live**. You drag, loop and mix by hand; the AI can do the
same through MCP (Model Context Protocol) while you watch — and every change it makes can be undone.

Free and open source, built by Make Tech Pty Ltd — [maketech.com.au](https://maketech.com.au).
Version 0.4.0 (public beta), Windows 10/11 64-bit.

---

## 1. Unzip it

Right-click the downloaded zip → **Extract All…** and pick a normal folder, for example
`Documents\MCP DAW`. Don't run it from inside the zip window, and avoid `Program Files`.

You'll see:

| File | What it is |
| --- | --- |
| `MCP-DAW.exe` | The app — double-click to start |
| `mcp-daw-bridge.exe` | The connector AI apps use to talk to MCP DAW (you never run it yourself) |
| `Quick Start.html` | This guide |
| `Demo\` | The one-click demo show, and its three songs as projects |
| `Samples-Starter-Kit\` | Drums, percussion, bass notes and vinyl crackle to start with |
| `Scripting\` | For people who want to control MCP DAW from Python |
| `Licences\` | The legal bits, and where to get the source code |

Keep the two `.exe` files together in the same folder.

## 2. First start: "Windows protected your PC"

MCP DAW is new and not code-signed (signing certificates cost money every year, and this is a free
project), so Windows SmartScreen will show a blue warning the first time:

1. Click **More info**.
2. Click **Run anyway**.

Windows remembers your choice. Some antivirus programs also ask about new programs — allow
`MCP-DAW.exe` and `mcp-daw-bridge.exe`. The complete source code is public
([github.com/Make-Tech-ptyltd/mcp-daw](https://github.com/Make-Tech-ptyltd/mcp-daw)), so anyone can
check exactly what they do.

## 3. Watch the demo

Open the `Demo` folder and double-click **Run Demo**, and turn your speakers up. MCP DAW opens by
itself and, over about four minutes, builds and performs three songs while they play: a drum & bass
track written bar by bar, a halftime dub track it switches to on the beat, and a hip-hop beat it
reaches by braking the tempo from 174 to 92 BPM - then leaves that one looping for you. Every clip,
fader move, effect and tempo change is an MCP command; the console window narrates. Keep your hands
off the app until the console says *Done*. No AI subscription or programming needed.

The three songs are in the `Demo` folder as projects too - `kens-demo-2.mcpdaw` (drum & bass),
`kens-demo-3.mcpdaw` (halftime dub) and `kens-demo-1.mcpdaw` (the hip-hop beat). Open one with
File → Open to see how it is put together, or ask your AI to remix it.

## 4. Make music yourself

* **Add sounds:** drag WAV files from the Browser panel (left) or Windows Explorer onto the arrangement.
  The Browser starts in the **Samples Starter Kit** (it's also under *Places*): click a file to hear it.
* **Loop:** drag the **right edge** of a clip to repeat it. Drag in an **empty lane** to paint the
  track's sound along it.
* **Move / copy:** drag a clip; hold **Ctrl** to copy, **Alt** to ignore the grid.
* **Select several:** hold **Ctrl** and drag a box in an empty area.
* **Play / stop:** Space. **Loop on/off:** L. **Zoom:** Ctrl + mouse wheel. **Fit everything:** F.
* **Swap a sound:** right-click a track name → *Replace audio*, rest the mouse on a file to hear it.
* **Pitch:** right-click a clip → *Pitch* (−12 … +12 semitones).
* **Effects:** select a track, then use the FX Rack buttons (EQ, compressor, reverb, delay, distortion).
* **Undo anything:** Ctrl+Z (including everything an AI did).
* **Save:** Ctrl+S (`.mcpdaw` project). **Export audio:** File → Render.

## 5. Connect an AI

In MCP DAW open **Help → MCP / AI control setup…**. The window shows ready-made settings with
**Copy** buttons.

### Claude Desktop

1. In MCP DAW click **Copy Claude Desktop config**.
2. In Claude Desktop: **Settings → Developer → Edit Config**. This opens `claude_desktop_config.json`.
3. Paste the copied text into it and save. If the file already has an `"mcpServers"` section, add
   just the `"mcp-daw": { ... }` part inside it (separated from the others by a comma).
4. **Quit Claude completely** (right-click its icon near the clock → Quit) and open it again.

### ChatGPT / Codex

1. In MCP DAW click **Copy Codex config**.
2. Paste it at the end of `%USERPROFILE%\.codex\config.toml` (create the file if it doesn't exist).
3. Restart Codex.

### Cursor, VS Code and other MCP apps

Use **Copy VS Code / Cursor config** and add it to that app's MCP settings.

### Then just ask

> "Open MCP DAW and make a dusty 90 BPM hip-hop beat with the samples in my Music folder."

MCP DAW doesn't start when your AI app starts — it opens the moment the AI actually sends it a
command. The top-right corner shows who is connected (**MCP: Claude**, **MCP: ChatGPT**…) and the
bottom bar shows each command as it arrives.

**Taking turns:** one AI or script uses MCP DAW at a time. With *Take turns* ticked (the default, in
Help → MCP / AI control setup) whoever is working holds the connection and hands it back after
20 seconds without commands, so you can switch between Claude, ChatGPT and scripts freely. Untick it
to lock MCP DAW to the first AI that connects.

## 6. Troubleshooting

| What you see | What to do |
| --- | --- |
| The AI says *"MCP DAW is in use by …"* | Another AI or script is working. Wait ~20 s and ask again. |
| The AI says *"MCP DAW isn't running…"* | Keep `MCP-DAW.exe` and `mcp-daw-bridge.exe` in the same folder, or open MCP DAW yourself. |
| Claude shows *mcp-daw* as failed | You moved the MCP DAW folder: copy the config again (step 5) and restart Claude. |
| No sound | Click **Audio** (top right) and pick your output device. |
| Top right says **MCP: off** | MCP couldn't start. Close MCP DAW and open it again. |

## 7. Feedback and support

* Questions, bugs, ideas: **ken@maketech.com.au** (or the ♥ menu → *Send feedback*).
* MCP DAW is free. If it's useful to you, the ♥ menu has a **Donate** link — thank you!

MCP DAW is open source under the GNU AGPL v3 (see `Licences\LICENSE.txt`); third-party components are
listed in `Licences\THIRD_PARTY_NOTICES.txt`, and `Licences\SOURCE.txt` says where to get the source
code. The samples in `Demo` and `Samples-Starter-Kit` have their own terms (the `README.txt` /
`CREDITS.txt` in those folders): free to use in your own music, not to resell as samples.
