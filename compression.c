#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

#define ROUND( a )      ( ( (a) < 0 ) ? (int) ( (a) - 0.5 ) : \
                                                  (int) ( (a) + 0.5 ) )

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
double C[8][8];
double Ct[8][8];

void computeCmatrix(double C[8][8],double Ct[8][8],int N){
	double pi = atan( 1.0 ) * 4.0;
	int i;
	int j;
	int k;
	for(i = 0; i < N; i++){
		C[0][i] = sqrt(1.0/(double)N);
		Ct[i][0] = C[0][i];
	}
	for(j = 1; j < N; j++){
		for(k = 0; k < N; k++){
			C[j][k] = sqrt(2.0/(double)N)*cos((j*(2*k+1)*pi)/2.0*N);
			Ct[k][j] = Ct[j][k];
		}
	}

}
//computes one 8x8 dct block
void computeDCT(int *channel, int x, int y, double C[8][8],double Ct[8][8], int offset){
	double temp[8][8];
	double temp2;
	int i;
	int j;
	int k;
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			temp[i][j] = 0.0;
			for(k = 0; k < 8; k++){
				temp[i][j] += C[i][k]*((*(channel+(k*x+j+8*offset)))-128);
			}
		}
	}
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			temp2 = 0.0;
			for(k = 0; k < 8; k++){
				temp2+=temp[i][k]*Ct[k][j];
			}
			*(channel+i*x+j+8*offset) = ROUND(temp2);
		}
	}	
}

void computeAllDCT(int *channel, int width, int height){
	int numBlocks = (width/8) * (height/8);
	int i;
	for(i = 0; i < numBlocks; i++){
		computeDCT(channel,width,height,C,Ct,i);
	}	
}
void fromRGBtoYCbCr(uchar **pixel, int **Ychannel, int **Cbchannel, int **Crchannel){
	**Ychannel = (int) (0 + (0.299*(**pixel))+(0.587*(*((*pixel)+1)))+(0.114*(*((*pixel)+2)))); //Y
	(*Ychannel)++;
	**Cbchannel = (int) (128 - (0.168736*(**pixel))-(0.331264*(*((*pixel)+1)))+(0.5*(*((*pixel)+2)))); //Cb
	(*Cbchannel)++;
	**Crchannel = (int) (128 + (0.5*(**pixel))-(0.418688*(*((*pixel)+1)))-(0.081312*(*((*pixel)+2)))); //Cr
	(*pixel)+=3;
	(*Crchannel)++;
}
void fromRGBtoY(uchar **pixel, int **Ychannel){
	**Ychannel = (int) (0 + (0.299*(**pixel))+(0.587*(*((*pixel)+1)))+(0.114*(*((*pixel)+2)))); //Y
	(*Ychannel)++;
	(*pixel)+=3;
}
//int b = 4 || b = 2 || b = 0
void downsample(uchar *pixel, int b, int x, int y, int *Ychannel, int *Cbchannel, int *Crchannel){
	int i;
	int k;
	for(i = 0; i < y; i++){
		for(k = 0; k < x; k++){
			if(((i+1) % 2 == 0) && b == 0){
				fromRGBtoY(&pixel,&Ychannel);
			}
			else if((b == 2 || b == 0) && ((k+1) % 2) == 0){
				fromRGBtoY(&pixel,&Ychannel);
			}
			else{
				fromRGBtoYCbCr(&pixel,&Ychannel,&Cbchannel,&Crchannel);
			}
		}
	}

}

//should be called with an image argument
//should be called with 3 options with argument
// option is -b with values corresponding to downsampling ratios
// ie 4 for 4:4:4, 2 for4:2:2 and 0 for 4:0:0
// a valid call is ./test -b 2 4pixels.bmp
void main(int argc, char *argv[]){
	int x,y,n;
	int b;
	int option;
	while((option = getopt(argc,argv,"b:")) != -1){
		switch(option){
			case 'b' : b = atoi(optarg);
				break;
			default: exit(0);
		}


	}
	uchar *data = stbi_load(argv[optind], &x, &y, &n,0);
	int Yline,Ycolumn,Cbline,Cbcolumn,Crline,Crcolumn;
	if(b == 4){
		Yline = y;
		Ycolumn = x;
		Cbline = y;
		Cbcolumn = x;
		Crline = y;
		Crcolumn = x; 
	}else if(b == 2){
		Yline = y;
		Ycolumn = x;
		Cbline = y;
		Cbcolumn = x/2; 
		Crline = y;
		Crcolumn = x/2;
	}else{ //b = 0
		Yline = y;
		Ycolumn = x;
		Cbline = y/2;
		Cbcolumn = x/2;
		Crline = y/2;
		Crcolumn = x/2;
	}
	int channelY[Yline*Ycolumn];
	int channelCb[Cbline*Cbcolumn];
	int channelCr[Crline*Crcolumn];
	downsample(data,b,x,y,channelY,channelCb,channelCr);
	/**printf("Y channel\n");
	printf("\n");
	int i;
	int j;
	for(i = 0; i < Yline; i++){
		for(j = 0; j < Ycolumn; j++){
			printf("%d ",channelY[i*Ycolumn +j]);
		}
		printf("\n");
	}
	printf("\n");
	printf("Cb channel\n");
	printf("\n");
	for(i = 0; i < Cbline; i++){
		for(j = 0; j < Cbcolumn; j++){
			printf("%d ",channelCb[i*Cbcolumn +j]);
		}
		printf("\n");
	}
	printf("\n");
	printf("Cr channel\n");
	printf("\n");
	for(i = 0; i < Crline; i++){
		for(j = 0; j < Crcolumn; j++){
			printf("%d ",channelCr[i*Crcolumn +j]);
		}
		printf("\n");
	}**/
	computeAllDCT(channelY,Ycolumn,Yline);
	printf("Y channel\n");
	printf("\n");
	int i;
	int j;
	for(i = 0; i < Yline; i++){
		for(j = 0; j < Ycolumn; j++){
			printf("%d ",channelY[i*Ycolumn +j]);
		}
		printf("\n");
	}
	printf("\n");
	stbi_image_free(data);
}

