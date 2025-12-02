#include "routing.h"


/*

per node data structure

data structure that keeps info on best internediate neighbor to use to reach destination node 

look at a given message M arriving at node N (O, S, D, s) (origin, source, destination, steps) (Ignore broadcast)

we know from N, node S and O can be reached.
	- O can be reached in s steps
	- S can be reached in 1 step

looking at the end-to-end acknolegement message MA at node N
	- we know the message has successfully reached D from O
	- M_O can be reached in M_s steps
	- M_S can be reached in 1 step
	- MA_O can be reached in MA_s steps
	- MA_S can be reached in 1 step
*/