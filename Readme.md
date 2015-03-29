# What is this about? #
This project aims to connect a Sun Type 5c I found to a pc without any drivers and only with the help of 2 Attiny85s.

## Why don't I keep this to myself but publish it to github? ##
Because I am a generous person and don't want to keep this to myself. Also if someone finds one of those awesome keyboards, they should be able to use it.
And I want a central storage for all my stuff/documentation and file versioning. Mostly the last one. ^^

## Why 2 ICs and not 1? ##
Initially I planned to do all the polling and USB reporting on a single AT85; but that wouldn't work. 
The realtime components of emulating a USB device via software [(V-USB)](http://www.obdev.at/products/vusb/index-de.html) and the relatively low baud on the keyboard side of 1200 conflicted.
So two should resolve that hurdle by decoupling those two requirements while still allowing me to complete this project without expensive and larger flasher and boards.

## So what's the plan, chief? ##
Basically there is a master AT85, which handles USB communication and a buffer AT85 which handles communication with the keyboard and acts as a two-way buffer for events and commands.

# Now it's gonna get technical, be warned. #

### USB ###
The hardware-USB-implementation is heavily *cough* borrowed *cough* from [codeandlifes.com blog entry](http://codeandlife.com/2012/02/22/v-usb-with-attiny45-attiny85-without-a-crystal/).
The plan looks like this: ![planned layout][documentation/images/pcb.png]
Software is (as of right now) based on [another blog entry](http://codeandlife.com/2012/06/18/usb-hid-keyboard-with-v-usb/) and extended by myself.

### Keyboard interface ###
The Type 5c uses an async 1200 baud non-inverted signal with one bit to signal the start of a transfer and then following 8 bit. If the last bit is high, then this is the last bit in this message. Else the reciever should wait for more packets.
That is my personal experience using a logic analyzer, so official documentation might differ, but that is really hard to prove with the lack of official documentation out there. I found a document describing general Sparc keyboard, altough I lost the sourcelink. You can find it it [documentation/Sparcs.pdf](documentation/Sparcs.pdf)


### Tiny-Tiny-interface ###
The two IC's are connected via 2 wired, both with a 4.7K pull-down to ground. The slave signals communication by rasing its flow-control-line high, so the master doesn't request any data. If the flwcntrl is low, then the master can send a 8-bit command to the slave, by raising it's txd for 70µ, then sending each bit for 70µs afterwards. The slave never initiates transfers.
If the command requires a respone such as ("what keys where pressed?") the master raises the flwcntrl line for each bit and the slave shall send one bit as long as the flwcntrl line is kept high. The master then pulles it low for 10µs and requests the next bit. *This is slow but probably needed to keep the master idling enough for v-usb to work*.
