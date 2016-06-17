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
int numConsumedDCTY;
pthread_mutex_t mutexDCTY;
pthread_mutex_t mutexDwnSclY;
lockableNode *buffDCTYchan = NULL;
sem_t semDCTYchan;

int numConsumedDCTCb;
pthread_mutex_t mutexDCTCb;
pthread_mutex_t mutexDwnSclCb;
lockableNode *buffDCTCbchan = NULL;
sem_t semDCTCbchan;

int numConsumedDCTCr;
pthread_mutex_t mutexDCTCr;
pthread_mutex_t mutexDwnSclCr;
lockableNode *buffDCTCrchan = NULL;
sem_t semDCTCrchan;

int numConsumedQuantY;
pthread_mutex_t mutexQuantY;
lockableNode *buffQuantYchan = NULL;
sem_t semQuantYchan;

int numConsumedQuantCb;
pthread_mutex_t mutexQuantCb;
lockableNode *buffQuantCbchan = NULL;
sem_t semQuantCbchan;

int numConsumedQuantCr;
pthread_mutex_t mutexQuantCr;
lockableNode *buffQuantCrchan = NULL;
sem_t semQuantCrchan;

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
	node->validity = 1;
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
// Need to lock on node before read variable validity because validity is a critical section 
lockableNode* getFirstUnlockedElem(lockableNode* startList){
	lockableNode* temp = startList;
	while(temp != NULL){
		if( pthread_mutex_trylock(&(temp->mutex)) == 0){
			if(temp->validity){
				temp->validity = 0;
				int err=pthread_mutex_unlock(&(temp->mutex));
				if(err!=0)
					error(err,"pthread_mutex_unlock");
				return temp;
			}else{
				pthread_mutex_unlock(&(temp->mutex));
			}
		}
		temp = temp->next;
	}
	return NULL;
}

//necessary for 4:2:0 that 4 blocks in a square have been downscaled before applying DCT to 1 block
int areInList(lockableNode* startList,int next, int below, int diag){
	lockableNode *temp = startList;
	printf("%d-%d-%d\n",next,below,diag);
	int count = 0;
	while(temp != NULL && count < 3){
		if(temp->offset == next || temp->offset == below || temp->offset == diag){
			count++;
		}
		temp = temp->next;
	}
	//printf("%d\n",count);
	return count == 3;
}

//necessary for 4:2:0 that 2 adjacent blocks have been downscaled before applying DCT to 1 block
int isInList(lockableNode* startList,int next){
	lockableNode *temp = startList;
	//printf("%d-%d-%d\n",next,below,diag);
	int count = 0;
	while(temp != NULL && count < 1){
		if(temp->offset == next){
			count++;
		}
		temp = temp->next;
	}
	//printf("%d\n",count);
	return count == 1;
}

//returns the first 8x8 block for which DCT can be called given an donwscaling ratio of 4:2:0
//numBlocksWidth is the number of blocks on the width of the initial image
lockableNode* getFirstElemFor420(lockableNode* startList, int numBlocksWidth){
	lockableNode *temp = startList;
	while(temp != NULL){
		if(((temp->offset)%2 == 0)  && ((temp->offset)%(2*numBlocksWidth) == (temp->offset)%numBlocksWidth)){
			if( pthread_mutex_trylock(&(temp->mutex)) == 0){
				if(temp->validity && areInList(startList,(temp->offset)+1,(temp->offset)+numBlocksWidth,(temp->offset)+1+numBlocksWidth)){
					temp->validity = 0;
					int err=pthread_mutex_unlock(&(temp->mutex));
					if(err!=0)
						error(err,"pthread_mutex_unlock");
					return temp;
				}else{
					pthread_mutex_unlock(&(temp->mutex));
				}
			}	
		}
		temp = temp->next;
	}
	return NULL;
}

//returns the first 8x8 block for which DCT can be called given downscaling ratio of 4:2:2
lockableNode* getFirstElemFor422(lockableNode* startList){
	lockableNode *temp = startList;
	while(temp != NULL){
		if((temp->offset)%2 == 0){
			if( pthread_mutex_trylock(&(temp->mutex)) == 0){
				if(temp->validity && isInList(startList,(temp->offset)+1)){
					temp->validity = 0;
					int err=pthread_mutex_unlock(&(temp->mutex));
					if(err!=0)
						error(err,"pthread_mutex_unlock");
					return temp;
				}else{
					pthread_mutex_unlock(&(temp->mutex));
				}
			}	
		}
		temp = temp->next;
	}
	return NULL;
}


int addElementAtFront(lockableNode* element, lockableNode** list, pthread_mutex_t *mutex){
	if(pthread_mutex_trylock(mutex) == 0){
		element->next = *list;
		*list = element;
		pthread_mutex_unlock(mutex);
		return 1;
	}
	return 0;
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
		int res;
		//should lock on the list before adding element
		lockableNode *newNode = createNewNode(index);
		res = addElementAtFront(newNode,&buffDCTYchan,&mutexDwnSclY);
		while(res != 1){
			res = addElementAtFront(newNode,&buffDCTYchan,&mutexDwnSclY);
		}
		sem_post(&semDCTYchan);
		newNode = createNewNode(index);
		res = addElementAtFront(newNode,&buffDCTCbchan, &mutexDwnSclCb);
		while(res != 1){
			res = addElementAtFront(newNode,&buffDCTCbchan,&mutexDwnSclCb);
		}
		sem_post(&semDCTCbchan);
		newNode = createNewNode(index);
		res = addElementAtFront(newNode,&buffDCTCrchan, &mutexDwnSclCr);
		while(res != 1){
			res = addElementAtFront(newNode,&buffDCTCrchan,&mutexDwnSclCr);
		}
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

//returns the index of the block on which DCT will be applied
int computeDCTForChannelY(int *size){
	numConsumedDCTY += 1;
	int index;
	//printf("%d-Y \n",numConsumedDCTY);
	pthread_mutex_unlock(&mutexDCTY);
	sem_wait(&semDCTYchan);
	lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTYchan);
	index = blockindexNode->offset;
	computeDCT(Ychannel,Yline,Ycolumn,blockindexNode->offset);
	pthread_mutex_lock(&mutexDCTY);
	*size = numConsumedDCTY;
	return index;
}

//returns the index of the block on which DCT will be applied
int computeDCTForChannelCb(int *size){
	numConsumedDCTCb += 1;
	pthread_mutex_unlock(&mutexDCTCb);
	sem_wait(&semDCTCbchan);
	int index = 0;
	if(b == 4){
		lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTCbchan);
		index = blockindexNode->offset;
		computeDCT(Cbchannel,Cbline,Cbcolumn,blockindexNode->offset);
		pthread_mutex_lock(&mutexDCTCb);
		*size = numConsumedDCTCb;
	}else if(b == 2){
		lockableNode* blockindexNode = getFirstElemFor422(buffDCTCbchan);
		while(blockindexNode == NULL){
			sem_wait(&semDCTCbchan);
			lockableNode* blockindexNode = getFirstElemFor422(buffDCTCbchan);
		}
		index = (blockindexNode->offset)/2;
		computeDCT(Cbchannel,Cbline,Cbcolumn,(blockindexNode->offset)/2);
		pthread_mutex_lock(&mutexDCTCb);
		*size = numConsumedDCTCb;
	}else{
		lockableNode* blockindexNode = getFirstElemFor420(buffDCTCbchan,Ycolumn/8);
		while(blockindexNode == NULL){
			sem_wait(&semDCTCbchan);
			lockableNode* blockindexNode = getFirstElemFor420(buffDCTCbchan,Ycolumn/8);
		}
		//need to compute the corresponding offset in the Cb channel
		int oldL = (blockindexNode->offset)/(Ycolumn/8);
		int oldO = (blockindexNode->offset)%(Ycolumn/8);
		int newL = oldL/2;
		int newO = oldO/2;
		int newOffset = newL*((Ycolumn/8)/2) + newO;
		index = newOffset;
		computeDCT(Cbchannel,Cbline,Cbcolumn,newOffset);
		pthread_mutex_lock(&mutexDCTCb);
		*size = numConsumedDCTCb;
	}
	return index;
}

//returns the index of the block on which the DCT will be applied
int computeDCTForChannelCr(int *size){
	numConsumedDCTCr += 1;
	pthread_mutex_unlock(&mutexDCTCr);
	sem_wait(&semDCTCrchan);
	int index = 0;
	if(b == 4){
		lockableNode* blockindexNode = getFirstUnlockedElem(buffDCTCrchan);
		index = blockindexNode->offset;
		computeDCT(Crchannel,Crline,Crcolumn,blockindexNode->offset);
		pthread_mutex_lock(&mutexDCTCr);
		*size = numConsumedDCTCr;
	}else if(b == 2){
		lockableNode* blockindexNode = getFirstElemFor422(buffDCTCrchan);
		while(blockindexNode == NULL){
			sem_wait(&semDCTCrchan);
			lockableNode* blockindexNode = getFirstElemFor422(buffDCTCrchan);
		}
		index = (blockindexNode->offset)/2;
		computeDCT(Crchannel,Crline,Crcolumn,(blockindexNode->offset)/2);
		pthread_mutex_lock(&mutexDCTCr);
		*size = numConsumedDCTCr;
	}else{
		lockableNode* blockindexNode = getFirstElemFor420(buffDCTCrchan,Ycolumn/8);
		while(blockindexNode == NULL){
			sem_wait(&semDCTCrchan);
			lockableNode* blockindexNode = getFirstElemFor420(buffDCTCrchan,Ycolumn/8);
		}
		//need to compute the corresponding offset in the Cr channel
		int oldL = (blockindexNode->offset)/(Ycolumn/8);
		int oldO = (blockindexNode->offset)%(Ycolumn/8);
		int newL = oldL/2;
		int newO = oldO/2;
		int newOffset = newL*((Ycolumn/8)/2) + newO;
		index = newOffset;
		computeDCT(Crchannel,Crline,Crcolumn,newOffset);
		pthread_mutex_lock(&mutexDCTCr);
		*size = numConsumedDCTCr;
	}
	return index;
}
void* dispatchForProcessing(void *arg){
	char *param = (char *)arg;
	int size;
	int res;
	int index;
	lockableNode *newNode = NULL;
	if(*param == 'y'){
		pthread_mutex_lock(&mutexDCTY);
		size = numConsumedDCTY; //need to keep track of how many blocks have already been consumed
		while(size < (Yline/8)*(Ycolumn/8)){
			index = computeDCTForChannelY(&size);
			//signal to quantization step that a new block is ready to be processed
			newNode = createNewNode(index);
			res = addElementAtFront(newNode,&buffQuantYchan,&mutexQuantY);
			while(res != 1){
				res = addElementAtFront(newNode,&buffQuantYchan,&mutexQuantY);
			}
		}
		pthread_mutex_unlock(&mutexDCTY);
	}else if(*param == 'b'){
		pthread_mutex_lock(&mutexDCTCb);
		size = numConsumedDCTCb; //need to keep track of how many blocks have already been consumed
		while(size < (Cbline/8)*(Cbcolumn/8)){
			index = computeDCTForChannelCb(&size);
			//signal to quantization step that a new block is ready to be processed
			newNode = createNewNode(index);
			res = addElementAtFront(newNode,&buffQuantCbchan,&mutexQuantCb);
			while(res != 1){
				res = addElementAtFront(newNode,&buffQuantCbchan,&mutexQuantCb);
			}
		}
		pthread_mutex_unlock(&mutexDCTCb);
	}else{
		pthread_mutex_lock(&mutexDCTCr);
		size = numConsumedDCTCr; //need to keep track of how many blocks have already been consumed
		while(size < (Crline/8)*(Crcolumn/8)){
			index = computeDCTForChannelCr(&size);
			//signal to quantization step that a new block is ready to be processed
			newNode = createNewNode(index);
			res = addElementAtFront(newNode,&buffQuantCrchan,&mutexQuantCr);
			while(res != 1){
				res = addElementAtFront(newNode,&buffQuantCrchan,&mutexQuantCr);
			}
		}
		pthread_mutex_unlock(&mutexDCTCr);
	}
	return NULL;	
}


void DCTparallelStep(int numThreads){
	pthread_t YchanThreads[numThreads];
	int i;
	int err;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	for(i = 0; i < numThreads; i++){
		char arg = 'y';
		err = pthread_create(&(YchanThreads[i]),&attr,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CbchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'b';
		printf("CbthreadCreated\n");
		err = pthread_create(&(CbchanThreads[i]),&attr,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CrchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'r';
		err = pthread_create(&(CrchanThreads[i]),&attr,&dispatchForProcessing,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
}

void* Quantization(void *arg){
	return NULL;
}
void QuantizationThreadCreation(int numThreads){
	pthread_t YchanThreads[numThreads];
	int i;
	int err;
	for(i = 0; i < numThreads; i++){
		char arg = 'y';
		//printf("YthreadCreated\n");
		err = pthread_create(&(YchanThreads[i]),NULL,&Quantization,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CbchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'b';
		printf("CbthreadCreated\n");
		err = pthread_create(&(CbchanThreads[i]),NULL,&Quantization,(void *)&arg);
		if(err!=0)
			error(err,"pthread_create");
	}
	pthread_t CrchanThreads[numThreads];
	for(i = 0; i < numThreads; i++){
		char arg = 'r';
		//printf("CrthreadCreated\n");
		err = pthread_create(&(CrchanThreads[i]),NULL,&Quantization,(void *)&arg);
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
	sem_init(&semQuantYchan, 0, 0);
	sem_init(&semQuantCbchan, 0, 0);
	sem_init(&semQuantCrchan, 0, 0);
	pthread_mutex_init(&mutexDCTY,NULL);
	pthread_mutex_init(&mutexDCTCb,NULL);
	pthread_mutex_init(&mutexDCTCr,NULL);
	pthread_mutex_init(&mutexDwnSclY,NULL);
	pthread_mutex_init(&mutexDwnSclCb,NULL);
	pthread_mutex_init(&mutexDwnSclCr,NULL);
	pthread_mutex_init(&mutexQuantY,NULL);
	pthread_mutex_init(&mutexQuantCb,NULL);
	pthread_mutex_init(&mutexQuantCr,NULL);
	numConsumedDCTY = 0;
	numConsumedDCTCb = 0;
	numConsumedDCTCr = 0;
	numConsumedQuantY = 0;
	numConsumedQuantY = 0;
	numConsumedQuantY = 0;
}

void freeGlobalVariables(){
	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);
	sem_destroy(&semDCTYchan);
	sem_destroy(&semDCTCbchan);
	sem_destroy(&semDCTCrchan);
	sem_destroy(&semQuantYchan);
	sem_destroy(&semQuantCbchan);
	sem_destroy(&semQuantCrchan);
	pthread_mutex_destroy(&mutexDCTY);
	pthread_mutex_destroy(&mutexDCTCb);
	pthread_mutex_destroy(&mutexDCTCr);
	pthread_mutex_destroy(&mutexDwnSclY);
	pthread_mutex_destroy(&mutexDwnSclCb);
	pthread_mutex_destroy(&mutexDwnSclCr);
	pthread_mutex_destroy(&mutexQuantY);
	pthread_mutex_destroy(&mutexQuantCb);
	pthread_mutex_destroy(&mutexQuantCr);
	
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
	printf("buffer DCT Y\n");
	while(buffDCTYchan != NULL){
		printf("%d,",buffDCTYchan->offset);
		free(buffDCTYchan);
		buffDCTYchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffDCTCbchan->next;
	printf("buffer DCT Cb\n");
	while(buffDCTCbchan != NULL){
		printf("%d,",buffDCTCbchan->offset);
		free(buffDCTCbchan);
		buffDCTCbchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffDCTCrchan->next;
	printf("buffer DCT Cr\n");
	while(buffDCTCrchan != NULL){
		printf("%d,",buffDCTCrchan->offset);
		free(buffDCTCrchan);
		buffDCTCrchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffQuantYchan->next;
	printf("buffer Quant Y\n");
	while(buffQuantYchan != NULL){
		printf("%d,",buffQuantYchan->offset);
		free(buffQuantYchan);
		buffQuantYchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffQuantCbchan->next;
	printf("buffer Quant Cb\n");
	while(buffQuantCbchan != NULL){
		printf("%d,",buffQuantCbchan->offset);
		free(buffQuantCbchan);
		buffQuantCbchan = tmp;
		if(tmp != NULL)
			tmp = tmp->next;
	}
	printf("\n");
	tmp = buffQuantCrchan->next;
	printf("buffer Quant Cr\n");
	while(buffQuantCrchan != NULL){
		printf("%d,",buffQuantCrchan->offset);
		free(buffQuantCrchan);
		buffQuantCrchan = tmp;
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
	QuantizationThreadCreation(numThreads);
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

