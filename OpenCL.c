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
double C[64];
double Ct[64];

typedef struct dimensions{
	int Yline;
	int Ycolumn;
 	int Cbline;
	int Cbcolumn;
	int Crline;
	int Crcolumn;
	int cRate;
} dimensions;

void computeCmatrix(double C[64],double Ct[64],int N){
	double pi = atan( 1.0 ) * 4.0;
	int i;
	int j;
	int k;
	for(i = 0; i < N; i++){
		C[i] = sqrt(1.0/(double)N);
		Ct[i*8] = C[i];
	}
	for(j = 1; j < N; j++){
		for(k = 0; k < N; k++){
			C[j*8+k] = sqrt(2.0/(double)N)*cos((j*(2*k+1)*pi)/(2.0*N));
			Ct[k*8+j] = C[j*8+k];
		}
	}
}

//the call is : gcc OpenCL.c -o open -I/opt/intel/opencl-1.2-5.0.0.43/include -lOpenCL -lm
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

	computeCmatrix(C,Ct,8);

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

	//local channels
	Ychannel = (int*) malloc(dim.Yline*dim.Ycolumn*sizeof(int));
	Cbchannel = (int*) malloc(dim.Cbline*dim.Cbcolumn*sizeof(int));
	Crchannel = (int*) malloc(dim.Crline*dim.Crcolumn*sizeof(int));
	//uchar *imgRet = (uchar*) malloc(dim.Yline*dim.Ycolumn*3*sizeof(uchar));


	/*-------------------------------------------------STEP 1 DOWNSAMPLING-------------------------------------------------------*/
	
	//Create OpenCL kernel:
	cl_kernel kernel = clCreateKernel(program, "downscaling", NULL);

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
	
	//Queue the kernel up for execution across the array
	size_t globalWorkSize[1] = {dim.Yline};
	clEnqueueNDRangeKernel(commandQueue, kernel,1,NULL,globalWorkSize,NULL,0,NULL,NULL);

	//wait for the commands to complete before reading back results
	clFinish(commandQueue);

	/*------------------------------------------------STEP 2 : DCT computation------------------------------------------------*/
	
	//Create OpenCL kernel:
	cl_kernel kernel2 = clCreateKernel(program, "DCTtransform", NULL);
	
	double answ[64];
	//Create buffer objects and write data to device
	cl_mem bufferC = clCreateBuffer(context, CL_MEM_READ_ONLY,8*8*sizeof(double),NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");
	cl_mem bufferCt = clCreateBuffer(context, CL_MEM_READ_ONLY,8*8*sizeof(double),NULL,&error);
	if(error != CL_SUCCESS)
		printf("error\n");

	error = clEnqueueWriteBuffer(commandQueue,bufferC,CL_TRUE,0,8*8*sizeof(double),(const void*)C,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error enqueuing buffer 1\n");
	error = clEnqueueWriteBuffer(commandQueue,bufferCt,CL_TRUE,0,8*8*sizeof(double),(const void*)Ct,0,NULL,NULL);
	if(error != CL_SUCCESS)
		printf("error enqueuing buffer 2\n");
	
	//Set kernel arguments
	clSetKernelArg(kernel2,0,sizeof(cl_mem),&bufferC);
	clSetKernelArg(kernel2,1,sizeof(cl_mem),&bufferCt);
	clSetKernelArg(kernel2,2,sizeof(cl_mem),&bufferY);
	clSetKernelArg(kernel2,3,sizeof(cl_mem),&bufferCb);
	clSetKernelArg(kernel2,4,sizeof(cl_mem),&bufferCr);
	clSetKernelArg(kernel2,5,sizeof(cl_mem),&bufferDim);

	//Queue the kernel up for execution across the array
	int numBlocksY = (dim.Yline/8)*(dim.Ycolumn/8);
	int numBlocksCb = (dim.Cbline/8)*(dim.Cbcolumn/8);
	int numBlocksCr = (dim.Crline/8)*(dim.Crcolumn/8);
	size_t globalWorkSize2[1] = {numBlocksY+numBlocksCb+numBlocksCr};
	clEnqueueNDRangeKernel(commandQueue, kernel2,1,NULL,globalWorkSize2,NULL,0,NULL,NULL);

	//wait for the commands to complete before reading back results
	clFinish(commandQueue);

	/*------------------------------------------------STEP 3 : quantization----------------------------------------------------*/

	//Create OpenCL kernel:
	cl_kernel kernel3 = clCreateKernel(program, "Quantization", NULL);

	//no buffers need to be created or written because all information allready in device global memory 

	//Set kernel arguments
	clSetKernelArg(kernel3,0,sizeof(cl_mem),&bufferY);
	clSetKernelArg(kernel3,1,sizeof(cl_mem),&bufferCb);
	clSetKernelArg(kernel3,2,sizeof(cl_mem),&bufferCr);
	clSetKernelArg(kernel3,3,sizeof(cl_mem),&bufferDim);
	
	//Queue the kernel up for execution. Reuse globalWorkSize of previous step
	clEnqueueNDRangeKernel(commandQueue, kernel3,1,NULL,globalWorkSize2,NULL,0,NULL,NULL);
	
	//wait for the commands to complete before reading back results
	clFinish(commandQueue);

	
	/*------------------------------------------------testing section----------------------------------------------------*/

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
	
	printf("Ychannel\n");
	int j;
	for(i = 0; i < dim.Yline; i++){
		for(j = 0; j < dim.Ycolumn; j++){
			printf("%d ",Ychannel[i*dim.Ycolumn+j]);
		}
		printf("\n");
	}
	printf("Cbchannel\n");
	for(i = 0; i < dim.Cbline; i++){
		for(j = 0; j < dim.Cbcolumn; j++){
			printf("%d ",Cbchannel[i*dim.Cbcolumn+j]);
		}
		printf("\n");
	}
	printf("Crchannel\n");
	for(i = 0; i < dim.Crline; i++){
		for(j = 0; j < dim.Crcolumn; j++){
			printf("%d ",Crchannel[i*dim.Crcolumn+j]);
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

