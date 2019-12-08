#include "cs421net.h"

/* YOUR CODE HERE */

int main(void) {
	/* Here is an example of sending some data to CS421Net */
	cs421net_init();
	cs421net_send("4442", 4);

	/* YOUR CODE HERE */
	return 0;
}
