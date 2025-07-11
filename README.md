# Kotor 2 Efficient Load Times
This mod aims to improve the load times for the steam version of Kotor 2

## Status
The mod is in early development, with only initial timing of the LoadingScreen function set up. Currently still exploring and investigating functions in the game.

## What has been identified so far?
- `FUN_00533830` **LoadingScreen**: This appears to be the main function that initiates the loading screens in the game

- `FUN_005582f0` **LoadAndInitialize**: This function likely handles the loading and initialization of mod resources and game scenario data, including mod information, game parameters (time, player data), and setting up various in-game scripts.

- `FUN_00407920` **GameMain**: Most likely the main function of the game

## Currently Identifying:
- `FUN_00703f30`
- `FUN_007047b0`
- `FUN_0053a0c0`
- `FUN_0053a8b0`

## Build Instructions
Download [MinHook](https://github.com/TsudaKageyu/minhook)

Place it in the repository directory and include both the src and lib then run

`build.bat`