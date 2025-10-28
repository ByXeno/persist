
#ifndef PERSIST_H_
#define PERSIST_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    type_bool,
    type_i64,
    type_u64,
    type_f64,
    type_f32,
    type_i32,
    type_u32,
    type_i16,
    type_u16,
    type_i8,
    type_u8,
    type_str,
} persist_type_t;

typedef enum {
    success = 0,
    malloc_failed,
    fread_failed,
    fwrite_failed,
    front_sig_diff,
    back_sig_diff,
    unknown_type,
} persist_error_code_t;

typedef struct {
    persist_type_t type;
    uint32_t off;
} persist_field_t;

#ifndef offsetof
#define offsetof(type,member) ((size_t)&(((type *)0)->member))
#endif // offsetof

bool persist_write_struct(void* src,persist_field_t* types,uint32_t count,FILE* fptr);
bool persist_read_struct(void* src,persist_field_t* types,uint32_t count,FILE* fptr);
void persist_error_write(void);

#define SIG_FRONT (uint64_t)0xDEADBEEFCAFEBABE
#define SIG_BACK  (uint64_t)0xBEEFABADFEEDFACE
#define SIG_TOTAL_SIZE (sizeof(uint64_t)*2)

static persist_error_code_t error_code;

#endif // PERSIST_H_

#ifdef PERSIST_IMPLEMENTATION

uint32_t get_types_size
(persist_field_t* types,uint32_t count)
{
    uint32_t i = 0;
    uint32_t total = 0;
    for(;i < count;++i)
    {
        switch(types[i].type)
        {
            case type_bool: total += sizeof(bool);break;
            case type_str:
            case type_i64:
            case type_u64:
            case type_f64: total += sizeof(uint64_t);break;
            case type_f32:
            case type_i32:
            case type_u32: total += sizeof(uint32_t);break;
            case type_i16:
            case type_u16: total += sizeof(uint16_t);break;
            case type_i8:
            case type_u8: total += sizeof(uint8_t);break;
            default: {fprintf(stderr,"Unknown type!\n");exit(1);}
        }
    }
    return total;
}

uint32_t get_str_count
(persist_field_t* types,uint32_t count)
{
    uint32_t total = 0;
    for(uint32_t i = 0;i < count;++i)
    {if(types[i].type == type_str) {total++;}}
    return total;
}

typedef struct {
    char* ptr;
    char* data;
} str_data;

bool persist_write_struct
(void* src,persist_field_t* types,uint32_t count,FILE* fptr)
{
    uint32_t i = 0;
    uint32_t size = 0;
    uint32_t str_off = 0;
    void* cur = 0;
    uint64_t sig_back = SIG_BACK;
    uint64_t sig_front = SIG_FRONT;
    uint32_t size_of_all = get_types_size(types,count);
    char *buf = malloc(size_of_all + SIG_TOTAL_SIZE);
    if (!buf) { error_code = malloc_failed;return false; }
    str_data *str_all = malloc(get_str_count(types,count) * sizeof(str_data));
    if (!str_all) { free(buf);error_code = malloc_failed;return false; }
    uint32_t str_ptrs_c = 0;
    char* ptr = buf;
    memcpy(ptr,&sig_front,sizeof(SIG_FRONT));
    ptr += sizeof(SIG_FRONT);
    for(;i < count;++i)
    {
        cur = (char*)src+types[i].off;
        switch(types[i].type)
        {
            case type_bool: size = sizeof(bool);break;
            case type_i64:
            case type_u64:
            case type_f64: size = sizeof(int64_t);break;
            case type_f32:
            case type_i32:
            case type_u32: size = sizeof(int32_t);break;
            case type_i16:
            case type_u16: size = sizeof(int16_t);break;
            case type_i8:
            case type_u8: size = sizeof(int8_t);break;
            case type_str: /*size+off*/ {
                size = sizeof(int64_t);
                if(!(*(char**)cur)){
                    memset(ptr,0,size);
                }
                else{
                    str_all[str_ptrs_c].ptr = ptr;
                    str_all[str_ptrs_c++].data = (*(char**)cur);
                }
                ptr += size;
            } continue;
            default:{error_code = unknown_type;return false;}
        }
        memcpy(ptr,cur,size);
        ptr += size;
    }
    memcpy(ptr,&sig_back,sizeof(SIG_BACK));
    for(i = 0;i < str_ptrs_c;++i)
    {
        uint64_t data = 0;
        uint64_t len = strlen(str_all[i].data) + 1;
        data = ((len << 32) | str_off);
        memcpy(str_all[i].ptr,&data,sizeof(uint64_t));
        str_off += len;
    }
    if(size_of_all + SIG_TOTAL_SIZE >
    fwrite(buf,1,size_of_all + SIG_TOTAL_SIZE,fptr))
    {error_code = fwrite_failed;return false;}
    for(i = 0;i < str_ptrs_c;++i)
    {
        if(strlen(str_all[i].data) + 1 >
        fwrite(str_all[i].data,1,strlen(str_all[i].data) + 1,fptr))
        {error_code = fwrite_failed;return false;}
    }
    free(buf);
    free(str_all);
    return true;
}

typedef struct {
    uint32_t len;
    char** ptr;
} read_str;

bool persist_read_struct
(void* src,persist_field_t* types,uint32_t count,FILE* fptr)
{
    uint32_t i = 0;
    uint32_t size_of_all = get_types_size(types,count);
    char *buf = malloc(size_of_all + SIG_TOTAL_SIZE);
    if (!buf) { error_code = malloc_failed;return false; }
    if(0 == fread(buf,sizeof(char),size_of_all + SIG_TOTAL_SIZE,fptr))
    { error_code = fread_failed;return false;}
    char* ptr = buf;
    void* cur = 0;
    uint32_t off = 0;
    uint32_t str_len = 0;
    uint32_t str_all_c = 0;
    read_str *str_all = malloc(get_str_count(types,count) * sizeof(read_str));
    if (!str_all) { free(buf);error_code = malloc_failed;return false; }
    if((*(int64_t*)ptr) != (int64_t)SIG_FRONT)
    { error_code = front_sig_diff;return false; }
    ptr += sizeof(int64_t);
    for(;i < count;++i)
    {
        cur = (char*)src+types[i].off;
        switch(types[i].type)
        {
            case type_bool: (*(bool*)cur) = *ptr;ptr += sizeof(bool);break;
            case type_i64:
            case type_u64:
            case type_f64: (*(int64_t*)cur) = *(int64_t*)ptr;ptr += sizeof(int64_t);break;
            case type_f32:
            case type_i32:
            case type_u32: (*(int32_t*)cur) = *(int32_t*)ptr;ptr += sizeof(int32_t);break;
            case type_i16:
            case type_u16: (*(int16_t*)cur) = *(int16_t*)ptr;ptr += sizeof(int16_t);break;
            case type_i8:
            case type_u8: (*(int8_t*)cur) = *(int8_t*)ptr;ptr += sizeof(int8_t);break;
            case type_str: {
                uint64_t val = *(uint64_t*)ptr;
                ptr += sizeof(int64_t);
                if(!val) {(*(char**)cur) = 0;}
                else {
                    str_len += (val >> 32) + 1;
                    str_all[str_all_c].len = (val >> 32);
                    str_all[str_all_c++].ptr = ((char**)cur);
                }
            } break;
            default:error_code = unknown_type;return false;
        }
    }
    if((*(int64_t*)ptr) != (int64_t)SIG_BACK)
    { error_code = back_sig_diff;return false; }
    ptr += sizeof(int64_t);
    free(buf);
    buf = malloc(str_len);
    if (!buf) { free(str_all);error_code = malloc_failed;return false; }
    if(0 == fread(buf,sizeof(char),str_len,fptr))
    { error_code = fread_failed; return false;}
    for(i = 0;i < str_all_c;++i)
    {
        *(str_all[i].ptr) = strdup(buf + off);
        off += str_all[i].len;
    }
    free(buf);
    free(str_all);
    return true;
}

void persist_error_write
(void)
{
    switch(error_code)
    {
        case success: return;
        case malloc_failed:
            fprintf(stderr, "Persist error: malloc function failed!\n");
            return;
        case fread_failed:
            fprintf(stderr, "Persist error: fread failed to read from file!\n");
            return;
        case fwrite_failed:
            fprintf(stderr, "Persist error: fwrite failed to write to file!\n");
            return;
        case front_sig_diff:
            fprintf(stderr, "Persist error: front signature mismatch!\n");
            return;
        case back_sig_diff:
            fprintf(stderr, "Persist error: back signature mismatch!\n");
            return;
        case unknown_type:
            fprintf(stderr, "Persist error: unknown type encountered!\n");
            return;
        default:
            fprintf(stderr, "Persist error: unknown error code: %d\n", error_code);
            return;
    }
}

#endif // PERSIST_IMPLEMENTATION
