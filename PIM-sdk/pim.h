#ifndef LIB_PIM_H_
#define LIB_PIM_H_
#include <string>
#include <vector>
#include <bitset>
#ifdef __cplusplus
extern "C" {
#endif

using namespace std;

/////////////////////////////////// dummy PiM SDK  ////////////////////////////
void print_pim_SDK(int data);

int PIM_SDK_alloc_buffer(int size, float* data);

void get_PIM_SDK_buffer(int PiM_addr, float* data);

void PIM_SDK_print_buffer_info(int PIM_addr);

int PIM_dispatch_code(std::vector<int> PiM_addr_vec, int op_type, std::vector<std::vector<int>> PiM_dim_inf, std::vector<int> &output_shape);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  
