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

#define elements 2048
int datasize = elements * sizeof(float);
float a[elements], b[elements], result[elements];
size_t deviceBufferSize;

//call with gcc gpu.c -o gpu -I/opt/intel/opencl-1.2-5.0.0.43/include -lOpenCL
void main(int argc, void** argv){
	int i;
	for(i = 0; i < elements; i++){
		a[i] = i;
		b[i] = i;
	}
	//Select platform:
	cl_platform_id firstPlatformId;
	cl_uint numPlatforms;
	cl_int errNum = clGetPlatformIDs(1,&firstPlatformId, &numPlatforms);

	//Create context on platform:
	cl_context_properties contextProperties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties) firstPlatformId, 0};
	cl_context context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_CPU, NULL, NULL, &errNum);

	//Create command queue:
	errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &deviceBufferSize);
	cl_device_id *devices = (cl_device_id *)malloc(deviceBufferSize);
	errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, deviceBufferSize, devices, NULL);
	cl_command_queue commandQueue = clCreateCommandQueueWithProperties(context, devices[0], 0, NULL);

	//Create program from kernel source file
	FILE *fp = fopen("vec_add.cl","r");
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
		clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, sizeof(buildLog), buildLog, NULL);
		exit(-1);
	}

	//Create OpenCL kernel:
	cl_kernel kernel = clCreateKernel(program, "vec_add", NULL);
	


	//Create buffer objects and write data to device
	cl_mem bufferA = clCreateBuffer(context, CL_MEM_READ_ONLY, datasize,NULL,NULL);
	cl_mem bufferB = clCreateBuffer(context, CL_MEM_READ_ONLY, datasize,NULL,NULL);
	cl_mem bufferC = clCreateBuffer(context, CL_MEM_WRITE_ONLY, datasize, NULL,NULL);
	clEnqueueWriteBuffer(commandQueue,bufferA,CL_TRUE,0,datasize,a,0,NULL,NULL);
	clEnqueueWriteBuffer(commandQueue,bufferB,CL_TRUE,0,datasize,b,0,NULL,NULL);

	//Set kernel arguments (a,b,result)
	errNum = clSetKernelArg(kernel,0,sizeof(cl_mem),&bufferA);
	errNum = clSetKernelArg(kernel,1,sizeof(cl_mem),&bufferB);
	errNum = clSetKernelArg(kernel,2,sizeof(cl_mem),&bufferC);

	//Queue the kernel up for execution across the array
	size_t globalWorkSize[1] = {elements};
	errNum = clEnqueueNDRangeKernel(commandQueue, kernel,1,NULL,globalWorkSize,NULL,0,NULL,NULL);

	//wait for the commands to complete before reading back results
	clFinish(commandQueue);

	//Copy the output buffer back to the host
	clEnqueueReadBuffer(commandQueue,bufferC,CL_TRUE,0,datasize,result,0,NULL,NULL);
	/**for(i = 0; i < elements; i++){
		printf("%f,",result[i]);
	}**/
	free(source_str);
}



