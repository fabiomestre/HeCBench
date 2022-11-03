#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <random>
#include <chrono>
#include "common.h"

int main(int argc, char* argv[]) {

  if (argc != 2) {
    printf("Usage: ./%s <iterations>\n", argv[0]);
    return 1;
  }

  // specify the number of test cases
  const int iteration = atoi(argv[1]);

  // number of elements to reverse
  const int len = 256;

  // save device result
  int test[len];

  // save expected results after performing reverse operations even/odd times
  int error = 0;
  int gold_odd[len];
  int gold_even[len];

  for (int i = 0; i < len; i++) {
    gold_odd[i] = len-i-1;
    gold_even[i] = i;
  }

#ifdef USE_GPU
  gpu_selector dev_sel;
#else
  cpu_selector dev_sel;
#endif

  queue q(dev_sel, property::queue::in_order());
  
  int *d_test = malloc_device<int>(len, q);
  range<1> gws (len);
  range<1> lws (len);

  std::default_random_engine generator (123);
  // bound the number of reverse operations
  std::uniform_int_distribution<int> distribution(100,9999);

  long time = 0;

  for (int i = 0; i < iteration; i++) {
    const int count = distribution(generator);

    q.memcpy(d_test, gold_even, sizeof(int) * len);
      
    auto start = std::chrono::steady_clock::now();

    for (int j = 0; j < count; j++) {
      q.submit([&](handler &cgh) {
        accessor <int, 1, sycl_read_write, access::target::local> s (len, cgh);
        cgh.parallel_for<class blockReverse>(nd_range<1>(gws, lws), [=](nd_item<1> item) {
          int t = item.get_local_id(0);
          s[t] = d_test[t];
          item.barrier(access::fence_space::local_space);
          d_test[t] = s[len-t-1];
        });
      });
    }

    q.wait();
    auto end = std::chrono::steady_clock::now();
    time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    q.memcpy(test, d_test, sizeof(int) * len).wait();

    if (count % 2 == 0)
      error = memcmp(test, gold_even, len*sizeof(int));
    else
      error = memcmp(test, gold_odd, len*sizeof(int));
    
    if (error) break;
  }
  
  printf("Total kernel execution time: %f (s)\n", time * 1e-9f);
  printf("%s\n", error ? "FAIL" : "PASS");
 
  free(d_test, q);

  return 0;
}
