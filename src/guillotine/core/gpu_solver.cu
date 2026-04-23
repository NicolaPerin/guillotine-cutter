#include <cuda_runtime.h>
#include <cub/cub.cuh>
#include <stdint.h>
#include <stdio.h>

extern "C" {
#include "solver_core.h"
}

#define MAX_VAL(a, b) ((a) > (b) ? (a) : (b))

struct DPJob {
    int16_t sx;
    int16_t sy;
};

struct GPUMapDevice {
    HashEntry* entries;
    size_t mask;
};

// --- ADD THIS MIXING FUNCTION ---
__device__ __host__ inline uint64_t mix_hash(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return key;
}

__device__ __host__ inline uint64_t pack_key_gpu(uint16_t w, uint16_t h, uint16_t sx, uint16_t sy) {
    return ((uint64_t)w << 48) | ((uint64_t)h << 32) | ((uint64_t)sx << 16) | (uint64_t)sy;
}

__device__ void map_insert(GPUMapDevice map, uint64_t key, uint16_t delta) {
    // --- CHANGED: Apply mix_hash before masking ---
    size_t idx = mix_hash(key) & map.mask;
    
    while (true) {
        unsigned long long assumed = 0;
        unsigned long long old = atomicCAS((unsigned long long*)&map.entries[idx].key, assumed, (unsigned long long)key);
        
        if (old == 0 || old == (unsigned long long)key) {
            map.entries[idx].delta = delta;
            return;
        }
        idx = (idx + 1) & map.mask;
    }
}

__device__ inline int32_t lookup_device(GPUMapDevice map, const int32_t* F_values, int col_stride, int w, int h, int sx, int sy) {
    uint64_t key = pack_key_gpu(w, h, sx, sy);
    
    // --- CHANGED: Apply mix_hash before masking ---
    size_t idx = mix_hash(key) & map.mask;
    
    while (true) {
        uint64_t k = map.entries[idx].key;
        if (k == key) return F_values[w * col_stride + h] - map.entries[idx].delta;
        if (k == 0)   return F_values[w * col_stride + h];
        idx = (idx + 1) & map.mask;
    }
}

__global__ void solve_state_kernel(DPJob* jobs, int num_jobs, int w, int h, int col_stride, const int32_t* F_values, int32_t pure_val, GPUMapDevice map) {
    int job_idx = blockIdx.x;
    if (job_idx >= num_jobs) return;

    DPJob job = jobs[job_idx];
    int sx = job.sx;
    int sy = job.sy;
    int32_t thread_best = 0;

    for (int z = 1 + threadIdx.x; z < w; z += blockDim.x) {
        int32_t left = lookup_device(map, F_values, col_stride, z, h, sx, sy);
        int32_t right = lookup_device(map, F_values, col_stride, w - z, h, sx + z, sy);
        thread_best = MAX_VAL(thread_best, left + right);
    }

    for (int z = 1 + threadIdx.x; z < h; z += blockDim.x) {
        int32_t bottom = lookup_device(map, F_values, col_stride, w, z, sx, sy);
        int32_t top = lookup_device(map, F_values, col_stride, w, h - z, sx, sy + z);
        thread_best = MAX_VAL(thread_best, bottom + top);
    }

    typedef cub::BlockReduce<int32_t, 128> BlockReduce;
    __shared__ typename BlockReduce::TempStorage temp_storage;
    int32_t block_best = BlockReduce(temp_storage).Reduce(thread_best, cub::Max());

    if (threadIdx.x == 0) {
        uint16_t delta = (uint16_t)(pure_val - block_best);
        map_insert(map, pack_key_gpu(w, h, sx, sy), delta);
    }
}

extern "C" {
FdSlab* execute_dp_gpu(int sheet_width, int sheet_height, int col_stride, const int32_t* host_defect_prefix, const int32_t* host_F_values, size_t required_map_capacity) {
    size_t cap = 1;
    while (cap < required_map_capacity * 2) cap *= 2; 
    
    HashEntry* d_entries;
    cudaMalloc(&d_entries, cap * sizeof(HashEntry));
    cudaMemset(d_entries, 0, cap * sizeof(HashEntry)); 
    GPUMapDevice d_map = {d_entries, cap - 1};

    int32_t* d_F_values;
    size_t F_size = (sheet_width + 1) * col_stride * sizeof(int32_t);
    cudaMalloc(&d_F_values, F_size);
    cudaMemcpy(d_F_values, host_F_values, F_size, cudaMemcpyHostToDevice);

    DPJob* host_jobs = (DPJob*)malloc(sheet_width * sheet_height * sizeof(DPJob));
    DPJob* d_jobs;
    cudaMalloc(&d_jobs, sheet_width * sheet_height * sizeof(DPJob));

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            int num_jobs = 0;
            for (int sx = 0; sx <= sheet_width - w; sx++) {
                for (int sy = 0; sy <= sheet_height - h; sy++) {
                    if (defect_count_in_rect(host_defect_prefix, col_stride, sx, sy, sx + w, sy + h) > 0) {
                        host_jobs[num_jobs++] = { (int16_t)sx, (int16_t)sy };
                    }
                }
            }
            if (num_jobs == 0) continue;

            cudaMemcpy(d_jobs, host_jobs, num_jobs * sizeof(DPJob), cudaMemcpyHostToDevice);
            int32_t pure_val = host_F_values[w * col_stride + h];

            solve_state_kernel<<<num_jobs, 128>>>(d_jobs, num_jobs, w, h, col_stride, d_F_values, pure_val, d_map);
            cudaDeviceSynchronize(); 
        }
    }

    FdSlab* slab = (FdSlab*)calloc(1, sizeof(FdSlab));
    slab->sheet_width = sheet_width;
    slab->sheet_height = sheet_height;
    
    GPUMap* h_map = (GPUMap*)malloc(sizeof(GPUMap));
    h_map->capacity = cap;
    h_map->mask = cap - 1;
    h_map->count = required_map_capacity; 
    h_map->entries = (HashEntry*)malloc(cap * sizeof(HashEntry));

    cudaMemcpy(h_map->entries, d_entries, cap * sizeof(HashEntry), cudaMemcpyDeviceToHost);
    slab->map = h_map;

    cudaFree(d_entries);
    cudaFree(d_F_values);
    cudaFree(d_jobs);
    free(host_jobs);
    return slab;
}
}