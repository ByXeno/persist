
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

typedef struct {
    persist_type_t type;
    uint32_t off;
} persist_field_t;

#ifndef offsetof
#define offsetof(type,member) ((size_t)&(((type *)0)->member))
#endif // offsetof

void write_struct(void* src,persist_field_t* types,uint32_t count,FILE* fptr);
void read_struct(void* src,persist_field_t* types,uint32_t count,FILE* fptr);

#define SIG_FRONT (uint64_t)0xDEADBEEFCAFEBABE
#define SIG_BACK  (uint64_t)0xBEEFABADFEEDFACE
#define SIG_TOTAL_SIZE (sizeof(uint64_t)*2)

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

#if 1
typedef struct {
    char* ptr;
    char* data;
} str_data;

void write_struct
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
    if (!buf) { perror("malloc"); exit(1); }
    str_data *str_all = malloc(count * sizeof(str_data));
    if (!str_all) { free(str_all);perror("malloc"); exit(1); }
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
            default:{fprintf(stderr,"Unknown type!\n");exit(1);}
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
    fwrite(buf,1,size_of_all + SIG_TOTAL_SIZE,fptr);
    for(i = 0;i < str_ptrs_c;++i)
    {
        fwrite(str_all[i].data,1,strlen(str_all[i].data) + 1,fptr);
    }
    free(buf);
    free(str_all);
}

void read_struct
(void* src,persist_field_t* types,uint32_t count,FILE* fptr)
{}

#else

void read_struct
(void* src,persist_field_t* types,uint32_t count,FILE* fptr)
{
    uint32_t total = 0;
    uint32_t i = 0;
    for(;i < count;i++)
    {
        switch(types[i].type)
        {
            case type_bool: total += sizeof(bool);break;
            case type_f64:
            case type_i64:
            case type_u64: total += sizeof(int64_t);break;
            case type_i32:
            case type_u32:
            case type_f32: total += sizeof(int32_t);break;
            case type_i16:
            case type_u16: total += sizeof(int16_t);break;
            case type_i8:
            case type_u8: total += sizeof(int8_t);break;
            case type_str: continue; {
                fprintf(stderr, "type_str not implemented yet\n");
                exit(1);
            } break;
            default: {
                fprintf(stderr, "Unknown type: %d\n", types[i].type);
                exit(1);
            } break;
        }
    }
    char* buf = malloc(total+1);
    fread(buf,sizeof(char),total,fptr);
    void* cur = 0;
    uint32_t x = 0;
    uint32_t size = 0;
    for(i = 0;i < count;i++)
    {
        cur = (char*)src+types[i].off;
        switch(types[i].type)
        {
            case type_bool: size = sizeof(bool);break;
            case type_f64:
            case type_i64:
            case type_u64: size = sizeof(int64_t);break;
            case type_i32:
            case type_u32:
            case type_f32: size = sizeof(int32_t);break;
            case type_i16:
            case type_u16: size = sizeof(int16_t);break;
            case type_i8:
            case type_u8: size = sizeof(int8_t);break;
            case type_str: continue;
            default: {
                fprintf(stderr, "Unknown type: %d\n", types[i].type);
                exit(1);
            } break;
        }
        memcpy(cur,buf+x,size);
        x+=size;
    }
    free(buf);
}
#endif

#endif // PERSIST_IMPLEMENTATION
