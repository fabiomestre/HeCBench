#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <sycl/sycl.hpp>
#include <oneapi/mkl.hpp>

template <typename T>
void transpose(sycl::queue &q, int nrow, int ncol, int repeat) {
  int error = 0;
  const size_t size = nrow * ncol;
  const size_t size_byte = size * sizeof(T);

  T *matrix = (T *) malloc (size_byte);
  for (size_t i = 0; i < size; i++) matrix[i] = rand() % 13; 

  T *matrixT = (T *) malloc (size_byte);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < nrow; i++)
    for (int j = 0; j < ncol; j++)
      matrixT[(j*nrow)+i] = matrix[(i*ncol)+j];

  auto end = std::chrono::steady_clock::now();
  double time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  printf("Host: serial matrix transpose time = %f (ms)\n", time * 1e-6f);

  const T alpha = (T)1;
  const T beta  = (T)0;

  // store host and device results
  T *h_matrixT, *d_matrixT, *d_matrix;
  h_matrixT = (T *) malloc (size_byte);

  d_matrixT = (T *)sycl::malloc_device(size_byte, q);
  d_matrix = (T *)sycl::malloc_device(size_byte, q);
  q.memcpy(d_matrix, matrix, size_byte).wait();

  time = 0.0;

  // warmup to exclude program/kernel setup time
  const int warmup = 4;

  sycl::event status;
  for (int i = 0; i < repeat + warmup; i++) {
    if (i >= warmup) {
      start = std::chrono::steady_clock::now();
    }
    try {
      status = oneapi::mkl::blas::row_major::omatadd_batch(
        q,
        oneapi::mkl::transpose::trans,
        oneapi::mkl::transpose::nontrans,
        nrow,
        ncol,
        alpha,
        d_matrix,
        ncol,
        size,
        beta,
        d_matrix,
        nrow,
        size,
        d_matrixT,
        nrow,
        size,
        1);
    } catch(sycl::exception const& e) {
      std::cout << "\t\tCaught SYCL exception during omatadd_batch:\n"
                << e.what() << std::endl;
      error = 1;
      break;
    }
    status.wait();
    if (i >= warmup) {
      end = std::chrono::steady_clock::now();
      time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
  }

  printf("Device: average matrix transpose time = %f (ms)\n", (time * 1e-6f) / repeat);

  q.memcpy(h_matrixT, d_matrixT, size_byte).wait();

  sycl::free(d_matrix, q);
  sycl::free(d_matrixT, q);

  if (error == 0) { // check host and device results
    error = memcmp(h_matrixT, matrixT, size_byte);
  }

  printf("%s\n", error ? "FAIL" : "PASS");

  free(matrixT);
  free(matrix);
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    printf("Usage %s <matrix row> <matrix col> <repeat>\n", argv[0]);
    return 1;
  }

  int nrow = atoi(argv[1]); 
  int ncol = atoi(argv[2]); 
  const int repeat = atoi(argv[3]); 
  if (nrow <= 0 || ncol <= 0 || repeat < 0) {
    printf("Error: invalid inputs\n");
    return 1;
  }

#ifdef USE_GPU
  sycl::gpu_selector dev_sel;
#else
  sycl::cpu_selector dev_sel;
#endif
  sycl::queue q(dev_sel, sycl::property::queue::in_order());

  printf("----------------FP32 transpose matrix (%d x %d)----------------\n",
         nrow, ncol);
  transpose<float>(q, nrow, ncol, repeat);

  printf("----------------FP64 transpose matrix (%d x %d)----------------\n",
         nrow, ncol);
  transpose<double>(q, nrow, ncol, repeat);

  return 0;
}
