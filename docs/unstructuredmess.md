
# unstructured mess

This is mostly an unstructured mess. Things that we want to
write down, but aren't a proper documentation. This will also
randomly switch between german and english, sorry about that.

## hardware so far

* We will use an "Olimex ESP32-S2-DevKit-Lipo" as the microcontroller.
  - We don't plan on connecting a a LiPo-battery though.
* LTE-modem will be a [MikroE LTE IoT 6 Click](https://www.mikroe.com/lte-iot-6-click)
  - this is based on an u-blox SARA R412M, which supports LTE-M and NB-IoT, but can also fall back to EPGRS where that is not available.
  - we will use an [ResIOT SIM](https://sim.resiot.io/), that permits us to use pretty much any mobile network in Europe. In Germany, we can log into all 3 mobile networks with that - so we should be able to get reception even in the middle of nowhere where the CCCamp is.
* SHT41 temperature/humidity sensor
  - SHT45-AD1F, the variant of SHT45 with builtin protective membrane, which makes these variants practically indestructable, should be available from May 30 2023, but it'll then take some more time before breakout-boards using these become available.
  - for now we'll use one of the unprotected SHT41 BOBs we still have, and possibly exchange them later - they are 100% software compatible anyways.
* LPS35HW pressure sensor
  - in the form of an Adafruit breakout board from which we had to desolder the useless and power-wasting always-on LED they insist on putting on.
* LTR390 ambient light / UV sensor
  - in the form of an Adafruit breakout board, on which it seems they at least had the decency to provide an undocumented 'switch' for the useless and power-wasting always-on LED: In revision B, there are two solder pads on the back with a tiny and easily cuttable wire between, labelled "LED". As of April 2023, their documentation page does not mention a revision B or the switch. And I still cannot stop shaking my head about how dumb it is to put an ALWAYS ON LED on a LIGHT SENSOR.
* SEN50 particulate matter sensor
* DFROBOT SEN0483 wind speed (anemometer) and SEN0482 wind direction sensor - both are connected via RS485 (serial with differential voltage). Both are made of metal and look surprisingly sturdy.
* probably an optical RG15 rain sensor
* DFROBOT DFR0627 dual I2C-to-UART-module - because we do not have enough UARTs (the ESP32-S2 only has two in hardware, and one is the system console and the other is the LTE modem), we expand them with this to be able to connect the rain sensor and wind sensors
* Olimex MOD-RS485 RS485-converter - this converts 'normal 3.3V UART' to 'RS485'.
* Case made from wood
  - essentially a stevenson screen / englische Huette
  - with a solar panel on the roof (also see under "solar")
  - Seiten: [Lamellentueren 39,5 x 39,4 cm](https://www.ben-camilla.com/index.php?a=3272)
  - Konstruktion Fuesse noch unklar - sollten abschraubbar sein, damit mans gut transportiert kriegt. Stichwort: Aufschraubmuttern.
* Solar zur Stromversorgung
  - [Solarmodul-Solarladegeraet-Set](https://www.amazon.de/-/dp/B07RZBVTGR/) "20W" (wers glaubt wird selig)
  - solar panel
    + outer dimensions: 406 x 340 mm
    + has 4 holes for mounting at the bottom of the frame, 9 mm x 6mm (rounded), so it will be very easy to screw this onto the wodden roof.
  - alter Bleiakku (AGM) - ist vielleicht nicht mehr gut genug und wir brauchen nen neuen. Andererseits muss er nur ein paar Tage halten und kriegt voraussichtlich ja fast taeglich Sonne.

## wiring ##

* we will use two I2C busses: One for the 3.3V sensors, and one for the 5V powered sensor - just to reduce the risk of accidential 5V to the other sensors. Note that the sensor requiring 5V still has 3.3V logic level for the I2C.
  - For consistency, we will always use green cabling for SCL and yellow cabling for SDA
  - Bus 0 (3.3V): GPIO9 == SCL, GPIO10 - SDA
    + SHT3x - I2C-address: 0x44 1000100b
    + LTR390 - I2C-address: 0x53 1010011b
    + LPS35HW - I2C-address: 0x5d 1011101b
    + DFR0627 - this always responds to 8 I2C-addresses, i.e. only the 4 most significant address bits are really address bits and the rest is abused for function selection, but at least we can set 2 of the 4 via DIP switch. We set both to 1, which results in the device taking up the addresses 1110xxxb, or 0x70 to 0x77 (inclusive)
  - Bus 1 (5V powered device with 3.3V I2C level): GPIO11 = SCL, GPIO12 = SDA
    + SEN50 particulate matter sensor - I2C-address: ?
* LTE module
  - the power wires (GND, 5V, 3.3V) should be routed through our distribution board.
  - ESP32 GPIO1 <-> LTE-module RX
  - ESP32 GPIO2 <-> LTE-module TX
  - ESP32 GPIO3 <-> LTE-module RTS
  - ESP32 GPIO4 <-> LTE-module CTS
  - ESP32 GPIO5 <-> LTE-module PWR
* Olimex MOD-RS485
  - connected with 4 wires (3V3, GND, TX, RX) to the DFR0627 I2C-to-Serial-adapter Port "2"
    + I2C-to-Serial P2 "+" (red) <-> MOD-RS485 "VCC" (Pin 1)
    + I2C-to-Serial P2 "-" (black) <-> MOD-RS485 "GND" (Pin 2)
    + I2C-to-Serial P2 "T" (blue) <-> MOD-RS485 "TXD" (Pin 3)
    + I2C-to-Serial P2 "R" (green) <-> MOD-RS485 "RXD" (Pin 4)
  - MOD-RS485 SCK (Pin 9) and SS# (Pin 10) <-> ESP32 GPIO13 (this is used to switch between "receiving" and "sending" on the half-duplex RS485 bus); we use a splitter cable to connect them both to one I/O pin.
  - Note: If you look at the "UEXT" connector with the notch facing down, then Pin 1 is at the bottom left, 2 is top left, 9 is bottom right, 10 is top right.
* Wind sensors
  - these come with a special outdoor plug, and a matching (pretty long) cable.
  - At the other end of that cable:
    + Red and Black: these are the power supply. These sensors need 7-24V, so connect these to 12+V!
    + Yellow: RS485 "+" / "A" - connect to the screw terminal on Olimex MOD-RS485. These are labeled at the bottom of the PCB.
    + Green: RS485 "-" / "B" - connect to the screw terminal on Olimex MOD-RS485.
* Battery sensor
  - we built an external voltage divider on a stripboard. It consists of a 1 MOhm resistor towards the "+" of the AGM battery, 47 kOhm towards GND, and a small capacitor parallel to the 47 kOhm (to make readings more stable).
  - the middle of the voltage divider needs to connect to ESP32 GPIO6 (a.ka. ADC1_CHANNEL_5)
* WiFi on/off
  - this is not permanently wired, but if you want to turn WiFi on or off, connect GPIO17 to GND for at least 3 seconds.
  - GPIO17 is right next to 5V on the ESP32-S2, be careful not to touch that.
* RG15
  - V+ <-> 5V, white cable, plugged into main power distribution board
  - GND <-> GND, brown cable, for easier cabling plugged into I2C-to-Serial P1 GND
  - Serial In <-> I2C-to-Serial P1 "T" (green), yellow cable
  - Serial Out <-> I2C-to-Serial P1 "R" (blue), green cable

## TODOs

* we currently run the SEN50 non-stop. There is power-saving potential there, as it consumes 63-70 mA that way, vs. 3 when powered down...

