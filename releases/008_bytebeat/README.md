# 08 - bytebeat

A program for generating and mangling <a href="http://canonical.org/~kragen/bytebeat/">bytebeats</a>.

There are 36 built-in bytebeat formulas organized into 6 banks of 6, which are indicated by the LEDs. The last 2 banks contain percussive/drum sounds which need to be triggered with Pulse In 1. 

Bytebeats can also be "live-coded" through the web interface (bytebeat.html) and saved to 6 "user" slots on the card.

Flash this program via [Arudino IDE](https://www.arduino.cc/en/software/) using [earlephilhower's Raspberry Pi Pico Arduino core](https://github.com/earlephilhower/arduino-pico) or use the pre-built UF2. 

This is written for the Proto 1.2 (May 2024) Developer Kit.

##  Controls 

  - Main Pot = Sample Rate (Speed)
  - Pot X = Bank/Formula Select
  - Pot Y = Parameter 1
  - Switch Z Up = Built-in Formulas
  - Switch Z Middle = User Formulas
  - Switch Z Down (momentary) = Reset / Trigger
  
 ##  Inputs

- Audio In 1 = Parameter 1 Modulation
- Audio In 2 = Parameter 2 Modulation 
- CV In 1 = Formula Select Modulation 
- CV In 2 = Sample Rate Modulation
- Pulse In 1 = Reset / Trigger
- Pulse In 2 = Reverse

##  Outputs

- Audio Out 1 = Bytebeat Out
- Audio Out 2 = Next Bytebeat Out
- CV Out 1 = ByteBeat Out (Slow)
- CV Out 2 = ByteBeat Out (Fast)
- Pulse Out 1 = 1Bit Output (Bitbeat)
- Pulse Out 2 = Division of ``t`` (Use as clock for other modules?)


----
Author: [Matt Kuebrich](https://github.com/MattKuebrich)

