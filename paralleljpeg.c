#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>

#define ROUND( a )      ( ( (a) < 0 ) ? (int) ( (a) - 0.5 ) : \
                                                  (int) ( (a) + 0.5 ) )

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "paralleljpeg.h"

typedef unsigned char uchar;

double C[8][8];
double Ct[8][8];
lockableNode *firstBuffer;
int Yline,Ycolumn,Cbline,Cbcolumn,Crline,Crcolumn,b;
int *Ychannel;
int *Cbchannel;
int *Crchannel;
uchar *data;

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

void error(int err, char *msg) {
	fprintf(stderr,"%s a retourné %d, message d'erreur : %s\n",msg,err,strerror(errno));
	exit(EXIT_FAILURE);
}


lockableNode* initFineGrainedList(int size){
	int i;
	lockableNode* node = (lockableNode*) malloc(sizeof(lockableNode));
	node->offset = 0;
	pthread_mutex_init(&(node->mutex),NULL);
	lockableNode* previous = node;
	for(i = 1; i < size; i++){
		lockableNode* nextNode = (lockableNode*) malloc(sizeof(lockableNode));
		nextNode->offset = i;
		nextNode->validity = 1;
		pthread_mutex_init(&(nextNode->mutex),NULL);
		previous->next = nextNode;
		previous = nextNode;
	}
	previous->next = NULL;
	return node;
}

lockableNode* getFirstUnlockedElem(lockableNode* startList){
	lockableNode* temp = startList;
	while(temp != NULL){
		if(temp->validity){
			if( pthread_mutex_trylock(&(startList->mutex)) == 0){
				temp->validity = 0;
				int err=pthread_mutex_unlock(&(startList->mutex));
				if(err!=0)
					error(err,"pthread_mutex_unlock");
				return temp;
			}
		}
		temp = temp->next;
	}
	return NULL;
}

void fromRGBtoYCbCr(uchar *pixel, int *Ychannel, int *Cbchannel, int *Crchannel){
	*Ychannel = (int) (0 + (0.299*(*pixel))+(0.587*(*(pixel+1)))+(0.114*(*(pixel+2)))); //Y
	*Cbchannel = (int) (128 - (0.168736*(*pixel))-(0.331264*(*(pixel+1)))+(0.5*(*(pixel+2)))); //Cb
	*Crchannel = (int) (128 + (0.5*(*pixel))-(0.418688*(*(pixel+1)))-(0.081312*(*(pixel+2)))); //Cr
}
void fromRGBtoY(uchar *pixel, int *Ychannel){
	*Ychannel = (int) (0 + (0.299*(*pixel))+(0.587*(*(pixel+1)))+(0.114*(*(pixel+2)))); //Y
}

void* subSampling(void* param){
	lockableNode* blockindexNode = getFirstUnlockedElem(firstBuffer);
	while(blockindexNode != NULL){
		int index = blockindexNode->offset;
		int i,j,pixelX,pixelY,cbX,cbY,Yx,Yy;
		for(i = 0; i < 8; i++){
			pixelX = (i+8*(index/(Yline/8)))*Ycolumn*3;
			for(j = 0; j < 8; j++){
				pixelY = 3*j+(8*3*(index % (Ycolumn/8)));
				if(b == 4){
					Yx = (i+8*(index/(Yline/8)))*Ycolumn;
					Yy = j+(8*(index % (Ycolumn/8)));
					fromRGBtoYCbCr(data+pixelX+pixelY,Ychannel+Yx+Yy,Cbchannel+Yx+Yy,Crchannel+Yx+Yy);
					//printf("%d,",*(data+pixelX+pixelY));
				}else if(b == 2){
					Yx = (i+8*(index/(Yline/8)))*Ycolumn;
					Yy = j+(8*(index % (Ycolumn/8)));
					if(j % 2 != 0){
						fromRGBtoY(data+pixelX+pixelY,Ychannel+Yx+Yy);
					}else{
						cbX = (i + 8*(index/(Cbline/8)))*Cbcolumn;
						cbY = (j/2) + (4*(index % (Cbcolumn/4)));
						fromRGBtoYCbCr(data+pixelX+pixelY,Ychannel+Yx+Yy,Cbchannel+cbX+cbY,Crchannel+cbX+cbY);
					}
				}else{
					Yx = (i+8*(index/(Yline/8)))*Ycolumn;
					Yy = j+(8*(index % (Ycolumn/8)));
					if(i % 2 != 0 || j % 2 != 0){
						fromRGBtoY(data+pixelX+pixelY,Ychannel+Yx+Yy);
					}else{
						cbX = ((i/2) + 4*(index/(Cbline/4)))*Cbcolumn;
						cbY = (j/2)+(4*(index % (Cbcolumn/4)));
						fromRGBtoYCbCr(data+pixelX+pixelY,Ychannel+Yx+Yy,Cbchannel+cbX+cbY,Crchannel+cbX+cbY);
					}
				}
			}
		}
		blockindexNode = getFirstUnlockedElem(firstBuffer);
	}
	return NULL;
}

void downsample(int numThreads){
	pthread_t threads[numThreads];
	int i;
	int err;
	for(i = 0; i < numThreads; i++){
		err = pthread_create(&(threads[i]),NULL,&subSampling,NULL);
		if(err!=0)
			error(err,"pthread_create");
	}
	for(i=numThreads-1;i>=0;i--) {
		err=pthread_join(threads[i],NULL);
		if(err!=0)
			error(err,"pthread_join");
		
	}
}
void main(int argc, char *argv[]){
	int x,y,n;
	int option;
	while((option = getopt(argc,argv,"b:")) != -1){
		switch(option){
			case 'b' : b = atoi(optarg);
				break;
			default: exit(0);
		}


	}
	data = stbi_load(argv[optind], &x, &y, &n,0);
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
	
	Ychannel = (int*) malloc(Yline*Ycolumn*sizeof(int));
	Cbchannel = (int*) malloc(Cbline*Cbcolumn*sizeof(int));
	Crchannel = (int*) malloc(Crline*Crcolumn*sizeof(int));
	//computeCmatrix(C,Ct,8);
	long int numThreads = sysconf(_SC_NPROCESSORS_ONLN);
	int numBlocks = (Yline/8)*(Ycolumn/8);
	firstBuffer = initFineGrainedList(numBlocks);
	downsample(numThreads);
	int i;
	lockableNode* tmp = firstBuffer->next;
	for(i = 0; i < numBlocks; i++){
		free(firstBuffer);
		firstBuffer = tmp;
		if(tmp != NULL){
			tmp = tmp->next;
		}
	}
	int j;
	for(i = 0; i < Yline; i++){
		for(j = 0; j < Ycolumn; j++){
			printf("%d ",Ychannel[i*Ycolumn +j]);
		}
		printf("\n");
	}
	printf("\n");
	printf("Cb channel\n");
	printf("\n");
	for(i = 0; i < Cbline; i++){
		for(j = 0; j < Cbcolumn; j++){
			printf("%d ",Cbchannel[i*Cbcolumn +j]);
		}
		printf("\n");
	}
	printf("\n");
	printf("Cr channel\n");
	printf("\n");
	for(i = 0; i < Crline; i++){
		for(j = 0; j < Crcolumn; j++){
			printf("%d ",Crchannel[i*Crcolumn +j]);
		}
		printf("\n");
	}
	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);
	stbi_image_free(data);
}

