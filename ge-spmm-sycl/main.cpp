#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <fstream>
#include <chrono>
#include "common.h"
#include "kernels.h"

#include "./util/mmio.hpp"
#include "./util/util.hpp"

// validate GPU results
#define VALIDATE

#define CLEANUP(s) \
  do { \
    printf("%s", s); \
    if (A_data) free(A_data); \
    if (A_indptr) free(A_indptr); \
    if (A_indices) free(A_indices); \
    if (B) free(B); \
    if (C) free(C); \
    if (golden) free(golden); \
    fflush(stdout); \
  } while (0)

void spmmWrapper(
    queue &q,
    int method, int tile_row, int A_nrows, int B_ncols, 
    buffer<int, 1> &A_rowPtr, buffer<int, 1> &A_colInd,
    buffer<float, 1> &A_val, buffer<float, 1> &B, buffer<float, 1> &C)
{
  const int smem_size = 8*tile_row*(sizeof(int)+sizeof(float)); // size in words
  range<2> lws (tile_row, 32);

  switch(method) {
    case 1:
      q.submit([&] (handler &cgh) {
        auto a_rptr = A_rowPtr.get_access<sycl_read>(cgh);
        auto a_cidx = A_colInd.get_access<sycl_read>(cgh);
        auto a_val = A_val.get_access<sycl_read>(cgh);
        auto b = B.get_access<sycl_read>(cgh);
        auto c = C.get_access<sycl_discard_write>(cgh);
        accessor<int, 1, sycl_read_write, access::target::local> sm(smem_size, cgh);
        cgh.parallel_for<class t1>(nd_range<2>(range<2> ((B_ncols+31)/32 * tile_row, 
                                                         (A_nrows+tile_row-1)/tile_row * 32),
                                               lws), [=] (nd_item<2> item) {
          spmm_test1<float>(item, sm.get_pointer(), A_nrows, B_ncols, 
                            a_rptr.get_pointer() , a_cidx.get_pointer(),
                            a_val.get_pointer(), b.get_pointer(), c.get_pointer());
        });
      });
      break;
    case 2:
      q.submit([&] (handler &cgh) {
        auto a_rptr = A_rowPtr.get_access<sycl_read>(cgh);
        auto a_cidx = A_colInd.get_access<sycl_read>(cgh);
        auto a_val = A_val.get_access<sycl_read>(cgh);
        auto b = B.get_access<sycl_read>(cgh);
        auto c = C.get_access<sycl_discard_write>(cgh);
        accessor<int, 1, sycl_read_write, access::target::local> sm(smem_size, cgh);
        cgh.parallel_for<class t2>(nd_range<2>(range<2> ((B_ncols+63)/64 * tile_row, 
                                                         (A_nrows+tile_row-1)/tile_row * 32),
                                               lws), [=] (nd_item<2> item) {
          spmm_test2<float>(item, sm.get_pointer(), A_nrows, B_ncols, 
                            a_rptr.get_pointer() , a_cidx.get_pointer(),
                            a_val.get_pointer(), b.get_pointer(), c.get_pointer());
        });
      });
      break;
    case 3:
      q.submit([&] (handler &cgh) {
        auto a_rptr = A_rowPtr.get_access<sycl_read>(cgh);
        auto a_cidx = A_colInd.get_access<sycl_read>(cgh);
        auto a_val = A_val.get_access<sycl_read>(cgh);
        auto b = B.get_access<sycl_read>(cgh);
        auto c = C.get_access<sycl_discard_write>(cgh);
        accessor<int, 1, sycl_read_write, access::target::local> sm(smem_size, cgh);
        cgh.parallel_for<class t3>(nd_range<2>(range<2> ((B_ncols+127)/128 * tile_row, 
                                                         (A_nrows+tile_row-1)/tile_row * 32),
                                               lws), [=] (nd_item<2> item) {
          spmm_test3<float>(item, sm.get_pointer(), A_nrows, B_ncols, 
                            a_rptr.get_pointer() , a_cidx.get_pointer(),
                            a_val.get_pointer(), b.get_pointer(), c.get_pointer());
        });
      });
      break;
    case 4:
      q.submit([&] (handler &cgh) {
        auto a_rptr = A_rowPtr.get_access<sycl_read>(cgh);
        auto a_cidx = A_colInd.get_access<sycl_read>(cgh);
        auto a_val = A_val.get_access<sycl_read>(cgh);
        auto b = B.get_access<sycl_read>(cgh);
        auto c = C.get_access<sycl_discard_write>(cgh);
        accessor<int, 1, sycl_read_write, access::target::local> sm(smem_size, cgh);
        cgh.parallel_for<class t4>(nd_range<2>(range<2> ((B_ncols+255)/256 * tile_row, 
                                                         (A_nrows+tile_row-1)/tile_row * 32),
                                               lws), [=] (nd_item<2> item) {
          spmm_test4<float>(item, sm.get_pointer(), A_nrows, B_ncols, 
                            a_rptr.get_pointer() , a_cidx.get_pointer(),
                            a_val.get_pointer(), b.get_pointer(), c.get_pointer());
        });
      });
      break;
    default: printf("Please choose one of the methods 1,2,3,4\n");
  }
}


int main(int argc, char** argv) {
  if (argc != 4) {
    printf("Usage: %s <matrix file> <tile row> <repeat>\n", argv[0]);
    return 1;
  }

  int A_nrows, A_ncols, nnz, B_ncols;
  int max_ncols=256; // 256, 512

  std::vector<int> row_indices;
  std::vector<int> col_indices;
  std::vector<float> values;

  // Host allocate
  int* A_indptr = 0;
  int* A_indices = 0;
  float* A_data = 0;
  float* B = 0;
  float* C = 0;
  float* golden = 0;

  printf("reading data file ...\n");
  readMtx<float>(argv[1], row_indices, col_indices, values, A_nrows, A_ncols, nnz);

  const int tile_row = atoi(argv[2]);
  const int repeat = atoi(argv[3]);

  A_data = (float *)malloc(nnz*sizeof(A_data[0]));
  A_indptr = (int *)malloc((A_nrows+1)*sizeof(A_indptr[0]));
  A_indices = (int *)malloc(nnz*sizeof(A_indices[0]));
  B = (float *)malloc((max_ncols*A_ncols)*sizeof(B[0]));

  if ( !A_data || !A_indices || !A_indptr || !B ) {
    CLEANUP("Host malloc failed\n");
    return 1;
  }

#ifdef VALIDATE
  C = (float *)malloc((A_nrows*max_ncols)*sizeof(C[0]));
  golden = (float *)malloc((A_nrows*max_ncols)*sizeof(golden[0]));
  if (!C || !golden) {
    CLEANUP("Host malloc failed\n");
    return 1;
  }
#endif

  /* format conversation COO -> CSR */
  for (int i=0; i<A_nrows+1; i++) {
    A_indptr[i] = 0;
  }
  for (int n=0; n<nnz; n++) {
    int row = row_indices[n];
    if (row>=A_nrows) fprintf(stderr, "out of bound row\n");
    A_indptr[row+1]++;
  }
  for (int n=1; n<A_nrows+1; n++) {
    A_indptr[n] += A_indptr[n-1];
  }
  for (int n=0; n<nnz; n++) {
    int ptr = A_indptr[row_indices[n]];
    if (col_indices[n]>A_ncols) fprintf(stderr, "out of bound column\n");
    A_indices[ptr] = col_indices[n];
    // A_data[ptr] = values[n];
    A_data[ptr] = 1;
    ptr++;
    A_indptr[row_indices[n]]=ptr;
  }
  for (int n=A_nrows-1; n>0; n--) {
    A_indptr[n] = A_indptr[n-1];
  }
  A_indptr[0] = 0; // COO->CSR finish

  printf("read file ok. N=%d nnz=%d\n", A_nrows, nnz);

  /* random assign */
  srand(123);
  for (int i=0; i<max_ncols*A_ncols; i++)
    B[i] = float(rand()%100 - 50)/100;

#ifdef VALIDATE
  for (int i=0; i<A_nrows; i++) {
    for (int k=0; k<max_ncols; k++) {
      float acc = 0.f;
      for (int ptr=A_indptr[i]; ptr<A_indptr[i+1]; ptr++) {
        acc += A_data[ptr]*B[(max_ncols*A_indices[ptr]+k)];
      }
      golden[(max_ncols*i+k)] = acc;
    }
  }
#endif

#ifdef USE_GPU
  gpu_selector dev_sel;
#else
  cpu_selector dev_sel;
#endif
  queue q(dev_sel);

  // allocate device memory 
  buffer<int, 1> A_indptr_dev (A_indptr, A_nrows+1);
  buffer<int, 1> A_indices_dev (A_indices, nnz);
  buffer<float, 1> A_data_dev (A_data, nnz);
  buffer<float, 1> B_dev (B, max_ncols*A_ncols);
  buffer<float, 1> C_dev (max_ncols*A_nrows);

  // execute spmm
  bool ok = true;
  for (B_ncols=256; B_ncols<=max_ncols; B_ncols *= 2) {
    NEXT_METHOD:
    for (int method=1; method<5; method++) {
      q.submit([&] (handler &cgh) {
        auto acc = C_dev.get_access<sycl_write>(cgh);
        cgh.fill(acc, 0.f);
      });

      q.wait();
      auto start = std::chrono::steady_clock::now();

      for (int i=0; i<repeat; i++)
        spmmWrapper(q, method, tile_row, A_nrows, B_ncols,
                    A_indptr_dev, A_indices_dev, A_data_dev, B_dev, C_dev);

      q.wait();
      auto end = std::chrono::steady_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      printf("Average kernel (method %d) execution time %f (s)\n", method, (time * 1e-9f) / repeat);

      q.submit([&] (handler &cgh) {
        auto acc = C_dev.get_access<sycl_read>(cgh);
        cgh.copy(acc, C);
      }).wait();

      #ifdef VALIDATE
      for (int i=0; i<A_nrows; i++) 
        for (int j=0; j<B_ncols; j++) 
          if ( fabs((C[(i*B_ncols+j)] - golden[(i*B_ncols+j)])) > 1e-2 ) {
            printf("b_ncols %d kernel method %d: results mismatch %f %f\n",
                    B_ncols, method, C[(i*B_ncols+j)], golden[(i*B_ncols+j)]);
            ok = false;
            goto NEXT_METHOD;
          }
      #endif
    }
  }

  CLEANUP("");
  printf("%s\n", ok ? "PASS" : "FAIL");
  
  return 0;
}
