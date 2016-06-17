typedef struct dimensions {
	int Yline;
	int Ycolumn;
 	int Cbline;
	int Cbcolumn;
	int Crline;
	int Crcolumn;
	int cRate;
}dimensions;

#define ROUND( a )      ( ( (a) < 0 ) ? (int) ( (a) - 0.5 ) : \
                                                  (int) ( (a) + 0.5 ) )

__constant int lumQuantTable[64] = {16,11,10,16,24,40,51,61,
			12,12,14,19,26,58,60,55,
			14,13,16,24,40,57,69,56,
			14,17,22,29,51,87,80,62,
			18,22,37,56,68,109,103,77,
			24,35,55,64,81,104,113,92,
			49,64,78,87,103,121,120,101,
			72,92,95,98,112,100,103,99};

__constant int chromQuantTable[64] = {17,18,24,47,99,99,99,99,
			18,21,26,66,99,99,99,99,
			24,26,56,99,99,99,99,99,
			47,66,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99,
			99,99,99,99,99,99,99,99};

__kernel void downscaling(__global const unsigned char *img, 
							__global int *Ychannel,
							__global int *Cbchannel,
							__global int *Crchannel,
							__global const dimensions *b){
	int gid = get_global_id(0);
	int i;
	int j;
	int ImgOffset;
	int YOffset;
	int CbOffset;
	int CrOffset;

	int Ycolumn = b->Ycolumn;
	int Cbcolumn = b->Cbcolumn;
	int Crcolumn = b->Crcolumn;
	int cRate = b->cRate;

	/**unsigned char localArray[Ycolumn*3];
	for(j = 0; j < Ycolumn*3; j++){
		localArray[j] = img[gid*Ycolumn*3 + j];
	}**/
	for(i = 0; i < Ycolumn; i++){
		ImgOffset = gid*(Ycolumn)*3 + i*3;
		YOffset = gid*(Ycolumn) +i;
		if(cRate == 4){
			*(Ychannel+YOffset) = (int) (0 + (0.299*(*(img+ImgOffset)))+(0.587*(*(img+ImgOffset+1)))+(0.114*(*(img+ImgOffset+2)))); //Y
			*(Cbchannel+YOffset) = (int) (128 - (0.168736*(*(img+ImgOffset)))-(0.331264*(*(img+ImgOffset+1)))+(0.5*(*(img+ImgOffset+2)))); //Cb
			*(Crchannel+YOffset) = (int) (128 + (0.5*(*(img+ImgOffset)))-(0.418688*(*(img+ImgOffset+1)))-(0.081312*(*(img+ImgOffset+2)))); //Cr
		}else if(cRate == 2){
			if(i % 2 != 0){
				*(Ychannel+YOffset) = (int) (0 + (0.299*(*(img+ImgOffset)))+(0.587*(*(img+ImgOffset+1)))+(0.114*(*(img+ImgOffset+2)))); //Y
			}else{
				CbOffset = gid*(Cbcolumn) + i/2;
				CrOffset = gid*(Crcolumn) + i/2;
				*(Ychannel+YOffset) = (int) (0 + (0.299*(*(img+ImgOffset)))+(0.587*(*(img+ImgOffset+1)))+(0.114*(*(img+ImgOffset+2)))); //Y
				*(Cbchannel+CbOffset) = (int) (128 - (0.168736*(*(img+ImgOffset)))-(0.331264*(*(img+ImgOffset+1)))+(0.5*(*(img+ImgOffset+2)))); //Cb
				*(Crchannel+CrOffset) = (int) (128 + (0.5*(*(img+ImgOffset)))-(0.418688*(*(img+ImgOffset+1)))-(0.081312*(*(img+ImgOffset+2)))); //Cr
			}
		}else{
			if(i % 2 != 0 || gid % 2 != 0){
				*(Ychannel+YOffset) = (int) (0 + (0.299*(*(img+ImgOffset)))+(0.587*(*(img+ImgOffset+1)))+(0.114*(*(img+ImgOffset+2)))); //Y
			}else{
				CbOffset = (gid/2)*(Cbcolumn) + i/2;
				CrOffset = (gid/2)*(Crcolumn) + i/2;
				*(Ychannel+YOffset) = (int) (0 + (0.299*(*(img+ImgOffset)))+(0.587*(*(img+ImgOffset+1)))+(0.114*(*(img+ImgOffset+2)))); //Y
				*(Cbchannel+CbOffset) = (int) (128 - (0.168736*(*(img+ImgOffset)))-(0.331264*(*(img+ImgOffset+1)))+(0.5*(*(img+ImgOffset+2)))); //Cb
				*(Crchannel+CrOffset) = (int) (128 + (0.5*(*(img+ImgOffset)))-(0.418688*(*(img+ImgOffset+1)))-(0.081312*(*(img+ImgOffset+2)))); //Cr
			}
		}
	}
}

void computeDCT(__global int *channel, int y, int x,int offset, __global const double *C, __global const double *Ct){
	double temp[64];
	double temp2;
	int i;
	int j;
	int k;
	int lineCoord;
	int colCoord;
	for(i = 0; i < 8; i++){
		lineCoord = (i+ 8*(offset/(x/8)))*x;
		for(j = 0; j < 8; j++){
			temp[i*8+j] = 0.0;
			for(k = 0; k < 8; k++){
				colCoord = k+ (8*(offset % (x/8)));
				temp[i*8+j] += (*(channel+ lineCoord + colCoord)-128) * Ct[k*8+j]; 
			}
		}
	}
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			temp2 = 0.0;
			for(k = 0; k < 8; k++){
				temp2+=C[i*8+k]*temp[k*8+j];
			}
			lineCoord = (i+ 8*(offset/(x/8)))*x;
			colCoord = j+ (8*(offset % (x/8)));
			*(channel+lineCoord+colCoord) = ROUND(temp2);
		}
	}
}

__kernel void DCTtransform(__global const double *C, 
							__global const double *Ct, 
							__global int *Ychannel,
							__global int *Cbchannel,
							__global int *Crchannel,
							__global const dimensions *b){

	int Yline = b->Yline;
	int Ycolumn = b->Ycolumn;
	int Cbline = b->Cbline;
	int Cbcolumn = b->Cbcolumn;
	int Crline = b->Crline;
	int Crcolumn = b->Crcolumn;
	
	int gid = get_global_id(0);
	int blockId;
	if(gid < (Yline/8)*(Ycolumn/8)){
		blockId = gid;
		computeDCT(Ychannel,Yline,Ycolumn,blockId,C,Ct);
	}else if(gid < (Yline/8)*(Ycolumn/8)+(Cbline/8)*(Cbcolumn/8)){
		blockId = gid - (Yline/8)*(Ycolumn/8);
		computeDCT(Cbchannel,Cbline,Cbcolumn,blockId,C,Ct);
	}else{
		blockId = gid - (Yline/8)*(Ycolumn/8) - (Cbline/8)*(Cbcolumn/8);
		computeDCT(Crchannel,Crline,Crcolumn,blockId,C,Ct);
	}

}

void quantization(__global int *channel,int width, int offset, __constant int *quantTable){
	int i;
	int j;
	int lineCoord;
	int colCoord;
	for(i = 0; i < 8; i++){
		for(j = 0; j < 8; j++){
			lineCoord = (i+ 8*(offset/(width/8)))*width;
			colCoord = j+ (8*(offset % (width/8)));
			channel[lineCoord+colCoord] = ROUND(channel[lineCoord+colCoord]/quantTable[i*8+j]);
		}
	}
}

__kernel void Quantization(__global int *Ychannel,
														__global int *Cbchannel,
														__global int *Crchannel,
														__global const dimensions *b){

	int Yline = b->Yline;
	int Ycolumn = b->Ycolumn;
	int Cbline = b->Cbline;
	int Cbcolumn = b->Cbcolumn;
	int Crline = b->Crline;
	int Crcolumn = b->Crcolumn;
	
	int gid = get_global_id(0);
	int blockId;
	if(gid < (Yline/8)*(Ycolumn/8)){
		blockId = gid;
		quantization(Ychannel,Ycolumn,blockId,lumQuantTable);
	}else if(gid < (Yline/8)*(Ycolumn/8)+(Cbline/8)*(Cbcolumn/8)){
		blockId = gid - (Yline/8)*(Ycolumn/8);
		quantization(Cbchannel,Cbcolumn,blockId,chromQuantTable);
	}else{
		blockId = gid - (Yline/8)*(Ycolumn/8) - (Cbline/8)*(Cbcolumn/8);
		quantization(Crchannel,Crcolumn,blockId,chromQuantTable);
	}

}

