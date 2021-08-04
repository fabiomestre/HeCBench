#include "morphology.h"

void display(unsigned char *img, const int height, const int width)
{
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++)
      printf("%d ", img[i*width+j]);
    printf("\n");
  }
  printf("\n");
}

int main(int argc, char* argv[])
{
  int hsize = atoi(argv[1]);  // kernel width
  int vsize = atoi(argv[2]);  // kernel height
  int width = atoi(argv[3]);  // image width
  int height = atoi(argv[4]); // image height

  unsigned int memSize = width * height * sizeof(unsigned char);

  unsigned char* srcImg = (unsigned char*) malloc (memSize);

  for (int i = 0; i < height; i++) 
    for (int j = 0; j < width; j++)
      srcImg[i*width+j] = (i == (height/2 - 1) && 
                           j == (width/2 - 1)) ? WHITE : BLACK;

  unsigned char* img_d;
  cudaMalloc((void **) &img_d, memSize);
  cudaMemcpy(img_d, srcImg, memSize, cudaMemcpyHostToDevice);

  unsigned char* tmp_d;
  cudaMalloc((void **) &tmp_d, memSize);

  for (int n = 0; n < 100; n++) {
    dilate(img_d, tmp_d, width, height, hsize, vsize);
    erode(img_d, tmp_d, width, height, hsize, vsize);
  }

  cudaMemcpy(srcImg, img_d, memSize, cudaMemcpyDeviceToHost);

  int s = 0;
  for (unsigned int i = 0; i < memSize; i++) s += srcImg[i];
  printf("%s\n", s == WHITE ? "PASS" : "FAIL");

  cudaFree(img_d);
  cudaFree(tmp_d);
  free(srcImg);
  return 0;
}
