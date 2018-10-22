# PsNee

PsNee, an open source stealth modchip for the Sony Playstation 1 

                        PPPPPPPPPPPPPPPP                  P            P      
                       P              P                  PP           P        
                      P              P                  P P          P        
                     P              P                  P  P         P          
                    P              P                  P   P        P          
                   P              P                  P    P       P            
                  P              P                  P     P      P            
                 PPPPPPPPPPPPPPPP  PPPPPPPPPPP     P      P     P  PPPPPPPPPPP  PPPPPPPPPPP
                P                 P               P       P    P  P            P
               P                 P               P        P   P  P            P  
              P                 P               P         P  P  P            P  
             P                 P               P          P P  P            P    
            P                 PPPPPPPPPPPPPP  P           PP  PPPPPPP      PPPPPPP    
           P                              P  P            P  P            P      
          P                              P  P            P  P            P      
         P                              P  P            P  P            P        
        P                              P  P            P  P            P        
       P                              P  P            P  P            P      
      P                              P  P            P  P            P        
     P                   PPPPPPPPPPPP  P            P  PPPPPPPPPPP  PPPPPPPPPPP

---------------------------------------
This version is forked from
https://github.com/kalymos/PsNee
Is was modified by me.

-------------------------------------------------

This version is specifically intended to run on an AtTiny25. No debugging, just as a stealth mod chip.
Also, the software was rewritten to not compile in the Arduino IDE, but in Atmel Studio instead.

At this time it is untested, but i will test it soon.

-----------------------------------------------------
Pin assignments

PlayStation    Name        Attiny
3.5v           = supply    VCC
IC732.Pin-5    = WFCK      PB4     
IC732.Pin-42   = CEO       PB2
IC304.Pin-24   = SUBQ      PB1
IC304.Pin-26   = SQCK      PB0
GND            = gnd       GND
