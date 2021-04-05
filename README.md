## cddNES is moving to [Merton](https://github.com/matoya/merton)
If you're interested in a [Parsec SDK](https://github.com/parsec-cloud/parsec-sdk) integration example, stay here, otherwise [Merton](https://github.com/matoya/merton) is the place to be.

## Overview
cddNES is a cycle accurate NES emulator written in [pure C with no dependencies](/src) (although the [ui](/ui) has a few). The code base is lean with a focus on accuracy and readability--but don't let its minimal nature fool you, it [passes almost every known test](/test) and successfully emulates tricky games such as [Battletoads](https://en.wikipedia.org/wiki/Battletoads_(video_game)) with a only a handful of minor issues listed below.
  
cddNES also serves as the canonical example of a Parsec SDK `HOST_GAME` implementation, with many different renderers supported or on the way. See the [Parsec SDK](https://github.com/parsec-cloud/parsec-sdk) repo for more information.

#### Test Issues
- ppu_nmi_sync: Leftmost pixel is a few short of the Famicom result
- ppu_dpcmletterbox: Slight wiggle periodically, Famicom does not

#### Game Issues
- Micro Machines: very close, but needs revised sprite hit timing after resolving nmi_sync

## Building
UI dependencies are included in this repo as static libraries for convenience. Simply type `make` or `nmake` (on Windows) to build the emulator.

## Parsec Integration
cddNES ships with [Alfonzo Melee](https://www.spoonybard.ca/2018/01/the-alfonzo-game-and-alfonzo-melee.html) as the default ROM for a two player example. As long as the Parsec SDK binary is alongside the cddNES binary, the `Parsec` menu item will appear and allow you to authenticate then share your game.
  
Most of the Parsec SDK specific integration can be found in [main.c](/ui/main.c) and the [render](/ui/render/) directory. All Parsec API calls are wrapped in [api.c](/ui/api.c). See the [Parsec SDK](https://github.com/parsec-cloud/parsec-sdk) repo for more information.

## Command Line Arguments
```
-console                 Spawns a console window on Windows
-headless                Runs cddNES in headless mode. Parsec session must also be supplied.
-session=SESSION_ID      Start cddNES with an authenticated Parsec session
```
