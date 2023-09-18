/*******************************************************************************
* CPU Clock Measurement Using RDTSC
*
* Description:
*     This C file provides functions to compute and measure the CPU clock using
*     the `rdtsc` instruction. The `rdtsc` instruction returns the Time Stamp
*     Counter, which can be used to measure CPU clock cycles.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Notes:
*     Ensure that the platform supports the `rdtsc` instruction before using
*     these functions. Depending on the CPU architecture and power-saving
*     modes, the results might vary. Always refer to the CPU's official
*     documentation for accurate interpretations.
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "timelib.h"

int main (int argc, char **argv)
{
	if (argc <3) {
        return EXIT_FAILURE;
    }

	uint64_t clocks_elapsed;
	long sec = atol(argv[1]);
    long nsec = atol(argv[2]);
    char method = argv[3][0];
	

	if (method == 's'){
		clocks_elapsed = get_elapsed_sleep(sec, nsec);
		printf("WaitMethod: SLEEP\n");
	}else if (method == 'b'){
		clocks_elapsed = get_elapsed_busywait(sec, nsec);
		printf("WaitMethod: BUSYWAIT\n");
	}else{
		perror("Invalid Method");
		return EXIT_FAILURE;
	}

	printf("WaitTime: %ld %ld\n", sec, nsec);
    printf("ClocksElapsed: %lu\n", clocks_elapsed);

	//delayed time in nanoseconds
	long delayed = sec * NANO_IN_SEC + nsec;
	delayed /= 1000; 

	double clock_speed = (double)clocks_elapsed/ (delayed);

    printf("ClockSpeed: %.2f \n", clock_speed);
	return EXIT_SUCCESS;
}

