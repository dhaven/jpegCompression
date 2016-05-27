#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>


void main(int argc, char *argv[]){
	printf("%ld\n",sysconf(_SC_NPROCESSORS_ONLN));
}
