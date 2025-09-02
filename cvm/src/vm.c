#include "../include/ccbc.h"
#include <string.h>

enum {
    OP_HALT=0x00,
    OP_CONST=0x01,
    OP_PRINT_ESC=0x02,
    OP_PRINT_RAW=0x03,
    OP_DROP=0x04,
    OP_TAG_OPEN=0x10,
    OP_TAG_ATTR=0x11,
    OP_TAG_CLOSE=0x12,
    OP_TAG_END=0x13,
    OP_JUMP=0x20,
    OP_JF=0x21,
    OP_ARRAY_GET=0x30,
    OP_ARRAY_LEN=0x31,
    OP_ITER_START=0x32,
    OP_ITER_NEXT=0x33,
    OP_CALL=0x40,
    OP_RETURN=0x41
};

static int write_span(int (*write_fn)(const void*, size_t, void*), void* user, cc_span_t s){
    if(!s.data || s.len==0) return 0;
    return write_fn(s.data, s.len, user);
}

static int write_lit(int (*write_fn)(const void*, size_t, void*), void* user, const char* s){
    return write_fn(s, strlen(s), user);
}

static int write_escaped(int (*write_fn)(const void*, size_t, void*), void* user, cc_span_t s){
    const uint8_t* p = s.data; const uint8_t* end = s.data + s.len;
    const uint8_t* chunk = p;
    while(p < end){
        const char* ent = NULL; size_t entlen = 0;
        switch(*p){
            case '&': ent = "&amp;"; entlen = 5; break;
            case '<': ent = "&lt;"; entlen = 4; break;
            case '>': ent = "&gt;"; entlen = 4; break;
            case '"': ent = "&quot;"; entlen = 6; break;
            case '\'': ent = "&#39;"; entlen = 5; break;
            default: break;
        }
        if(ent){
            if(p > chunk){ if(write_fn(chunk, (size_t)(p - chunk), user) != 0) return -1; }
            if(write_fn(ent, entlen, user) != 0) return -1;
            p++; chunk = p; continue;
        }
        p++;
    }
    if(p > chunk){ if(write_fn(chunk, (size_t)(p - chunk), user) != 0) return -1; }
    return 0;
}

void cc_vm_init(cc_vm_t* vm, const cc_module_t* mod, uint32_t entry_off){
    memset(vm, 0, sizeof(*vm));
    vm->mod = mod;
    vm->ip = mod->code + entry_off;
    vm->sp = -1;
    vm->call_sp = -1;
}

int cc_vm_run(cc_vm_t* vm, int (*write_fn)(const void*, size_t, void*), void* user){
    for(;;){
        uint8_t op = *vm->ip++;
        switch(op){
            case OP_HALT:
                return 0;
            case OP_CONST: {
                uint32_t idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                cc_span_t s = cc_const_text(vm->mod, idx);
                vm->stack_spans[++vm->sp] = s;
                vm->stack_tags[vm->sp] = CC_T_TEXT;
                break;
            }
            case OP_JUMP: {
                int32_t rel = (int32_t)(vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24));
                vm->ip += 4;
                vm->ip += rel;
                break;
            }
            case OP_JF: {
                int32_t rel = (int32_t)(vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24));
                vm->ip += 4;
                // pop condition; treat empty string as false, non-empty as true
                if(vm->sp < 0) return -40;
                cc_span_t s = vm->stack_spans[vm->sp--];
                int truthy = (s.len != 0);
                if(!truthy){ vm->ip += rel; }
                break;
            }
            case OP_PRINT_ESC: {
                if(vm->sp < 0) return -10;
                cc_span_t s = vm->stack_spans[vm->sp--];
                if(write_escaped(write_fn, user, s) != 0) return -11;
                break;
            }
            case OP_PRINT_RAW: {
                if(vm->sp < 0) return -12;
                cc_span_t s = vm->stack_spans[vm->sp--];
                if(write_span(write_fn, user, s) != 0) return -13;
                break;
            }
            case OP_TAG_OPEN: {
                uint32_t idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                cc_span_t name = cc_const_text(vm->mod, idx);
                if(write_lit(write_fn, user, "<")!=0) return -20;
                if(write_span(write_fn, user, name)!=0) return -21;
                break;
            }
            case OP_TAG_ATTR: {
                uint32_t idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                if(vm->sp < 0) return -22;
                cc_span_t val = vm->stack_spans[vm->sp--];
                cc_span_t name = cc_const_text(vm->mod, idx);
                if(write_lit(write_fn, user, " ")!=0) return -23;
                if(write_span(write_fn, user, name)!=0) return -24;
                if(write_lit(write_fn, user, "=\"")!=0) return -25;
                if(write_escaped(write_fn, user, val)!=0) return -26;
                if(write_lit(write_fn, user, "\"")!=0) return -27;
                break;
            }
            case OP_TAG_END: {
                if(write_lit(write_fn, user, ">")!=0) return -28;
                break;
            }
            case OP_TAG_CLOSE: {
                uint32_t idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                cc_span_t name = cc_const_text(vm->mod, idx);
                if(write_lit(write_fn, user, "</")!=0) return -29;
                if(write_span(write_fn, user, name)!=0) return -30;
                if(write_lit(write_fn, user, ">")!=0) return -31;
                break;
            }
            case OP_DROP: {
                if(vm->sp >= 0) vm->sp--;
                break;
            }
            case OP_ARRAY_GET: {
                uint32_t idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                if(vm->sp < 0) return -60;
                // pop array constant index, push array element
                uint32_t array_idx = vm->stack_spans[vm->sp].len; // using len as index for now
                if(array_idx >= vm->mod->const_count) return -61;
                cc_const_t* arr_const = &vm->mod->consts[array_idx];
                if(arr_const->tag != CC_T_ARRAY || idx >= arr_const->v.arr.count) return -62;
                uint32_t elem_idx = arr_const->v.arr.indices[idx];
                cc_span_t elem = cc_const_text(vm->mod, elem_idx);
                vm->stack_spans[vm->sp] = elem;
                vm->stack_tags[vm->sp] = CC_T_TEXT;
                break;
            }
            case OP_ARRAY_LEN: {
                if(vm->sp < 0) return -63;
                uint32_t array_idx = vm->stack_spans[vm->sp].len; // using len as index for now
                if(array_idx >= vm->mod->const_count) return -64;
                cc_const_t* arr_const = &vm->mod->consts[array_idx];
                if(arr_const->tag != CC_T_ARRAY) return -65;
                vm->stack_spans[vm->sp].len = arr_const->v.arr.count;
                vm->stack_tags[vm->sp] = CC_T_TEXT;
                break;
            }
            case OP_ITER_START: {
                if(vm->sp < 0) return -66;
                // for now, just leave array on stack for iteration
                // in a full implementation, we'd set up an iterator frame
                break;
            }
            case OP_ITER_NEXT: {
                int32_t rel = (int32_t)(vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24));
                vm->ip += 4;
                if(vm->sp < 0) return -67;
                // simplified iteration - for now just jump to end
                // in a full implementation, we'd manage iterator state
                vm->ip += rel;
                break;
            }
            case OP_CALL: {
                uint32_t func_idx = vm->ip[0] | (vm->ip[1]<<8) | (vm->ip[2]<<16) | (vm->ip[3]<<24);
                vm->ip += 4;
                if(func_idx >= vm->mod->func_count) return -50;
                // push current frame
                if(vm->call_sp >= 31) return -51;
                vm->call_stack[++vm->call_sp].ip = vm->ip;
                vm->call_stack[vm->call_sp].sp = vm->sp;
                // jump to function
                vm->ip = vm->mod->code + vm->mod->funcs[func_idx].code_off;
                break;
            }
            case OP_RETURN: {
                if(vm->call_sp < 0) return -52;
                // restore frame
                vm->ip = vm->call_stack[vm->call_sp].ip;
                vm->sp = vm->call_stack[vm->call_sp].sp;
                vm->call_sp--;
                break;
            }
            default:
                return -99; // unknown opcode
        }
    }
}


