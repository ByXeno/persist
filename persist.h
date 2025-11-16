#ifndef PERSIST_H_
#define PERSIST_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

typedef uint32_t count_t;

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
    got_null_pointer,
    null_with_count,
    malloc_failed,
    fread_failed,
    fwrite_failed,
    front_sig_diff,
    back_sig_diff,
    misalign,
    buffer_too_small,
} persist_ec_t;

typedef struct {
    persist_type_t type;
    uint32_t off;
    uint32_t count;
    bool is_array;
} persist_field_t;

#define STRUCT_FIELD(src,t,name) \
(persist_field_t){.type = t,.off = offsetof(src,name)}
#define STRUCT_FIELD_ARR(src,t,name,c) \
(persist_field_t){.type = t,.off = offsetof(src,name),.is_array = true,.count = offsetof(src,c)}

persist_ec_t persist_write_struct
(void* src, persist_field_t* types, uint32_t count, FILE* fptr);

persist_ec_t persist_serialize_struct
(void* src, persist_field_t* types, uint32_t count,char** out,uint32_t* out_len);

persist_ec_t persist_deserialize_struct
(void* src, persist_field_t* types, uint32_t count,char* buf,uint32_t buf_len);

persist_ec_t persist_read_struct
(void* src, persist_field_t* types, uint32_t count, FILE* fptr);

void persist_error_write(persist_ec_t error_code);

#ifndef SIG_TYPE
#   define SIG_TYPE uint64_t
#endif

#ifndef SIG_FRONT
#   define SIG_FRONT ((SIG_TYPE)0xDEADBEEFCAFEBABEULL)
#endif

#ifndef SIG_BACK
#   define SIG_BACK  ((SIG_TYPE)0xBEEFABADFEEDFACEULL)
#endif

#define SIG_TOTAL_SIZE (sizeof(SIG_TYPE) * 2)

#endif // PERSIST_H_

#ifdef PERSIST_IMPLEMENTATION

static uint32_t get_type_size(persist_field_t type)
{
    switch (type.type)
    {
        case type_bool: return sizeof(bool);
        case type_i64:
        case type_u64:
        case type_f64: return sizeof(uint64_t);
        case type_f32:
        case type_i32:
        case type_u32: return sizeof(uint32_t);
        case type_i16:
        case type_u16: return sizeof(uint16_t);
        case type_i8:
        case type_u8: return sizeof(uint8_t);
        case type_str: return sizeof(char*);
        default:
            fprintf(stderr, "persist failed: unknown type in get_types_size\n");
            exit(1);
    }
}

static uint32_t get_total_size
(void* src,persist_field_t* types, uint32_t count)
{
    uint32_t i = 0;
    uint32_t total = 0;
    total += sizeof(count_t) + SIG_TOTAL_SIZE;
    persist_field_t cur = {0};
    for(;i < count;++i)
    {
        cur = types[i];
        if(cur.is_array && cur.type == type_str)
        {
            char** strs = *(char***)((char*)src + cur.off);
            count_t arr_count = *(count_t*)((char*)src + cur.count);
            if(count)
            {
                if(!strs) {exit(1);}
                uint32_t c = 0;
                for(;c < arr_count;++c)
                {
                    if(strs[c])
                    {total += strlen(strs[c]);}
                }
                total += arr_count + sizeof(count_t);
            }else{total+= 1;}
            continue;
        }
        if(!cur.is_array && cur.type == type_str)
        {
            char* str = *(char**)((char*)src + cur.off);
            if(str)
            {total += strlen(str)+1;}
            else
            {total += 1;}
            continue;
        }
        if(cur.is_array && cur.type != type_str)
        {
            count_t arr_count = *(count_t*)((char*)src + cur.count);
            total += sizeof(count_t) + (arr_count*get_type_size(cur));
            continue;
        }
        switch(cur.type)
        {
            case type_bool: total += sizeof(bool);break;
            case type_i64:
            case type_f64:
            case type_u64: total += sizeof(uint64_t);break;
            case type_i32:
            case type_f32:
            case type_u32: total += sizeof(uint32_t);break;
            case type_i16:
            case type_u16: total += sizeof(uint16_t);break;
            case type_i8:
            case type_u8: total += sizeof(uint8_t);break;
            case type_str:
            default:
                fprintf(stderr, "persist failed: unknown type in get_total_size\n");
                exit(1);
        }
    }
    return total;
}

persist_ec_t persist_write_struct
(void* src, persist_field_t* types, uint32_t count, FILE* fptr)
{
    persist_ec_t ec = success;
    char* out = 0;
    uint32_t out_len = 0;
    ec = persist_serialize_struct(src,types,count,&out,&out_len);
    if(ec) return ec;
    size_t written = fwrite(out,1,out_len,fptr);
    if (written != out_len) ec = fwrite_failed;
    free(out);
    return ec;
}

#define write_buf(data,size) \
    memcpy(ptr,data,size); \
    ptr += size;

persist_ec_t persist_serialize_struct
(void* src, persist_field_t* types, uint32_t count, char** out,count_t* out_len)
{
    persist_ec_t error_code = success;
    if(!src || !types || !out || !out_len) { error_code = got_null_pointer; goto error;}
    *out_len = get_total_size(src,types,count);
    printf("total: %d\n",*out_len);
    char* data = (char*)calloc(*out_len,sizeof(char));
    if (!data) { error_code = malloc_failed; goto error; }
    char* ptr = data;
    SIG_TYPE sig_front = SIG_FRONT;
    SIG_TYPE sig_back = SIG_BACK;
    write_buf(&sig_front,sizeof(SIG_TYPE));
    write_buf(out_len,sizeof(count_t));
    uint32_t i = 0;
    void* cur = 0;
    uint32_t elem_size = 0;
    for(;i < count;++i)
    {
        cur = 0;elem_size = 0;
        cur = (char*)src + types[i].off;
        elem_size = get_type_size(types[i]);
        if(types[i].type == type_str && types[i].is_array)
        {
            char** array = *(char***)cur;
            count_t arr_count = *(count_t*)((char*)src + types[i].count);
            write_buf(&arr_count,sizeof(count_t));
            if(!arr_count) {continue;}
            if(!array) {error_code = null_with_count; goto error;}
            uint32_t c = 0;
            for(;c < arr_count;++c)
            {
                if(!array[c])
                {
                    uint8_t zero_char = 0;
                    write_buf(&zero_char,1);
                    continue;
                }
                uint32_t len = strlen(array[c]) + 1;
                write_buf(array[c],len);
            }
            continue;
        }
        if(types[i].type == type_str && !types[i].is_array)
        {
            char* str = *(char**)cur;
            if(str)
            {
                count_t len = strlen(str) + 1;
                write_buf(str,len);
            }
            else
            {
                uint8_t zero_char = 0;
                write_buf(&zero_char,sizeof(uint8_t));
            }
            continue;
        }
        if(types[i].is_array && types[i].type != type_str)
        {
            void* array = 0;
            count_t arr_count = *(count_t*)((char*)src + types[i].count);
            write_buf(&arr_count,sizeof(count_t));
            if(!arr_count){continue;}
            memcpy(&array,cur,sizeof(void*));
            if(!array) {error_code = null_with_count; goto error;}
            write_buf(array,arr_count*elem_size);
            continue;
        }
        write_buf(cur,elem_size);
    }
    write_buf(&sig_back,sizeof(SIG_TYPE));
    printf("writed: %d\n",(ptr-data));
    if((ptr-data) != *out_len)
    {printf("misalign size:%d\n",*out_len-(ptr-data));error_code = misalign;goto error;}
    *out = data;
    return error_code;
error:
    if(data) {free(data);}
    return error_code;
}

#undef write_buf

#define read_buf(data,len) \
   memcpy(data,ptr,len);\
   ptr += len;

#define write_src(src,size) \
   memcpy(src,ptr,size); \
   ptr += size;
   
persist_ec_t persist_deserialize_struct
(void* src, persist_field_t* types, uint32_t count,char* buf, uint32_t buf_len)
{
    persist_ec_t error_code = success;
    if (!src || !types || !buf) { error_code = got_null_pointer; goto error; }
    if(buf_len < sizeof(count_t) + SIG_TOTAL_SIZE) {error_code = buffer_too_small; goto error;}
    char* ptr = buf;
    uint32_t need_free_count = 0;
    count_t expected_len = 0;
    SIG_TYPE sig_front = SIG_FRONT;
    SIG_TYPE sig_back = SIG_BACK;
    SIG_TYPE read_sig_front = 0;
    SIG_TYPE read_sig_back = 0;
    read_buf(&read_sig_front,sizeof(SIG_TYPE));
    if (read_sig_front != sig_front) {
        error_code = front_sig_diff;
        goto error;
    }
    read_buf(&expected_len,sizeof(count_t));
    if(expected_len > buf_len) {error_code = buffer_too_small; goto error;}
    uint32_t i = 0;
    void* cur = 0;
    uint32_t elem_size = 0;
    for(;i < count;++i)
    {
        cur = 0;elem_size = 0;
        cur = (char*)src + types[i].off;
        elem_size = get_type_size(types[i]);
        if(types[i].type == type_str && types[i].is_array)
        {
            need_free_count++;
            char** array = 0;
            count_t* arr_count = (count_t*)((char*)src + types[i].count);
            write_src(arr_count,sizeof(count_t));
            if(*arr_count)
            {
                array = calloc(*arr_count,sizeof(char*));
                uint32_t c = 0;
                for(;c < *arr_count;++c)
                {
                    array[c] = strdup(ptr);
                    ptr += strlen(array[c]) + 1;
                }
            }
            memcpy(cur,&array,sizeof(void*));
        }
        else if(types[i].type == type_str && !types[i].is_array)
        {
            need_free_count++;
            char** str = (char**)cur;
            *str = strdup(ptr);
            ptr += strlen(*str) + 1;
        }
        else if(types[i].is_array && types[i].type != type_str)
        {
            need_free_count++;
            void* array = 0;
            count_t* arr_count = (count_t*)((char*)src + types[i].count);
            write_src(arr_count,sizeof(count_t));
            if(*arr_count)
            {
                array = calloc(*arr_count,elem_size);
                write_src(array,(*arr_count)*elem_size);
            }
            memcpy(cur,&array,sizeof(void*));
        }
        else{write_src(cur,elem_size);}
    }
    read_buf(&read_sig_back,sizeof(SIG_TYPE));
    if (read_sig_back != sig_back) {
        error_code = back_sig_diff;
        goto error;
    }
    return error_code;
error:
    uint32_t f = 0;
    for(;f < need_free_count;++f)
    {
        cur = 0;elem_size = 0;
        cur = (char*)src + types[f].off;
        void* array;
        elem_size = get_type_size(types[f]);
        if(types[f].is_array && types[f].type != type_str)
        {memcpy(&array,cur,sizeof(void*));free(array);}
        else if(!types[f].is_array && types[f].type == type_str)
        {memcpy(&array,cur,sizeof(void*));free(array);}
        else if(types[f].is_array && types[f].type == type_str)
        {
            char** strs;
            memcpy(&strs,cur,sizeof(void*));
            count_t arr_count = 0;
            memcpy(&arr_count,((char*)src+types[f].count),sizeof(count_t));
            for(uint32_t c = 0;c < arr_count;++c)
            {free(strs[c]);}
            free(strs);
        }
        else{memset(cur,0,elem_size);}
    }
    return error_code;
}

#undef read_buf
#undef write_src
   
persist_ec_t persist_read_struct
(void* dst, persist_field_t* types, uint32_t count, FILE* fptr)
{
    if (!dst || !types || !fptr) return got_null_pointer;
    persist_ec_t error_code = success;
    if (fseek(fptr, 0, SEEK_END) != 0) return fread_failed;
    long file_size = ftell(fptr);
    if (file_size < 0) return fread_failed;
    rewind(fptr);
    char* buf = (char*)malloc((size_t)file_size);
    if (!buf) return malloc_failed;
    size_t read_bytes = fread(buf, 1, (size_t)file_size, fptr);
    if (read_bytes != (size_t)file_size) {
        free(buf);
        return fread_failed;
    }
    error_code = persist_deserialize_struct(dst, types, count, buf, (uint32_t)file_size);
    free(buf);
    return error_code;
}

void persist_error_write(persist_ec_t error_code)
{
    switch (error_code)
    {
        case success: return;
        case got_null_pointer:
            fprintf(stderr, "Persist error: got null pointer as a parameter!\n");
            return;
        case null_with_count:
            fprintf(stderr, "Persist error: got null pointer with a count!\n");
            return;
        case malloc_failed:
            fprintf(stderr, "Persist error: malloc failed!\n");
            return;
        case fread_failed:
            fprintf(stderr, "Persist error: fread failed or short read!\n");
            return;
        case fwrite_failed:
            fprintf(stderr, "Persist error: fwrite failed or short write!\n");
            return;
        case front_sig_diff:
            fprintf(stderr, "Persist error: front signature mismatch!\n");
            return;
        case back_sig_diff:
            fprintf(stderr, "Persist error: back signature mismatch!\n");
            return;
        case misalign:
            fprintf(stderr, "Persist error: buffer is misaligned!\n");
            return;
        case buffer_too_small:
            fprintf(stderr, "Persist error: buffer is too small for given types!\n");
            return;
        default:
            fprintf(stderr, "Persist error: unknown error code: %d\n", (int)error_code);
            return;
        }
}

#endif /* PERSIST_IMPLEMENTATION */

/*
 *  persist - simple serialize/deserialize library for C
 *  Copyright (C) 2025 Menderes Sabaz <sabazmenders@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

