# Grind Knob Tracking & Thermal Drift Calibration

This guide covers the installation of the hardware **KY-024 Hall-effect sensor** integration for real-time grind knob tracking, the **6-point dial calibration** procedure, the physics behind raw ADC drift, and how to use the automated **Thermal Drift Calibration** tool.

---

## Material needed and cost approximation

| Material | Approx cost in $ | Comment |
|----------|------------------|---------|
| [KY-024 linear Hall-effect sensor](https://www.az-delivery.de/en/products/hall-sensor-modul) | < 1 |  |
| small neodymium magnet | < 1 |Size is 5mm diameter x 2mm thickness | Probably you'll spend around 5€ as they sell pack of 50 or 100
| Wires | |  about 10cm length and 30/35cm ESP32 to KY-024 |
| [Small 3D printed case](3d_files/Hall_sensor_adapter.stl) |  | I used PLA but I believe PETG is totally fine  |
| Soldering iron and solder | n/a | |


## Preparation

First of all, print the 3D case model and while in the mean time you can check the 3 pins where the sensor attach on the board and dissoldered them. Map them for safety. [From the board you then need to sodler the 3 short cables (10cm iish) from to the sensor so it's freely movable](media\Digital_knob_installation\Step_0b.png).
Regarding the ESP32 adaptation with also the HX711 original smart grinder project, You need an extra pin usage which is **PIN 5** where you get the A0 (analog output of the KY-024). Obviosly you need +3.3V and GND too. To integrate as in the original prject, I used a [5pin JST connector](media\Digital_knob_installation\Step_0a.jpg) to make it nicer and easier to connect everything. That's up to you.

---

## Installation steps

Once you have everything, here are the installation steps.

### Step 1
Start with disassembly the entire Grinder, first from the top part, screen, etc, then the bottom part. **MAP the fast connector that goes onto the PCB board before removing. You can check in mine but I am not sure they are the same**. Finally, once the motor is the last piece in the case, remove the 4 screw from the head and lift it up. If unsure as it is heavy and you wanna be gentle, turn first the grinder case on the side and go one with the operation.

![Step 1 – Disassembly](media/Digital_knob_installation/Step_1.jpg)

### Step 2
 **Put some tape to cover the holes of the motor otherwise if a nut or a screw goes in it might be a real pain in the a__ removing it**

![Step 2 – Tape motor holes](media/Digital_knob_installation/Step_2.jpg)


### Step 3

Unscrew the nut from the bottom of the motor and then put the KY-024 (within the case) onto the attachment as in the picture. Be careful when you remove the nut as it is firmly keeping the the screw tip for a "maximal" distance calibration. I also used some tape for the cable and for keep the head of the sensor firmly attach. You can also glue it if you want. Up to you

![Step 3 – Install sensor](media/Digital_knob_installation/Step_3.jpg)

Following, tight the night and be sure you align properly the sensor head with the shaft where the magnet will go.

### Step 4

Put the magnet on the shaft and align with the sensor head like in the picture. It should be relative stable, but I added a bit of glue to keep it in place. 

![Step 4 – Magnet placement](media/Digital_knob_installation/Step_4.jpg)

### Step 5

Once happy you can tight the nut back if you didn't and I put some tape to avoid the cables to move around. Then you can put the entire motor head back in place and screw it

![Step 5 – Tight nut and final assembly](media/Digital_knob_installation/Step_5.jpg)

### Step 6
When you have putting the motor back in the case, be careful on the corner where the shaft goes, as the sensor is also there. You need to be gentle putting back, as there only little room. For this reason I didn't use a zip to tight the head of the sensor.




### Step 7

Connect the cable (3 pins) to the board and put back the fast connector in the same spot as before. And you can proceed now with the reassembly of the entire grinder.


