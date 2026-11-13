#!/bin/bash
cd /tmp/digiemu
T="qtap:DOWN:0.08 wait:0.3"
SET="wait:2 qtap:GLOBAL:0.08 wait:0.5 $T $T $T $T qtap:YES:0.08 wait:0.5 $T qtap:YES:0.08 wait:0.6 $T $T $T $T qtap:RIGHT:0.08 wait:0.5 qtap:NO:0.08 wait:0.3 qtap:NO:0.08 wait:0.3 qtap:NO:0.08 wait:0.5"
TRK="qtap:MIDI:0.1 wait:0.6 qtap:SYN2:0.1 wait:0.6 chord:FUNC+EA qtap:AMP:0.1 wait:0.5 chord:FUNC+EA chord:FUNC+EB chord:FUNC+EC qtap:FLTR:0.1 wait:0.5 chord:FUNC+EA chord:FUNC+EB chord:FUNC+EC wait:0.5 ${EXTRA}"
PLAY="${PRE} mem:419c5df4 mark:PLAY1 qtap:PLAY:0.1 wait:2 mark:STOP1 qtap:STOP:0.1 wait:1 mark:PLAY2 qtap:PLAY:0.1 wait:2 qtap:STOP:0.1 wait:1 mark:END"
RT=1 PYTHONPATH=/tmp/fake_tk timeout 600 .venv/bin/python /tmp/work/drive.py $SET $TRK $PLAY 2>&1 | grep -v "^\[" | grep -E "^MEM|^TS|^[0-9]+ |MIDI LOG|DRIVER|^total|^LEDS" | cut -c1-200 | grep -v "^LEDS" | head -${1:-80}
