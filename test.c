#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

double *C;
int x = 5;
int y = 5;
void initialize(double *tab[width]){
	tab[0][0] = 0.0;
	tab[1][0] = 1.0;

}
void main(int argc, char** argv){
	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	double tab[width][height];
	initialize(&tab[0]);
	printf("%f,%f\n",tab[0][0],tab[1][0]);
	exit(0);
}
