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

## Feature Requests
- Remap keyboard/gamepad
- NES rom database via CRC32
- Save states
- NSF player
- PAL support
- No sprite overflow
- Zapper support
- FDS support
- Vs. support
- Cheats/Game Genie

## Mapper Support
```
*000 *001 *002 *003 *004 ^005  006 *007  008 *009 *010 *011  012 *013  014  015
†016  017  018 ^019  020 *021 *022 *023 ^024 *025 ^026  027  028  029 *030 *031
 032  033 *034  035  036  037 *038  039  040  041  042  043  044  045  046  047
 048  049  050  051  052  053  054  055  056  057  058  059  060  061  062  063
 064  065 *066  067  068 ^069 *070 *071  072  073  074  075  076 *077 *078 *079
 080  081  082  083  084 ^085  086 *087  088 *089  090  091  092 *093 *094  095
 096 *097  098  099  100 *101  102  103  104  105  106 *107  108  109  110 *111
 112 *113  114  115  116  117  118  119  120  121  122  123  124  125  126  127
 128  129  130  131  132  133  134  135  136  137  138  139 *140  141  142  143
 144 *145 *146  147 *148 *149  150  151 *152  153  154  155  156  157  158 †159
 160  161  162  163  164  165  166  167  168  169  170  171  172  173  174  175
 176  177  178  179 *180  181  182  183 *184 *185  186  187  188  189  190  191
 192  193  194  195  196  197  198  199  200  201  202  203  204  205  206  207
 208  209  210  211  212  213  214  215  216  217  218  219  220  221  222  223
 224  225  226  227  228  229  230  231  232  233  234  235  236  237  238  239
 240  241  242  243  244  245  246  247  248  249  250  251  252  253  254  255

^ - Missing expansion audio
† - Missing EEPROM support
```

## Mapper Wish List
```
18       -- Jaleco SS88006
64, 158  -- RAMBO-1
99       -- Vs.

Namco:           210
MMC3:            74, 192, 206, 76, 88, 154, 95, 112, 119, 191, 118, 155, 194, 195
Stateful:        32, 33, 68, (72, 92), 75, (80, 207), 82, 86, 190, 218
Stateful w/ IRQ: 42, 48, 65, 67, 73, 165
Multicart:       15, 28, 37, 41, 47, 105, 228, 234
```
