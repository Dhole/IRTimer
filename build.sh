#!/bin/sh
set -ex

# BOARD="esp8266:esp8266:nodemcuv2"
BOARD="esp8266:esp8266:generic"
PORT="/dev/ttyUSB0"

ARDUINOCLI="$HOME/git/arduino-cli/arduino-cli"

${ARDUINOCLI} compile -v --fqbn "${BOARD}"

if [ "$1" = "upload" ];
then
    ${ARDUINOCLI} upload -v --port "${PORT}" --fqbn "${BOARD}"
fi

