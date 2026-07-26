![Music Thing Workshop System with headphones](https://www.musicthing.co.uk/images/900-workshopsystem-full-straight-headphones-colour-2.jpg)

# Workshop Computer  

[**CLICK HERE TO FIND PROGRAM CARDS TO DOWNLOAD**](https://tomwhitwell.github.io/Workshop_Computer/index.html) 


Dev material for the Music Thing Workshop Computer  
[Music Thing Workshop System Homepage](https://www.musicthing.co.uk/workshopsystem/)  
Further discussion in the Discord - invite in the documentation below.   

### DOCUMENTATION 

At the moment, [this Google doc is the most up-to-date shortform documentation](https://docs.google.com/document/d/1NsRewxAu9X8dQMUTdN0eeJeRCr0HmU0pUjpKB4gM-xo/edit?usp=sharing) for pinouts and hardware details

### Demonstrations+HelloWorlds

Starter code in various platforms - Arduino, Pico SDK, CircuitPython, Micropython

By far the most developed is [Chris Johnson's ComputerCard library](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard) for Pico SDK and Arduino.

The easiest way to get started building, flashing, and debugging ComputerCard firmware
is the **Visual Studio Code DevContainer**, that automatically installs everything you need into a tiny linux environment on your (non-workshop) computer and makes building, flashing, and debugging much easier. See
[.devcontainer/README.md](.devcontainer/README.md) to get going.


### RELEASES 

[Working and work-in-progress code for many program cards.](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases) 

My suggestion for the first 100 projects is that people grab numbers & folders in the 'releases' folder - by sending pull requests - then share whatever they're comfortable sharing - uf2, source, just documentation, or just a link to your own repo/web/gists, whatever works best. I don't have much experience of this kind of collective development, so would be delighted if better approach emerges.  

Release documentation: I've been making [little](https://docs.google.com/presentation/d/19z0S9cpGnyhb7lVmBPHYjTZLpEB-Xg-v9zzfXCjCjOQ/copy) [leaflets](https://docs.google.com/presentation/d/10R8onfP5JAq9MpOgVSa4sAhxg-WTx7_0-Q1fY0MUDho/copy) for each card, designed in Google Sheets, but you might experiment with other types of documentation   

### Optional pre-commit validation

Program card contributors can enable the repository's pre-commit checks for this clone:

1. Run `npm ci --prefix tools/sitegen`.
2. Run `npm run hooks:install`.

The hook runs only when staged changes touch `releases/`. It validates the exact staged snapshot, so unstaged edits do not affect the result. Run `npm run validate-staged` to invoke it manually. The hook reports advisory warnings and blocks commits only for malformed YAML, missing or invalid required core metadata, or an internal validation-rule crash.

Hook installation changes only this clone's `core.hooksPath`. If another hook manager is already configured, call `npm run validate-staged` from that manager instead. `git commit --no-verify` bypasses the local hook, but pull-request validation still runs in CI.


