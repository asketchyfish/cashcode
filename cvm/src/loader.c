#include "../include/ccbc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

static uint16_t rd_u16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd_u32(const uint8_t* p){ return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); }

static const uint8_t* p_at(const uint8_t* base, size_t size, uint32_t off, size_t need){
    if(off > size || size - off < need) return NULL;
    return base + off;
}

int cc_load_module(const uint8_t* bytes, size_t size, cc_module_t* out){
    if(size < 32) return -1;
    if(!(bytes[0]=='C' && bytes[1]=='C' && bytes[2]=='B' && bytes[3]=='C')) return -2;
    uint16_t ver = rd_u16(bytes+4);
    if(ver != 1) return -3;
    (void)rd_u16(bytes+6); // flags
    uint32_t off_consts = rd_u32(bytes+8);
    uint32_t off_funcs  = rd_u32(bytes+12);
    uint32_t off_routes = rd_u32(bytes+16);
    uint32_t off_code   = rd_u32(bytes+20);
    uint32_t code_size  = rd_u32(bytes+24);

    if(off_code + code_size > size) return -4;

    out->base = bytes;
    out->size = size;
    // decode constants (simple, only Text for MVP)
    const uint8_t* pc = p_at(bytes, size, off_consts, 4);
    if(!pc) return -5;
    out->const_count = rd_u32(pc);
    pc += 4;
    out->consts = (cc_const_t*)malloc(sizeof(cc_const_t) * out->const_count);
    if(!out->consts) return -6;
    for(uint32_t i=0;i<out->const_count;i++){
        if(pc + 1 > bytes + size) return -7;
        uint8_t tag = *pc++;
        if(tag==1 || tag==2 || tag==4){
            if(pc + 4 > bytes + size) return -8;
            uint32_t len = rd_u32(pc); pc+=4;
            if(pc + len > bytes + size) return -9;
            out->consts[i].tag = (cc_type_t)tag;
            out->consts[i].v.span.data = pc;
            out->consts[i].v.span.len = len;
            pc += len;
        } else if(tag==3){
            if(pc + 8 > bytes + size) return -10;
            out->consts[i].tag = CC_T_NUM;
            double d; memcpy(&d, pc, 8); pc+=8;
            out->consts[i].v.num = d;
        } else if(tag==5){
            if(pc + 4 > bytes + size) return -12;
            uint32_t count = rd_u32(pc); pc+=4;
            if(pc + count*4 > bytes + size) return -13;
            out->consts[i].tag = CC_T_ARRAY;
            out->consts[i].v.arr.count = count;
            out->consts[i].v.arr.indices = (const uint32_t*)pc;
            pc += count * 4;
        } else {
            return -14;
        }
    }
    const uint8_t* pf = p_at(bytes, size, off_funcs, 4);
    if(!pf) return -15;
    out->func_count = rd_u32(pf); pf+=4;
    out->funcs = (cc_func_t*)malloc(sizeof(cc_func_t)*out->func_count);
    if(!out->funcs) return -16;
    for(uint32_t i=0;i<out->func_count;i++){
        out->funcs[i].name_idx = rd_u32(pf); pf+=4;
        out->funcs[i].code_off = rd_u32(pf); pf+=4;
    }
    const uint8_t* pr = p_at(bytes, size, off_routes, 4);
    if(!pr) return -17;
    out->route_count = rd_u32(pr); pr+=4;
    out->routes = (cc_route_t*)malloc(sizeof(cc_route_t)*out->route_count);
    if(!out->routes) return -18;
    for(uint32_t i=0;i<out->route_count;i++){
        out->routes[i].path_idx = rd_u32(pr); pr+=4;
        out->routes[i].func_index = rd_u32(pr); pr+=4;
    }
    out->code = bytes + off_code;
    out->code_size = code_size;
    return 0;
}

cc_span_t cc_const_text(const cc_module_t* mod, uint32_t idx){
    if(idx >= mod->const_count) return (cc_span_t){0};
    return mod->consts[idx].v.span;
}

int cc_find_route(const cc_module_t* mod, const char* path, uint32_t* out_entry_off){
    for(uint32_t i=0;i<mod->route_count;i++){
        cc_span_t s = cc_const_text(mod, mod->routes[i].path_idx);
        if(s.len == strlen(path) && memcmp(s.data, path, s.len)==0){
            uint32_t fi = mod->routes[i].func_index;
            if(fi >= mod->func_count) return -1;
            *out_entry_off = mod->funcs[fi].code_off;
            return 0;
        }
    }
    return -1;
}

static void w32(uint8_t* p, uint32_t v){ p[0]=v&255; p[1]=(v>>8)&255; p[2]=(v>>16)&255; p[3]=(v>>24)&255; }

typedef struct { 
    uint8_t tag; 
    union {
        cc_span_t span;
        struct { uint32_t count; uint32_t* indices; } arr;
    } v;
} CConst;
typedef struct { uint32_t name_idx, code_off; } CFunc;
typedef struct { uint32_t path_idx, func_index; } CRoute;

static uint32_t bc_add_const(CConst** consts, size_t* csz, size_t* ccap, const char* s){
    size_t len = strlen(s);
    if(*csz == *ccap){ *ccap = *ccap ? (*ccap * 2) : 16; *consts = (CConst*)realloc(*consts, (*ccap) * sizeof(CConst)); }
    (*consts)[*csz].tag = 1;
    (*consts)[*csz].v.span.data = (const uint8_t*)strdup(s);
    (*consts)[*csz].v.span.len = (uint32_t)len;
    return (uint32_t)((*csz)++);
}

static uint32_t bc_add_array_const(CConst** consts, size_t* csz, size_t* ccap, uint32_t* indices, uint32_t count){
    if(*csz == *ccap){ *ccap = *ccap ? (*ccap * 2) : 16; *consts = (CConst*)realloc(*consts, (*ccap) * sizeof(CConst)); }
    (*consts)[*csz].tag = 5; // CC_T_ARRAY
    (*consts)[*csz].v.arr.count = count;
    (*consts)[*csz].v.arr.indices = indices;
    return (uint32_t)((*csz)++);
}

static void bc_code_emit(uint8_t** code, size_t* codelen, size_t* codecap, uint8_t b){
    if(*codelen == *codecap){ *codecap = *codecap ? (*codecap * 2) : 64; *code = (uint8_t*)realloc(*code, *codecap); }
    (*code)[(*codelen)++] = b;
}

static void bc_code_u32(uint8_t** code, size_t* codelen, size_t* codecap, uint32_t v){
    if((*codelen) + 4 > *codecap){ *codecap = ((*codelen) + 4) * 2; *code = (uint8_t*)realloc(*code, *codecap); }
    w32((*code) + (*codelen), v); (*codelen) += 4;
}

// -------- simple $-directive expansion (MVP) --------
typedef struct { char* name; char* value; } Var;

static char* str_dup(const char* s){ size_t n=strlen(s); char* r=(char*)malloc(n+1); memcpy(r,s,n+1); return r; }
static char* str_trim(char* s){
    while(*s==' '||*s=='\t' || *s=='\r') s++;
    size_t n=strlen(s);
    while(n>0 && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')){ s[--n]=0; }
    return s;
}

static cc_span_t cc_const_text_from_loader(CConst* consts, uint32_t idx){
    if(idx >= 256) return (cc_span_t){0}; // safety check
    return consts[idx].v.span;
}

static const char* var_get(Var* vars, size_t vcount, const char* name){
    for(size_t i=0;i<vcount;i++){ if(vars[i].name && strcmp(vars[i].name,name)==0) return vars[i].value; }
    return NULL;
}

static char* substitute_vars(const char* line, Var* vars, size_t vcount){
    // Replace occurrences of {$name} with value
    const char* p=line; size_t cap=strlen(line)+1; size_t len=0; char* out=(char*)malloc(cap); out[0]=0;
    while(*p){
        const char* start = strstr(p, "{$");
        if(!start){
            size_t rem=strlen(p); if(len+rem+1>cap){ cap = (len+rem+1)*2; out=(char*)realloc(out,cap);} memcpy(out+len,p,rem+1); len+=rem; break;
        }
        size_t chunk = (size_t)(start - p); if(len+chunk+1>cap){ cap=(len+chunk+1)*2; out=(char*)realloc(out,cap);} memcpy(out+len,p,chunk); len+=chunk;
        const char* close = strchr(start, '}');
        if(!close){ // no closing brace, copy rest
            size_t rem=strlen(start); if(len+rem+1>cap){ cap=(len+rem+1)*2; out=(char*)realloc(out,cap);} memcpy(out+len,start,rem+1); len+=rem; break;
        }
        char name[128]={0}; size_t nlen = (size_t)(close - (start+2)); if(nlen>sizeof(name)-1) nlen=sizeof(name)-1; memcpy(name, start+2, nlen); name[nlen]=0;
        const char* val = var_get(vars, vcount, name);
        if(!val) val="";
        size_t vlen=strlen(val); if(len+vlen+1>cap){ cap=(len+vlen+1)*2; out=(char*)realloc(out,cap);} memcpy(out+len,val,vlen); len+=vlen;
        p = close+1;
    }
    out[len]=0; return out;
}

static void emit_text_line(const char* line, CConst** consts, size_t* csz, size_t* ccap, uint8_t** code, size_t* codelen, size_t* codecap){
    uint32_t idx = bc_add_const((CConst**)consts, csz, ccap, line);
    bc_code_emit(code,codelen,codecap,0x01); bc_code_u32(code,codelen,codecap,idx); // CONST
    bc_code_emit(code,codelen,codecap,0x03); // PRINT_RAW
}

static char* read_joined_file(const char* base_dir, const char* rel){
    char path[1024];
    if(rel[0]=='/') snprintf(path, sizeof(path), "%s%s", base_dir, rel);
    else snprintf(path, sizeof(path), "%s/%s", base_dir, rel);
    FILE* f = fopen(path, "rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char* buf=(char*)malloc(sz+1); if(!buf){ fclose(f); return NULL; }
    if(fread(buf,1,sz,f)!=(size_t)sz){ fclose(f); free(buf); return NULL; }
    fclose(f); buf[sz]=0; return buf;
}

static char* replace_slot(const char* layout, const char* body){
    const char* slot = strstr(layout, "<slot/>");
    if(!slot){ return str_dup(layout); }
    size_t before = (size_t)(slot - layout);
    size_t after_len = strlen(slot + 7);
    size_t body_len = strlen(body);
    char* out = (char*)malloc(before + body_len + after_len + 1);
    memcpy(out, layout, before);
    memcpy(out + before, body, body_len);
    memcpy(out + before + body_len, slot + 7, after_len + 1);
    return out;
}

static const char* process_block(const char* cur, const char* pages_dir,
                                 CConst** consts, size_t* csz, size_t* ccap,
                                 CFunc** funcs, size_t* fsz, size_t* fcap,
                                 uint8_t** code, size_t* codelen, size_t* codecap,
                                 Var* vars, size_t* vcount, size_t vcap){
    // Process lines until $end or end of string. Returns pointer to the position after ending line.
    while(*cur){
        // get line
        const char* nl = strchr(cur, '\n'); size_t linelen = nl ? (size_t)(nl - cur) : strlen(cur);
        char* raw = (char*)malloc(linelen+1); memcpy(raw, cur, linelen); raw[linelen]=0;
        char* line = str_trim(raw);
        if(line[0]=='$'){
            if(strncmp(line, "$end", 4)==0){ free(raw); return nl? nl+1 : cur+linelen; }
            if(strncmp(line, "$else", 5)==0){ free(raw); return cur; }
            if(strncmp(line, "$function ", 10)==0){
                // $function name() { ... }
                char* p = line+10; while(*p==' '||*p=='\t') p++;
                char name[128]={0}; size_t i=0; while(*p && *p!='('){ if(i<sizeof(name)-1) name[i++]=*p; p++; }
                name[i]=0;
                // skip to opening brace
                while(*p && *p!='{') p++;
                if(*p!='{'){ free(raw); cur = nl? nl+1 : cur+linelen; continue; }
                p++; // skip {
                
                // find matching closing brace
                const char* after_func = nl? nl+1 : cur+linelen;
                const char* func_start = after_func;
                int brace_depth = 1;
                const char* scan = after_func;
                const char* func_end = NULL;
                while(*scan){
                    if(*scan == '{') brace_depth++;
                    else if(*scan == '}'){ brace_depth--; if(brace_depth == 0){ func_end = scan; break; } }
                    scan++;
                }
                if(!func_end){ free(raw); return after_func; }
                
                // compile function body
                uint32_t func_code_start = (uint32_t)*codelen;
                Var func_vars[32]; size_t func_vcount = 0;
                const char* func_pos = func_start;
                while(func_pos < func_end){
                    func_pos = process_block(func_pos, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, func_vars, &func_vcount, 32);
                    if(!func_pos) break;
                }
                bc_code_emit(code, codelen, codecap, 0x41); // OP_RETURN
                
                // add function to function table
                uint32_t name_idx = bc_add_const(consts, csz, ccap, name);
                if(*fsz == *fcap){ *fcap = *fcap ? *fcap*2 : 8; *funcs = (CFunc*)realloc(*funcs, *fcap*sizeof(CFunc)); }
                (*funcs)[*fsz].name_idx = name_idx;
                (*funcs)[*fsz].code_off = func_code_start;
                (*fsz)++;
                
                cur = func_end + 1;
                free(raw);
                continue;
            }
            if(strncmp(line, "$call ", 6)==0){
                // $call functionName()
                char* p = line+6; while(*p==' '||*p=='\t') p++;
                char name[128]={0}; size_t i=0; while(*p && *p!='('){ if(i<sizeof(name)-1) name[i++]=*p; p++; }
                name[i]=0;
                
                // find function index
                uint32_t func_idx = 0;
                int found = 0;
                for(size_t j = 0; j < *fsz; j++){
                    cc_span_t func_name = cc_const_text_from_loader(*consts, (*funcs)[j].name_idx);
                    if(func_name.len == strlen(name) && memcmp(func_name.data, name, func_name.len) == 0){
                        func_idx = (uint32_t)j;
                        found = 1;
                        break;
                    }
                }
                if(found){
                    bc_code_emit(code, codelen, codecap, 0x40); // OP_CALL
                    bc_code_u32(code, codelen, codecap, func_idx);
                }
                
                free(raw); cur = nl? nl+1 : cur+linelen; continue;
            }
            if(strncmp(line, "$let ", 5)==0){
                char* p = line+5; while(*p==' '||*p=='\t') p++;
                char name[128]={0}; size_t i=0; while(*p && *p!=' ' && *p!='\t' && *p!='='){ if(i<sizeof(name)-1) name[i++]=*p; p++; }
                name[i]=0; while(*p && *p!='=' ) p++; if(*p=='=') p++; while(*p==' '||*p=='\t') p++;
                
                if(*p=='['){
                    // Array literal: $let items = ["a", "b", "c"]
                    p++; // skip [
                    uint32_t indices[32]; uint32_t count = 0;
                    while(*p && *p!=']' && count < 32){
                        while(*p==' '||*p=='\t') p++;
                        if(*p=='"' || *p=='\''){
                            char q=*p++; char* start=p; while(*p && *p!=q) p++;
                            char tmp=*p; *p=0;
                            uint32_t elem_idx = bc_add_const(consts, csz, ccap, start);
                            indices[count++] = elem_idx;
                            *p=tmp; p++;
                        }
                        while(*p==' '||*p=='\t') p++;
                        if(*p==',') p++;
                    }
                    if(*p==']'){
                        uint32_t* arr_indices = (uint32_t*)malloc(count * sizeof(uint32_t));
                        memcpy(arr_indices, indices, count * sizeof(uint32_t));
                        uint32_t array_idx = bc_add_array_const(consts, csz, ccap, arr_indices, count);
                        // Store array index as variable value
                        char val_str[16]; snprintf(val_str, sizeof(val_str), "%u", array_idx);
                        if(*vcount<vcap){ vars[*vcount].name=str_dup(name); vars[*vcount].value=str_dup(val_str); (*vcount)++; }
                    }
                } else if(*p=='"' || *p=='\''){
                    char q=*p++; char* start=p; while(*p && *p!=q) p++; char tmp=*p; *p=0; const char* val=start; if(*vcount<vcap){ vars[*vcount].name=str_dup(name); vars[*vcount].value=str_dup(val); (*vcount)++; } *p=tmp;
                }
                free(raw); cur = nl? nl+1 : cur+linelen; continue;
            }
            if(strncmp(line, "$if ", 4)==0){
                char* cond = line+4; int truthy=0;
                cond = str_trim(cond);
                if(strcmp(cond, "true")==0 || strcmp(cond, "1")==0) truthy=1;
                else if(strcmp(cond, "false")==0 || strcmp(cond, "0")==0) truthy=0;
                else if(cond[0]=='$'){ const char* val = var_get(vars, *vcount, cond+1); truthy = (val && *val); }
                // process inner until $else/$end
                const char* after_if = nl? nl+1 : cur+linelen;
                const char* inner = after_if;
                // find matching
                int depth=1; const char* scan=after_if; const char* else_pos=NULL; const char* end_pos=NULL;
                while(*scan){
                    const char* lnl = strchr(scan,'\n'); size_t llen = lnl ? (size_t)(lnl - scan) : strlen(scan);
                    char* tmp=(char*)malloc(llen+1); memcpy(tmp,scan,llen); tmp[llen]=0; char* t=str_trim(tmp);
                    if(t[0]=='$'){
                        if(strncmp(t,"$if ",4)==0) depth++;
                        else if(strncmp(t,"$end",4)==0){ depth--; if(depth==0){ end_pos = scan; free(tmp); break; } }
                        else if(depth==1 && strncmp(t,"$else",5)==0){ else_pos = scan; }
                    }
                    free(tmp);
                    scan = lnl ? lnl+1 : scan+llen;
                }
                if(!end_pos){ free(raw); return after_if; }
                if(truthy){
                    const char* true_end = else_pos ? else_pos : end_pos;
                    const char* p = inner;
                    while(p < true_end){ p = process_block(p, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, vars, vcount, vcap); }
                } else if(else_pos){
                    const char* false_start = strchr(else_pos,'\n'); false_start = false_start ? false_start+1 : end_pos;
                    const char* p = false_start;
                    while(p < end_pos){ p = process_block(p, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, vars, vcount, vcap); }
                }
                // move cur to after $end line
                const char* end_nl = strchr(end_pos,'\n'); cur = end_nl ? end_nl+1 : end_pos; free(raw); continue;
            }
            if(strncmp(line, "$for ", 5)==0){
                // $for $item in a,b,c OR $for $item in arrayVariable
                char* p = line+5; while(*p==' '||*p=='\t') p++;
                if(*p=='$') p++;
                char vname[128]={0}; size_t vi=0; while(*p && *p!=' '&&*p!='\t') { if(vi<sizeof(vname)-1) vname[vi++]=*p; p++; }
                vname[vi]=0; while(*p==' '||*p=='\t') p++; if(strncmp(p,"in",2)==0) p+=2; while(*p==' '||*p=='\t') p++;
                
                // check if it's an array variable (starts with $)
                char* list = NULL;
                if(*p == '$'){
                    // Array variable: $for $item in $arrayVar
                    char array_name[128]={0}; size_t ai=0; p++; // skip $
                    while(*p && *p!=' '&&*p!='\t'&&*p!='\n'){ if(ai<sizeof(array_name)-1) array_name[ai++]=*p; p++; }
                    array_name[ai]=0;
                    const char* array_val = var_get(vars, *vcount, array_name);
                    if(array_val){
                        // For now, just use the array index as a simple list
                        list = str_dup(array_val);
                    }
                } else {
                    // CSV list: $for $item in a,b,c
                    list = str_dup(p);
                }
                // find block bounds
                const char* after_for = nl? nl+1 : cur+linelen;
                const char* inner = after_for; int depth=1; const char* end_pos=NULL; const char* scan=after_for;
                while(*scan){ const char* lnl=strchr(scan,'\n'); size_t llen=lnl?(size_t)(lnl-scan):strlen(scan); char* tmp=(char*)malloc(llen+1); memcpy(tmp,scan,llen); tmp[llen]=0; char* t=str_trim(tmp); if(t[0]=='$'){ if(strncmp(t,"$for ",5)==0) depth++; else if(strncmp(t,"$end",4)==0){ depth--; if(depth==0){ end_pos=scan; free(tmp); break; } } } free(tmp); scan=lnl?lnl+1:scan+llen; }
                if(!end_pos){ free(list); free(raw); return after_for; }
                // iterate CSV
                char* saveptr=NULL; char* tok=strtok_r(list, ",", &saveptr);
                while(tok){ char* tv=str_trim(tok);
                    // push var
                    if(*vcount < vcap){ vars[*vcount].name = str_dup(vname); vars[*vcount].value = str_dup(tv); (*vcount)++; }
                    // process inner
                    const char* p2 = inner;
                    while(p2 < end_pos){ p2 = process_block(p2, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, vars, vcount, vcap); }
                    // pop var
                    if(*vcount>0){ free(vars[*vcount-1].name); free(vars[*vcount-1].value); (*vcount)--; }
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                free(list);
                const char* end_nl = strchr(end_pos,'\n'); cur = end_nl ? end_nl+1 : end_pos; free(raw); continue;
            }
            if(strncmp(line, "$include ", 9)==0){
                char* p = line+9; while(*p==' '||*p=='\t') p++;
                if(*p=='"' || *p=='\''){ char q=*p++; char* start=p; while(*p && *p!=q) p++; char tmp=*p; *p=0; const char* rel=start; char* inc = read_joined_file(pages_dir, rel); *p=tmp; if(inc){ Var vtmp[32]; size_t vcnt=*vcount; const char* pos = inc; while(*pos){ pos = process_block(pos, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, vars, &vcnt, 32); if(!*pos) break; } free(inc);} }
                free(raw); cur = nl? nl+1 : cur+linelen; continue;
            }
            if(strncmp(line, "$layout ", 8)==0){
                char* p = line+8; while(*p==' '||*p=='\t') p++;
                if(*p=='"' || *p=='\''){
                    char q=*p++; char* start=p; while(*p && *p!=q) p++; char tmp=*p; *p=0; const char* rel=start;
                    // read layout file
                    char* lay = read_joined_file(pages_dir, rel);
                    *p=tmp;
                    // the body is the remainder after this line
                    const char* body_start = nl? nl+1 : cur+linelen;
                    char* body = str_dup(body_start);
                    if(lay && body){
                        char* combined = replace_slot(lay, body);
                        if(combined){
                            const char* pos = combined;
                            while(*pos){ pos = process_block(pos, pages_dir, consts, csz, ccap, funcs, fsz, fcap, code, codelen, codecap, vars, vcount, vcap); if(!*pos) break; }
                            free(combined);
                        }
                    }
                    if(lay) free(lay);
                    if(body) free(body);
                    free(raw);
                    // consume rest of source; we're done at this level
                    return body_start + strlen(body_start);
                }
                free(raw); cur = nl? nl+1 : cur+linelen; continue;
            }
            // unknown $ directive -> ignore line
            free(raw); cur = nl? nl+1 : cur+linelen; continue;
        } else {
            // literal line with var substitution
            char* sub = substitute_vars(line, vars, *vcount);
            // ensure newline
            size_t slen=strlen(sub);
            char* with_nl=(char*)malloc(slen+2); memcpy(with_nl, sub, slen); with_nl[slen]='\n'; with_nl[slen+1]=0;
            emit_text_line(with_nl, (CConst**)consts, csz, ccap, code, codelen, codecap);
            free(with_nl); free(sub); free(raw);
            cur = nl? nl+1 : cur+linelen; continue;
        }
    }
    return cur;
}

int cc_build_bundle_from_pages(const char* pages_dir, uint8_t** out_buf, size_t* out_len){
    // Build constants, functions, routes, and code from .cash files (static HTML after $route)
    DIR* d = opendir(pages_dir); if(!d) return -1;
    CConst* consts = NULL; size_t ccap=0, csz=0;
    CFunc* funcs = NULL; size_t fsz=0, fcap=0;
    CRoute* routes = NULL; size_t rsz=0, rcap=0;
    uint8_t* code = NULL; size_t codelen=0, codecap=0;

    // iterate .cash files

    struct dirent* ent;
    while((ent = readdir(d))){
        const char* n = ent->d_name;
        size_t ln = strlen(n);
        if(ln<6 || strcmp(n+ln-5, ".cash")!=0) continue;
        char path[1024]; snprintf(path, sizeof(path), "%s/%s", pages_dir, n);
        FILE* f = fopen(path, "rb"); if(!f) continue;
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        char* buf = (char*)malloc(sz+1); fread(buf,1,sz,f); buf[sz]=0; fclose(f);
        // first line: $route "..."
        char* nl = strchr(buf,'\n'); if(!nl){ free(buf); continue; }
        *nl = 0; char* first = buf; char* rest = nl+1;
        char* q1 = strchr(first,'"'); char* q2 = q1?strrchr(first,'"'):NULL; if(!q1||!q2||q2<=q1){ free(buf); continue; }
        char route[512]; memcpy(route, q1+1, (size_t)(q2-q1-1)); route[q2-q1-1]=0;

        // function name = filename without extension
        char fname[512]; strncpy(fname, n, sizeof(fname)); fname[sizeof(fname)-1]=0; char* dot=strrchr(fname,'.'); if(dot) *dot=0;
        uint32_t name_idx = bc_add_const(&consts, &csz, &ccap, fname);
        uint32_t func_index = (uint32_t)fsz;
        if(fsz==fcap){ fcap=fcap?fcap*2:8; funcs=(CFunc*)realloc(funcs, fcap*sizeof(CFunc)); }
        funcs[fsz].name_idx = name_idx; funcs[fsz].code_off = (uint32_t)codelen; fsz++;

        uint32_t path_idx = bc_add_const(&consts, &csz, &ccap, route);
        if(rsz==rcap){ rcap=rcap?rcap*2:8; routes=(CRoute*)realloc(routes, rcap*sizeof(CRoute)); }
        routes[rsz].path_idx = path_idx; routes[rsz].func_index = func_index; rsz++;

        // process body with $let/$if/$for, $include, and {$var} substitution
        Var vars[32]; size_t vcount=0;
        (void)process_block(rest, pages_dir, &consts, &csz, &ccap, &funcs, &fsz, &fcap, &code, &codelen, &codecap, vars, &vcount, 32);
        bc_code_emit(&code, &codelen, &codecap, 0x00); // HALT
        free(buf);
    }
    closedir(d);

    // build blobs
    // consts
    size_t const_bytes = 4; 
    for(size_t i=0;i<csz;i++){
        if(consts[i].tag == 5){ // ARRAY
            const_bytes += 1 + 4 + consts[i].v.arr.count * 4;
        } else {
            const_bytes += 1 + 4 + consts[i].v.span.len;
        }
    }
    uint8_t* const_blob = (uint8_t*)malloc(const_bytes); uint8_t* pc=const_blob; w32(pc, (uint32_t)csz); pc+=4;
    for(size_t i=0;i<csz;i++){
        *pc++ = consts[i].tag;
        if(consts[i].tag == 5){ // ARRAY
            w32(pc, consts[i].v.arr.count); pc+=4;
            for(uint32_t j=0; j<consts[i].v.arr.count; j++){
                w32(pc, consts[i].v.arr.indices[j]); pc+=4;
            }
        } else {
            w32(pc, consts[i].v.span.len); pc+=4;
            memcpy(pc, consts[i].v.span.data, consts[i].v.span.len); pc+=consts[i].v.span.len;
        }
    }

    // funcs
    size_t func_bytes = 4 + fsz*8; uint8_t* func_blob=(uint8_t*)malloc(func_bytes); uint8_t* pf=func_blob; w32(pf,(uint32_t)fsz); pf+=4;
    for(size_t i=0;i<fsz;i++){ w32(pf, funcs[i].name_idx); pf+=4; w32(pf, funcs[i].code_off); pf+=4; }

    // routes
    size_t route_bytes = 4 + rsz*8; uint8_t* route_blob=(uint8_t*)malloc(route_bytes); uint8_t* pr=route_blob; w32(pr,(uint32_t)rsz); pr+=4;
    for(size_t i=0;i<rsz;i++){ w32(pr, routes[i].path_idx); pr+=4; w32(pr, routes[i].func_index); pr+=4; }

    uint32_t off_consts = 32;
    uint32_t off_funcs = off_consts + (uint32_t)const_bytes;
    uint32_t off_routes = off_funcs + (uint32_t)func_bytes;
    uint32_t off_code = off_routes + (uint32_t)route_bytes;
    uint32_t code_size = (uint32_t)codelen;
    size_t total = off_code + code_size;
    uint8_t* blob = (uint8_t*)malloc(total);
    memcpy(blob+0, "CCBC", 4); blob[4]=1; blob[5]=0; blob[6]=0; blob[7]=0;
    w32(blob+8, off_consts); w32(blob+12, off_funcs); w32(blob+16, off_routes); w32(blob+20, off_code);
    w32(blob+24, code_size); memset(blob+28, 0, 4);
    memcpy(blob+off_consts, const_blob, const_bytes);
    memcpy(blob+off_funcs, func_blob, func_bytes);
    memcpy(blob+off_routes, route_blob, route_bytes);
    memcpy(blob+off_code, code, codelen);

    *out_buf = blob; *out_len = total;
    free(const_blob); free(func_blob); free(route_blob); free(code);
    for(size_t i=0;i<csz;i++){
        if(consts[i].tag == 5){ // ARRAY
            free((void*)consts[i].v.arr.indices);
        } else {
            free((void*)consts[i].v.span.data);
        }
    }
    free(consts); free(funcs); free(routes);
    return 0;
}


