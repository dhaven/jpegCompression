#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>

#define ROUND( a )      ( ( (a) < 0 ) ? (int) ( (a) - 0.5 ) : \
                                                  (int) ( (a) + 0.5 ) )

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef unsigned char uchar;

int Yline,Ycolumn,Cbline,Cbcolumn,Crline,Crcolumn,b;
int *Ychannel;
int *Cbchannel;
int *Crchannel;
uchar *data;
double C[8][8];
double Ct[8][8];

typedef struct dimensions{
	int Yline;
	int Ycolumn;
 	int Cbline;
	int Cbcolumn;
	int Crline;
	int Crcolumn;
	int cRate;
} dimensions;

int main(int argc, char* argv[]){
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
	dimensions dim;
	if(b == 4){
		dim.Yline = y;
		dim.Ycolumn = x;
		dim.Cbline = y;
		dim.Cbcolumn = x;
		dim.Crline = y;
		dim.Crcolumn = x; 
		dim.cRate = 4;
	}else if(b == 2){
		dim.Yline = y;
		dim.Ycolumn = x;
		dim.Cbline = y;
		dim.Cbcolumn = x/2; 
		dim.Crline = y;
		dim.Crcolumn = x/2;
		dim.cRate = 2;
	}else{ //b = 0
		dim.Yline = y;
		dim.Ycolumn = x;
		dim.Cbline = y/2;
		dim.Cbcolumn = x/2;
		dim.Crline = y/2;
		dim.Crcolumn = x/2;
		dim.cRate = 0;
	}
	//select a platform
	cl_platform_id firstPlatformId;
	cl_uint numPlatforms;
	cl_int errNum = clGetPlatformIDs(1,&firstPlatformId, &numPlatforms);

	//choosing device (GPU if possible)
	cl_uint ciDeviceCount;
  	cl_device_id *device;
	errNum = clGetDeviceIDs (firstPlatformId, CL_DEVICE_TYPE_ALL, 0, NULL, &ciDeviceCount);
	device = (cl_device_id*)malloc(sizeof(cl_device_id) * ciDeviceCount);
	errNum = clGetDeviceIDs (firstPlatformId, CL_DEVICE_TYPE_ALL, ciDeviceCount, device, &ciDeviceCount);
	int i;
	cl_device_type type;
	for(i = 0; i < ciDeviceCount; ++i )  {
 		clGetDeviceInfo(device[i], CL_DEVICE_TYPE, sizeof(type), &type, NULL);
		if(type ==  CL_DEVICE_TYPE_GPU){
			break;
		}
	}
	
	//Create context on platform:
	cl_context_properties contextProperties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties) firstPlatformId, 0};
	cl_context context = clCreateContextFromType(contextProperties,type, NULL, NULL, &errNum);

	//Create command queue:
	size_t deviceBufferSize;
	errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &deviceBufferSize);
	cl_device_id *devices = (cl_device_id *)malloc(deviceBufferSize);
	errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, deviceBufferSize, devices, NULL);
	cl_command_queue commandQueue = clCreateCommandQueueWithProperties(context, devices[0], 0, NULL);

	//Create program from kernel source file
	FILE *fp = fopen("jpegSteps.cl","r");
	// obtain file size:
	fseek (fp , 0 , SEEK_END);
	long lSize = ftell (fp);
	rewind (fp);
	char* source_str = (char*) malloc(lSize);
	size_t source_size = fread(source_str,1,lSize,fp);
	fclose(fp);
	cl_program program = clCreateProgramWithSource(context,1,(const char**)&source_str,(const size_t*) &source_size, NULL);
	errNum = clBuildProgram(program,0,NULL,NULL,NULL,NULL);
	if(errNum != CL_SUCCESS){
		//Determine the reason for the compile error
		char buildLog[16384];
		clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, sizeof(char)*16384, buildLog, NULL);
		printf("error\n");
		int k;
		for(k = 0; k < 16384; k++){
		printf("%c",buildLog[k]);
		}
		exit(-1);
	}

	//Create OpenCL kernel:
	cl_kernel kernel = clCreateKernel(program, "downscaling", NULL);

	//local channels
	Ychannel = (int*) malloc(dim.Yline*dim.Ycolumn*sizeof(int));
	Cbchannel = (int*) malloc(dim.Cbline*dim.Cbcolumn*sizeof(int));
	Crchannel = (int*) malloc(dim.Crline*dim.Crcolumn*sizeof(int));
	//uchar *imgRet = (uchar*) malloc(dim.Yline*dim.Ycolumn*3*sizeof(uchar));

	//Create buffer objects and write data to device
	cl_int error;
	cl_mem bufferImage = clCreateBuffer(context, CL_MEM_READ_ONLY,dim.Yline*dim.Ycolumn*3*sizeof(uchar),NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");
	cl_mem bufferY = clCreateBuffer(context, CL_MEM_READ_WRITE,dim.Yline*dim.Ycolumn*sizeof(int),NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");
	cl_mem bufferCb = clCreateBuffer(context, CL_MEM_READ_WRITE,dim.Cbline*dim.Cbcolumn*sizeof(int),NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");
	cl_mem bufferCr = clCreateBuffer(context, CL_MEM_READ_WRITE,dim.Crline*dim.Crcolumn*sizeof(int), NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");
	cl_mem bufferDim = clCreateBuffer(context, CL_MEM_READ_ONLY,sizeof(dimensions), NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");

	//cl_mem bufferA = clCreateBuffer(context, CL_MEM_READ_ONLY, datasize,NULL,NULL);
	//cl_mem bufferB = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(dimensions),NULL,NULL);
	//cl_mem bufferC = clCreateBuffer(context, CL_MEM_WRITE_ONLY, datasize, NULL,NULL);
	//clEnqueueWriteBuffer(commandQueue,bufferA,CL_TRUE,0,datasize,(const void*)a,0,NULL,NULL);
	//clEnqueueWriteBuffer(commandQueue,bufferB,CL_TRUE,0,sizeof(dimensions),(const void*)&dim,0,NULL,NULL);

	error = clEnqueueWriteBuffer(commandQueue,bufferImage,CL_TRUE,0,dim.Yline*dim.Ycolumn*3*sizeof(uchar),(const void*)data,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error enqueuing buffer 1\n");
	error = clEnqueueWriteBuffer(commandQueue,bufferDim,CL_TRUE,0,sizeof(dimensions),(const void*)&dim,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error enqueuing buffer 2\n");
	
	
	//Set kernel arguments
	clSetKernelArg(kernel,0,sizeof(cl_mem),&bufferImage);
	clSetKernelArg(kernel,1,sizeof(cl_mem),&bufferY);
	clSetKernelArg(kernel,2,sizeof(cl_mem),&bufferCb);
	clSetKernelArg(kernel,3,sizeof(cl_mem),&bufferCr);
	clSetKernelArg(kernel,4,sizeof(cl_mem),&bufferDim);

	//errNum = clSetKernelArg(kernel,0,sizeof(cl_mem),&bufferA);
	//errNum = clSetKernelArg(kernel,1,sizeof(cl_mem),&bufferB);
	//errNum = clSetKernelArg(kernel,2,sizeof(cl_mem),&bufferC);
	
	//Queue the kernel up for execution across the array
	size_t globalWorkSize[1] = {dim.Yline};
	clEnqueueNDRangeKernel(commandQueue, kernel,1,NULL,globalWorkSize,NULL,0,NULL,NULL);

	//size_t globalWorkSize[1] = {elements};
	//errNum = clEnqueueNDRangeKernel(commandQueue, kernel,1,NULL,globalWorkSize,NULL,0,NULL,NULL);

	//wait for the commands to complete before reading back results
	clFinish(commandQueue);

	//Copy the output buffer back to the host
	error =clEnqueueReadBuffer(commandQueue,bufferY,CL_TRUE,0,dim.Yline*dim.Ycolumn*sizeof(int),Ychannel,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error with reading buffer\n");
	error =clEnqueueReadBuffer(commandQueue,bufferCb,CL_TRUE,0,dim.Cbline*dim.Cbcolumn*sizeof(int),Cbchannel,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error with reading buffer\n");
	error =clEnqueueReadBuffer(commandQueue,bufferCr,CL_TRUE,0,dim.Crline*dim.Crcolumn*sizeof(int),Crchannel,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error with reading buffer\n");
	int j;
	printf("Ychannel\n");
	for(i = 0; i < dim.Yline; i++){
		for(j = 0; j < dim.Ycolumn; j++){
			printf("%d ",Ychannel[i*dim.Yline+j]);
		}
		printf("\n");
	}
	printf("Cbchannel\n");
	for(i = 0; i < dim.Cbline; i++){
		for(j = 0; j < dim.Cbcolumn; j++){
			printf("%d ",Cbchannel[i*dim.Cbline+j]);
		}
		printf("\n");
	}
	printf("Crchannel\n");
	for(i = 0; i < dim.Crline; i++){
		for(j = 0; j < dim.Crcolumn; j++){
			printf("%d ",Crchannel[i*dim.Crline+j]);
		}
		printf("\n");
	}
	/**clEnqueueReadBuffer(commandQueue,bufferC,CL_TRUE,0,datasize,result,0,NULL,NULL);
	for(i = 0; i < elements; i++){
		printf("%f,",result[i]);
	}**/
	free(source_str);
	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);
	stbi_image_free(data);

}

