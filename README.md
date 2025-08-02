# UberKB

Quick and dirty implementation of linux kernel keyboard remapping.

## Dependencies

1. cex.h (included)
2. libevdev 

## Installation

```
# clone the uberkb repo

# Install Dependencies
sudo apt install gcc pkg-config libevdev-dev

# prime cex build system 
gcc ./cex.c -o cex

# Installing app
./cex app build uberkb 

# List available keyboards
sudo ./cex app run uberkb

# Install as system service
sudo ./cex install '<your keyboard here>'

```
