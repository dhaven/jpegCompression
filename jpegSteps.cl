

typedef struct dimensions {
	int Yline;
	int Ycolumn;
 	int Cbline;
	int Cbcolumn;
	int Crline;
	int Crcolumn;
	int cRate;
}dimensions;




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
