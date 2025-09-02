#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CC_T_TEXT = 1,
    CC_T_HTML = 2,
    CC_T_NUM  = 3,
    CC_T_BYTES= 4,
    CC_T_ARRAY= 5
} cc_type_t;

typedef struct {
    const uint8_t* data;
    uint32_t len;
} cc_span_t;

typedef struct {
    uint32_t count;
    const uint32_t* indices;
} cc_array_t;

typedef struct {
    cc_type_t tag;
    union {
        cc_span_t span; // TEXT/HTML/BYTES
        double num;     // NUM
        cc_array_t arr; // ARRAY
    } v;
} cc_const_t;

typedef struct {
    uint32_t name_idx;
    uint32_t code_off;
} cc_func_t;

typedef struct {
    uint32_t path_idx;
    uint32_t func_index;
} cc_route_t;

typedef struct {
    // mapped file
    const uint8_t* base;
    size_t size;

    // tables
    cc_const_t* consts;
    uint32_t const_count;
    cc_func_t* funcs;
    uint32_t func_count;
    cc_route_t* routes;
    uint32_t route_count;

    // code
    const uint8_t* code;
    uint32_t code_size;
} cc_module_t;

typedef struct {
    const uint8_t* ip;
    int sp;
} cc_call_frame_t;

typedef struct {
    // simple stack VM
    const cc_module_t* mod;
    const uint8_t* ip;
    cc_span_t out_buf;
    size_t out_len;
    // extremely small stack for v1
    cc_span_t stack_spans[256];
    double stack_nums[64];
    uint8_t stack_tags[256];
    int sp;
    // call stack for functions
    cc_call_frame_t call_stack[32];
    int call_sp;
} cc_vm_t;

int cc_load_module(const uint8_t* bytes, size_t size, cc_module_t* out);
void cc_vm_init(cc_vm_t* vm, const cc_module_t* mod, uint32_t entry_off);
int cc_vm_run(cc_vm_t* vm, int (*write_fn)(const void*, size_t, void*), void* user);

// helpers
cc_span_t cc_const_text(const cc_module_t* mod, uint32_t idx);
int cc_find_route(const cc_module_t* mod, const char* path, uint32_t* out_entry_off);

// simple in-C bundler (MVP): build a CCBC blob from a pages directory
// returns 0 on success and allocates *out_buf. Caller must free(*out_buf).
int cc_build_bundle_from_pages(const char* pages_dir, uint8_t** out_buf, size_t* out_len);

#ifdef __cplusplus
}
#endif


