<p align="center">
  <img src="docs/banner.png" alt="MixLauncher" width="860">
</p>

<h1 align="center">MixLauncher</h1>

<p align="center">
  A fast launcher for Windows 10/11.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0b0b0d?style=flat-square" alt="Windows 10 | 11">
  <img src="https://img.shields.io/badge/size-~600%20KB-0b0b0d?style=flat-square" alt="~600 KB">
  <img src="https://img.shields.io/badge/no%20telemetry-0b0b0d?style=flat-square" alt="No telemetry">
</p>

## Features

- **Start menu replacement.** Opens with `Win` or any other key, even over games.
- **Apps.** Everything from the Start menu and the Microsoft Store. Frequently used ones rank higher.
- **Files.** Searches the whole disk through a bundled [Everything](https://www.voidtools.com/). If you already run your own Everything, that one is used.
- **Wrong keyboard layout.** `rfkr` finds Калькулятор, `сщву` finds VS Code.
- **English names.** `notepad`, `cmd`, `calc` find Notepad, Command Prompt and Calculator on a localized Windows.
- **Commands.** Lock, sleep, restart, shut down, sign out, empty recycle bin.
- **Start button.** Custom icon on the Windows 11 Start button; clicking it opens the launcher.
- **Now playing.** Cover art, title and playback controls for any player, right on the taskbar.
- **Styles.** Four launcher looks: Standard, Raycast, Windows 11 and Compact. Matches are highlighted in bold.
- **Updates.** When a new version is released on GitHub, an "Update" button appears in the launcher. The check can be turned off.
- **No AI, no telemetry.**

## Installation

Download from the [Releases](https://github.com/mel1x/mixlauncher/releases) page:

- `MixLauncher-Setup.exe` - installer, Everything included.
- `MixLauncher-portable.exe` - no installation; file search requires Everything to be installed.

## Hotkeys

| Keys | Action |
|---|---|
| `Win` | open or close |
| `↑` `↓`, `PgUp` `PgDn` | select |
| `Tab` | next section |
| `Enter` | open |
| `Ctrl+Enter` | show in folder |
| `Ctrl+Shift+Enter` | run as administrator |
| `Alt+Enter` | properties |
| `Ctrl+C` | copy path |
| `Esc` | clear the query, press again to close |
| `Ctrl+,` | settings |

## Settings

`Ctrl+,`, the button in the launcher's corner, or the tray icon menu. Stored in `%LOCALAPPDATA%\MixLauncher\config.ini`.

## Building

Requires MinGW-w64 (MSYS2) or Visual Studio Build Tools; the installer needs [Inno Setup 7](https://jrsoftware.org/isdl.php).

- `build.bat` - builds `build\mixlauncher.exe`; `build.bat dev` - without the administrator prompt.
- `release.bat` - builds the launcher and `build\MixLauncher-Setup.exe`. The version is taken from `res\version.h`.
