#!/bin/sh
./t6 -create disk 24600
./t6 -add disk a.txt c.txt
./t6 -add disk b.txt ".Never gonna give you up. Never gonna let you down."
./t6 -add disk c.txt c.txt
./t6 -copy disk c.txt cback.txt
./t6 -map disk
