#include "routing.h"


typedef struct pending_msg_struct {
	DataEntry *msg_ptr;
	ID msg_id;

	int attempts;
	ID target;

} PendingMsg;


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

/*

Functions needed

UPDATE
	- takes a message and updates the route tables

QUERY
	- asks which node we should send to if we are targeting a node

UNLINK
	- mostly for testing but removes direct access from this node to another node

LINK
	- mostly for testing but allows access from this node to another node

*/