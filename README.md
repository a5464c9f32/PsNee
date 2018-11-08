# PsNee

PsNee, an open source stealth modchip for the Sony Playstation 1 

---------------------------------------
This version is forked from
https://github.com/kalymos/PsNee

It was modified by me.

-------------------------------------------------

This version is specifically intended to run on an AtTiny25. No debugging, just as a stealth mod chip.
Also, the software was rewritten to not compile in the Arduino IDE, but in Atmel Studio instead.

Also using the 4.166MHz clock from IC304, to have a stable clock.
Optimized code to run at the reduced clock speed.

At this time it is tested only on an AtTiny85.

-----------------------------------------------------
For pin assignments, check the code and also https://github.com/danielheinrich/PsNee/blob/master/PU-22-hookup.jpg
