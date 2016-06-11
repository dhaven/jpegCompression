#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>


void main(int argc, char *argv[]){
	int *line = (int*) malloc(sizeof(int)*10);
	printf("size : %lu\n",sizeof(line));
	free(line);
	printf("%lu\n",sizeof(unsigned char));
}
