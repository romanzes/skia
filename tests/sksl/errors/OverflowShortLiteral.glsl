### Compilation failed:

error: 1: value is out of range for type 'short': -32769
short4 s4_neg = short4(-32766, -32767, -32769, -32768); // error -32769
                                       ^^^^^^
error: 2: value is out of range for type 'short': 32768
short4 s4_pos = short4(32765, 32768, 32766, 32767);     // error 32768
                              ^^^^^
error: 3: value is out of range for type 'short': 32768
short cast_short = short(32768.);                       // error
                         ^^^^^^
3 errors
