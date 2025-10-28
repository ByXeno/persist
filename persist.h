
#ifndef PERSIST_H_
#define PERSIST_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    type_bool,
    type_i64,
    type_i32,
    type_i16,
    type_i8,
    type_u64,
    type_u32,
    type_u16,
    type_u8,
    type_f32,
    type_f64,
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

#endif // PERSIST_H_

#ifdef PERSIST_IMPLEMENTATION

void write_struct
(void* src,persist_field_t* types,uint32_t count,FILE* fptr)
{
    char buf[1024];
    uint32_t i = 0;
    void* cur = 0;
    for(;i < count;i++)
    {
        uint32_t size = 0;
        cur = (char*)src+types[i].off;
        switch (types[i].type)
        {
            case type_bool: {
                size = sizeof(bool);
            } break;
            case type_f64:
            case type_u64:
            case type_i64: {
                size = sizeof(uint64_t);
            } break;
            case type_f32:
            case type_u32:
            case type_i32: {
                size = sizeof(uint32_t);
            } break;
            case type_u16:
            case type_i16: {
                size = sizeof(uint16_t);
            }  break;
            case type_u8:
            case type_i8: {
                size = sizeof(uint8_t);
            } break;
            case type_str: {
                fprintf(stderr, "type_str not implemented yet\n");
                exit(1);
            } break;
            default: {
                fprintf(stderr, "Unknown type: %d\n", types[i].type);
                exit(1);
            } break;
        }
        memcpy(buf, cur, size);
        fwrite(buf, 1, size, fptr);
    }
}

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
            case type_str: {
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
            case type_str: {
                fprintf(stderr, "type_str not implemented yet\n");
                exit(1);
            } break;
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

#endif // PERSIST_IMPLEMENTATION
