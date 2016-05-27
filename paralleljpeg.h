#include <pthread.h>

#ifndef PARALLELJPEG_H
#define PARALLELJPEG_H

typedef struct lockableNode lockableNode;
struct lockableNode{
	int offset;
	int validity; //0 if not valid (means already read by a thread) 1 if valid
 	lockableNode* next;
	pthread_mutex_t mutex;

} __attribute__((packed));


#endif
