# AcceleratorLocal

A fork of [komashchenko's AcceleratorLocal](https://github.com/komashchenko/AcceleratorLocal) (which is a fork of [asherkin's accelerator](https://github.com/asherkin/accelerator)) with **runtime symbolization** and **Discord webhook reporting** — no [Throttle](https://crash.limetech.org/) upload, everything is processed locally on the server.

## What it does

When the server crashes, a minidump plus a `.txt` metadata file (map, game path, command line and full console history) is written to `addons/accelerator_local/dumps/`.

On the next server start the plugin:

1. Detects the unprocessed crash from the previous session.
2. Re-processes the minidump and **symbolizes third-party (`addons/`) modules in-process** using the bundled Breakpad — symbols are extracted straight from the ELF/DWARF on disk, so no `llvm-symbolizer`, `addr2line` or any other tooling is needed inside a bare steamrt container. Frames resolve to `function @ file:line`.
3. Prints the crash stack with the **suspected culprit** (the first `addons/` frame) to the server console.
4. Sends the report to a **Discord webhook** through the Steam HTTP API, with the full crash `.txt` attached.

Example report:

```
Server crashed: SIGSEGV /SEGV_MAPERR @ 0x67

#0 some_plugin.so + 0x1dcc0 (CBadClass::DoStuff() @ badclass.cpp:39)
#1 some_plugin.so + 0x1db86 (some_command_callback(CCommandContext const&, CCommand const&) @ commands.cpp:56)
#2 libtier0.so + 0x15f682
#3 libengine2.so + 0x3ecb4e

Suspected culprit: some_plugin.so -> CBadClass::DoStuff() @ badclass.cpp:39
```

## Configuration

The configuration file is located at `addons/accelerator_local/accelerator_local.ini`:

```
"accelerator_local"
{
	// Discord webhook URL for automatic crash reports.
	// Leave empty to disable Discord reporting.
	"discord_webhook"	""
}
```

## Notes

- **Crash-loop protection:** if the server crashes twice in a row without ever finishing startup, the pending dump is left unprocessed so the reporting itself can never keep the server down.
- **Symbols:** plugins you want fully symbolized must be deployed **with debug info** (`-g` / RelWithDebInfo, not stripped). Without DWARF the report falls back to symtab function names, and for fully stripped binaries to `module + offset`.
- Reports are queued until the Steam API activates (`GameServerSteamAPIActivated`), then sent — a crash report generated at boot is never lost.
