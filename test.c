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
void initialize(double *tab){
	tab[0] = 0.0;
	tab[1] = 1.0;

}
void main(int argc, char** argv){
	C = malloc(sizeof(double)*x*y);
	*C = 2.0;
	double D = 2.0**C;
	printf("%f\n",D);
	double pi = atan( 1.0 ) * 4.0;
	printf("%f",pi);
	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	double tab[width*height];
	initialize(tab);
	printf("%f,%f\n",tab[0],tab[1]);
	exit(0);
}
