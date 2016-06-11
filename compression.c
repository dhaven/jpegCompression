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

typedef unsigned char uchar;
double C[8][8];
double Ct[8][8];
int lumQuantTable[64] = {16,11,10,16,24,40,51,61,
			12,12,14,19,26,58,60,55,
			14,13,16,24,40,57,69,56,
			14,17,22,29,51,87,80,62,
			18,22,37,56,68,109,103,77,
			24,35,55,64,81,104,113,92,
			49,64,78,87,103,121,120,101,
			72,92,95,98,112,100,103,99};

int chromQuantTable[64] = {17,18,24,47,99,99,99,99,
			18,21,26,66,99,99,99,99,
			24,26,56,99,99,99,99,99,
			47,66,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99};

//quantize an 8x8 block
//wrong pretty sure
void quantization(int *channel,int width, int offset, int *quantTable){
	int i;
	int j;
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			channel[i*width+j] = ROUND(channel[(i*width+j)+(8*offset)]/quantTable[(i*width+j)+(8*offset)]);
		}
	}
}

void quantizeAll(int *channel, int width, int height, int *quantTable){
	int numBlocks = (width/8) * (height/8);
	int i;
	for(i = 0; i < numBlocks; i++){
		quantization(channel,width,i,quantTable);
	}	
}

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
			C[j][k] = sqrt(2.0/(double)N)*cos((j*(2*k+1)*pi)/(2.0*N));
			Ct[k][j] = C[j][k];
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
	int lineCoord;
	int colCoord;
	for(i = 0; i < 8; i++){
		lineCoord = (i+ 8*(offset/(x/8)))*x;
		for(j = 0; j < 8; j++){
			temp[i][j] = 0.0;
			for(k = 0; k < 8; k++){
				colCoord = k+ (8*(offset % (x/8)));
				temp[i][j] += (*(channel+ lineCoord + colCoord)-128) * Ct[k][j]; 
			}
		}
	}
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			temp2 = 0.0;
			for(k = 0; k < 8; k++){
				temp2+=C[i][k]*temp[k][j];
			}
			lineCoord = (i+ 8*(offset/(x/8)))*x;
			colCoord = j+ (8*(offset % (x/8)));
			*(channel+lineCoord+colCoord) = ROUND(temp2);
		}
	}	
}

void computeAllDCT(int *channel, int width, int height){
	int numBlocks = (width/8) * (height/8);
	printf("numBlocks : %d,width : %d,height : %d\n",numBlocks,width,height);
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

void zigzagging(int *channel, int size){
    
    int i, j, m, n;
    m=size;
    int array_zig[m*m];

        for (i = n = 0; i < m * 2; i++)
    {
        for (j = (i < m) ? 0 : i-m+1; j <= i && j < m; j++)
        {
            array_zig[n]=channel[(i&1)? j*(m-1)+i : (i-j)*m+j ] ;
            n++;
        }
    }
    channel = array_zig;
    
    for (i=0;i<m*m;i++){
        printf("%d ", channel[i]);
    }
    
}
/**int zigzagging(int *channel){
	int i;
	int j;
	int aux[64]={channel[0], channel[1], channel[8], channel[16], channel[9], channel[2], channel[3], channel[10], 
				channel[17], channel[24], channel[32], channel[25], channel[18], channel[11], channel[4], channel[5],
				channel[12], channel[19], channel[26], channel[33], channel[40], channel[48], channel[41], channel[34],
				channel[27], channel[20], channel[13], channel[6], channel[7], channel[14], channel[21], channel[28],
				channel[28], channel[35], channel[42], channel[49], channel[56], channel[57], channel[50], channel[43],
				channel[36], channel[29], channel[22], channel[15], channel[23], channel[37], channel[44], channel[51],
				channel[58], channel[59], channel[52], channel[45], channel[38], channel[31], channel[39], channel[46],
				channel[53], channel[60], channel[61], channel[54], channel[47], channel[55], channel[62], channel[63]};
		
	for(i=0; i<64; i++){
	printf("%d ", aux[i]);
		}
return aux;
}**/
//should be called with an image argument
//should be called with  options with argument
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
	/**int channelY[256] = {52,55,61,66,70,61,64,73,52,55,61,66,70,61,64,73,
					63,59,55,90,109,85,69,72,63,59,55,90,109,85,69,72,
					62,59,68,113,144,104,66,73,62,59,68,113,144,104,66,73,
					63,58,71,122,154,106,70,69,63,58,71,122,154,106,70,69,
					67,61,68,104,126,88,68,70,67,61,68,104,126,88,68,70,
					79,65,60,70,77,68,58,75,79,65,60,70,77,68,58,75,
					85,71,64,59,55,61,65,83,85,71,64,59,55,61,65,83,
					87,79,69,68,65,76,78,94,87,79,69,68,65,76,78,94,
					52,55,61,66,70,61,64,73,52,55,61,66,70,61,64,73,
					63,59,55,90,109,85,69,72,63,59,55,90,109,85,69,72,
					62,59,68,113,144,104,66,73,62,59,68,113,144,104,66,73,
					63,58,71,122,154,106,70,69,63,58,71,122,154,106,70,69,
					67,61,68,104,126,88,68,70,67,61,68,104,126,88,68,70,
					79,65,60,70,77,68,58,75,79,65,60,70,77,68,58,75,
					85,71,64,59,55,61,65,83,85,71,64,59,55,61,65,83,
					87,79,69,68,65,76,78,94,87,79,69,68,65,76,78,94};**/
	
	int channelY[Yline*Ycolumn];
	int channelCb[Cbline*Cbcolumn];
	int channelCr[Crline*Crcolumn];
	downsample(data,b,x,y,channelY,channelCb,channelCr);
	computeCmatrix(C,Ct,8);
	//computeAllDCT(channelY,Ycolumn,Yline);
	//computeDCT(channelY,Ycolumn,Yline,C,Ct,0);
	//computeAllDCT(channelCb,Cbcolumn,Cbline);
	//computeAllDCT(channelCr,Crcolumn,Crline);
	//quantizeAll(channelY,Ycolumn,Yline,lumQuantTable);
	//quantizeAll(channelCb,Cbcolumn,Cbline,chromQuantTable);
	//quantizeAll(channelCr,Crcolumn,Crline,chromQuantTable);
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
	}
	stbi_image_free(data);
}

