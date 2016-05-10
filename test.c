#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/**
* test on 4 pixels image :
* top-left : color red, RGB is (255,0,0),
* top-right : color green, RGB is (0,255,0),
* down-left : color blue, RGB is (0,0,255),
* down-right : color yellow, RGB is (255,255,0),
*/
typedef unsigned char uchar;

void fromRGBtoYCbCr(uchar **pixel, uchar r, uchar g, uchar b, uchar *prevCb, uchar *prevCr){
	**pixel = 0 + (0.299*r)+(0.587*g)+(0.114*b); //Y
	(*pixel)++;
	**pixel = 128 - (0.168736*r)-(0.331264*g)+(0.5*b); //Cb
	*prevCb = **pixel;
	(*pixel)++;
	**pixel = 128 + (0.5*r)-(0.418688*g)-(0.081312*b); //Cr
	*prevCr = **pixel;
	(*pixel)++;

}
void fromRGBtoY(uchar **pixel, uchar r, uchar g, uchar b, uchar prevCb, uchar prevCr){
	**pixel = 0 + (0.299*r)+(0.587*g)+(0.114*b); //Y
	(*pixel)++;
	**pixel = prevCb; //Cb
	(*pixel)++;
	**pixel = prevCr; //Cr
	(*pixel)++;
}

void downsample(uchar *pixel, int j, int a, int b, int x, int y){
	int i;
	int k;
	uchar prevCb;
	uchar prevCr;
	for(i = 0; i < y; i++){
		for(k = 0; k < x; k++){
			if(((i+1) % 2 == 0) && b == 0){
				fromRGBtoY(&pixel,*pixel,*(pixel+1),*(pixel+2),prevCb,prevCr);
			}
			else if(a == 2 && ((k+1) % 2) == 0){
				fromRGBtoY(&pixel,*pixel,*(pixel+1),*(pixel+2),prevCb,prevCr);
			}
			else{
				fromRGBtoYCbCr(&pixel,*pixel,*(pixel+1),*(pixel+2),&prevCb,&prevCr);
			}
		}
	}

}

//should be called with an image argument
//should be called with 3 options with argument
// options are -j -a -b with values corresponding to downsampling ratios
// ie 4:4:4 4:2:2 or 4:0:0
// a valid call is ./test -j 4 -a 2 -b 2 4pixels.bmp
void main(int argc, char *argv[]){
	int x,y,n;
	int j,a,b; //downsampling ratio as j:a:b
	int option;
	while((option = getopt(argc,argv,"j:a:b:")) != -1){
		switch(option){
			case 'j' : j = atoi(optarg);
				break;
			case 'a' : a = atoi(optarg);
				break;
			case 'b' : b = atoi(optarg);
				break;
			default: exit(0);
		}


	}
	uchar *data = stbi_load(argv[optind], &x, &y, &n,0);

	printf("%d : x value\n",x);
	printf("%d : y value\n",y);
	printf("%d : number of components\n",n);
	printf("%ld : size of char\n",sizeof(char)); //1 byte
	printf("%ld : size of int\n",sizeof(int)); //4 bytes

	printf("%d : firstpixel R\n", *data);
	printf("%d : firstpixel G\n", *(data+1));
	printf("%d : firstpixel B\n", *(data+2));

	downsample(data,j,a,b,x,y);
	printf("%d : firstpixel Y\n", *data);
	printf("%d : firstpixel Cb\n", *(data+1));
	printf("%d : firstpixel Cr\n", *(data+2));
	printf("%d : secondpixel Y\n", *(data+3));
	printf("%d : secondpixel Cb\n", *(data+4));
	printf("%d : secondpixel Cr\n", *(data+5));
	printf("%d : thirdpixel Y\n", *(data+6));
	printf("%d : thirdpixel Cb\n", *(data+7));
	printf("%d : thirdpixel Cr\n", *(data+8));
	printf("%d : fourthpixel Y\n", *(data+9));
	printf("%d : fourthpixel Cb\n", *(data+10));
	printf("%d : fourthpixel Cr\n", *(data+11));
	

	stbi_image_free(data);
}

