# Midi To Kb
Convert midi input into keyboard commands.

## About
The motivation for this project is to allow a midi keyboard to execute arbitrary keyboard commands. As data is streamed from a midi device, it is translated into standard keyboard keys according to a provided input keymap. The result is sent to the host via a virtual USB keyboard device.

## Prerequisites
- Gcc
- alsa-lib

## Building
`make all`
`./miditokb -h`

## Licensing
This project is a fork of amidi from alsa-utils (http://www.alsa-project.org/main/index.php/Main_Page).
