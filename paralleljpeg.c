#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>

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

int numConsumedDCTY;
pthread_mutex_t mutexDCTY;
lockableNode *buffDCTYchan = NULL;
sem_t semDCTYchan;

int numConsumedDCTCb;
pthread_mutex_t mutexDCTCb;
lockableNode *buffDCTCbchan = NULL;
sem_t semDCTCbchan;

int numConsumedDCTCr;
pthread_mutex_t mutexDCTCr;
lockableNode *buffDCTCrchan = NULL;
sem_t semDCTCrchan;

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

void addElementAtFront(lockableNode* element, lockableNode** list){
	element->next = *list;
	*list = element;
}

lockableNode* createNewNode(int index){
	lockableNode *newNode = (lockableNode*) malloc(sizeof(lockableNode));
	newNode->offset = index;
	newNode->validity = 1;
	newNode->next = NULL;
	pthread_mutex_init(&(newNode->mutex),NULL);
	return newNode;
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
		addElementAtFront(createNewNode(index),&buffDCTYchan);
		sem_post(&semDCTYchan);
		addElementAtFront(createNewNode(index),&buffDCTCbchan);
		sem_post(&semDCTCbchan);
		addElementAtFront(createNewNode(index),&buffDCTCrchan);
		sem_post(&semDCTCrchan);
		blockindexNode = getFirstUnlockedElem(firstBuffer);
	}
	return NULL;
}

void downsample(int numThreads){
	pthread_t threads[numThreads];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	int i;
	int err;
	for(i = 0; i < numThreads; i++){
		err = pthread_create(&(threads[i]),&attr,&subSampling,NULL);
		if(err!=0)
			error(err,"pthread_create");
	}
}

void computeDCT(int *channel, int y, int x,int offset){
	double temp[8][8];
	double temp2;
	int i;
	int j;
	int k;
	int lineCoord;
	int colCoord;
	for(i = 0; i < 8; i++){
		lineCoord = (i+ 8*(offset/(y/8)))*x;
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
			lineCoord = (i+ 8*(offset/(y/8)))*x;
			colCoord = j+ (8*(offset % (x/8)));
			*(channel+lineCoord+colCoord) = ROUND(temp2);
		}
	}
}


void* dispatchForProcessing(void *arg){
	char *param = (char *)arg;
	int size;
	if(*param == 'y'){
		pthread_mutex_lock(&mutexDCTY);
		size = numConsumedDCTY;
		while(size < (Yline/8)*(Ycolumn/8)){
			numConsumedDCTY += 1;
			printf("%d-Y \n",numConsumedDCTY);
			pthread_mutex_unlock(&mutexDCTY);
			sem_wait(&semDCTYchan);
			lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTYchan);
			computeDCT(Ychannel,Yline,Ycolumn,blockindexNode->offset);
			pthread_mutex_lock(&mutexDCTY);
			size = numConsumedDCTY;
		}
		pthread_mutex_unlock(&mutexDCTY);
		printf("thread ends\n");
	}else if(*param == 'b'){
		pthread_mutex_lock(&mutexDCTCb);
		size = numConsumedDCTCb;
		while(size < (Yline/8)*(Ycolumn/8)){
			numConsumedDCTCb += 1;
			printf("%d-Cb \n",numConsumedDCTCb);
			pthread_mutex_unlock(&mutexDCTCb);
			sem_wait(&semDCTCbchan);
			lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTCbchan);
			computeDCT(Cbchannel,Cbline,Cbcolumn,blockindexNode->offset);
			pthread_mutex_lock(&mutexDCTCb);
			size = numConsumedDCTCb;
		}
		pthread_mutex_unlock(&mutexDCTCb);
		printf("thread ends\n");
	}else{
		pthread_mutex_lock(&mutexDCTCr);
		size = numConsumedDCTCr;
		while(size < (Yline/8)*(Ycolumn/8)){
			numConsumedDCTCr += 1;
			printf("%d-Cr \n",numConsumedDCTCr);
			pthread_mutex_unlock(&mutexDCTCr);
			sem_wait(&semDCTCrchan);
			lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTCrchan);
			computeDCT(Crchannel,Crline,Crcolumn,blockindexNode->offset);
			pthread_mutex_lock(&mutexDCTCr);
			size = numConsumedDCTCr;
		}
		pthread_mutex_unlock(&mutexDCTCr);
		printf("thread ends\n");
	}
	return NULL;	
}


void DCTparallelStep(int numThreads){
	pthread_t YchanThreads[numThreads];
	int i;
	int err;
	for(i = 0; i < numThreads; i++){
		char arg = 'y';
		err = pthread_create(&(YchanThreads[i]),NULL,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CbchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'r';
		err = pthread_create(&(CbchanThreads[i]),NULL,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CrchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'b';
		err = pthread_create(&(CrchanThreads[i]),NULL,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	for(i=0;i<numThreads;i++) {
		err=pthread_join(YchanThreads[i],NULL);
		if(err!=0)
			error(err,"pthread_join");
		
	}
	for(i=0;i<numThreads;i++) {
		err=pthread_join(CbchanThreads[i],NULL);
		if(err!=0)
			error(err,"pthread_join");
		
	}
	for(i=0;i<numThreads;i++) {
		err=pthread_join(CrchanThreads[i],NULL);
		if(err!=0)
			error(err,"pthread_join");
		
	}
}

void initGlobalVariables(){
	Ychannel = (int*) malloc(Yline*Ycolumn*sizeof(int));
	Cbchannel = (int*) malloc(Cbline*Cbcolumn*sizeof(int));
	Crchannel = (int*) malloc(Crline*Crcolumn*sizeof(int));
	int numBlocks = (Yline/8)*(Ycolumn/8);
	firstBuffer = initFineGrainedList(numBlocks);
	sem_init(&semDCTYchan, 0, 0);
	sem_init(&semDCTCbchan, 0, 0);
	sem_init(&semDCTCrchan, 0, 0);
	pthread_mutex_init(&mutexDCTY,NULL);
	pthread_mutex_init(&mutexDCTCb,NULL);
	pthread_mutex_init(&mutexDCTCr,NULL);
	numConsumedDCTY = 0;
	numConsumedDCTCb = 0;
	numConsumedDCTCr = 0;
}

void freeGlobalVariables(){
	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);
	sem_destroy(&semDCTYchan);
	sem_destroy(&semDCTCbchan);
	sem_destroy(&semDCTCrchan);
	pthread_mutex_destroy(&mutexDCTY);
	pthread_mutex_destroy(&mutexDCTCb);
	pthread_mutex_destroy(&mutexDCTCr);
	
	int i;
	lockableNode* tmp = firstBuffer->next;
	int numBlocks = (Yline/8)*(Ycolumn/8);
	for(i = 0; i < numBlocks; i++){
		free(firstBuffer);
		firstBuffer = tmp;
		if(tmp != NULL){
			tmp = tmp->next;
		}
	}
	int j;
	tmp = buffDCTYchan->next;
	while(buffDCTYchan != NULL){
		//printf("%d,",buffDCTYchan->offset);
		free(buffDCTYchan);
		buffDCTYchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffDCTCbchan->next;
	while(buffDCTCbchan != NULL){
		//printf("%d,",buffDCTCbchan->offset);
		free(buffDCTCbchan);
		buffDCTCbchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffDCTCrchan->next;
	while(buffDCTCrchan != NULL){
		//printf("%d,",buffDCTCrchan->offset);
		free(buffDCTCrchan);
		buffDCTCrchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");

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
	
	long int numThreads = sysconf(_SC_NPROCESSORS_ONLN);
	initGlobalVariables();
	downsample(numThreads);
	computeCmatrix(C,Ct,8);
	DCTparallelStep(numThreads);
	int i;
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
	freeGlobalVariables();
	stbi_image_free(data);
}

