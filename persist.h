#ifndef PERSIST_H_
#define PERSIST_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

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
    malloc_failed,
    fread_failed,
    fwrite_failed,
    front_sig_diff,
    back_sig_diff,
    misalign,
    unknown_type,
} persist_ec_t;

typedef struct {
    persist_type_t type;
    uint32_t off;
} persist_field_t;

#ifndef offsetof
#  define offsetof(type,member) ((size_t)&(((type *)0)->member))
#endif

persist_ec_t persist_write_struct(void* src, persist_field_t* types, uint32_t count, FILE* fptr);
persist_ec_t persist_serialize_struct
(void* src, persist_field_t* types, uint32_t count,char** out,uint32_t* out_len);
persist_ec_t persist_deserialize_struct
(void* src, persist_field_t* types, uint32_t count,char* buf);
persist_ec_t persist_read_struct(void* src, persist_field_t* types, uint32_t count, FILE* fptr);
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

//static persist_ec_t error_code = success;

#endif // PERSIST_H_

#ifdef PERSIST_IMPLEMENTATION

/* ----------------- Internal helpers ----------------- */

static uint32_t get_types_size(persist_field_t* types, uint32_t count)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        switch (types[i].type)
        {
            case type_bool: total += sizeof(bool); break;
            case type_str:
            case type_i64:
            case type_u64:
            case type_f64: total += sizeof(uint64_t); break;
            case type_f32:
            case type_i32:
            case type_u32: total += sizeof(uint32_t); break;
            case type_i16:
            case type_u16: total += sizeof(uint16_t); break;
            case type_i8:
            case type_u8: total += sizeof(uint8_t); break;
            default:
                fprintf(stderr, "persist failed: unknown type in get_types_size\n");
                exit(1);
        }
    }
    return total;
}

static uint32_t get_str_count(persist_field_t* types, uint32_t count)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (types[i].type == type_str) ++total;
    return total;
}

static uint32_t get_str_total_len
(void* src,persist_field_t* types, uint32_t count)
{
    uint32_t total = 0;
    char* cur = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (types[i].type == type_str)
        {
            cur = (char*)src + types[i].off;
            total += strlen(cur)+1;
        }
    }
    return total;
}


/* ptr_in_buf:location in the metadata buffer where the 64-bit (len<<32 | offset) lives */
/* src:actual C-string pointer from the struct */
typedef struct {
    char* ptr_in_buf; 
    const char* src;
} str_data_write;

/* len:length in bytes (including NUL) */
/* dst:pointer-to-char* inside user's struct where we will set strdup result */
typedef struct {
    uint32_t len;
    char** dst;
} str_data_read;

/* ----------------- Serialize implementation ----------------- */

persist_ec_t persist_serialize_struct
(void* src, persist_field_t* types, uint32_t count, char** out,uint32_t* out_len)
{
    persist_ec_t error_code = success;
    if(!src || !types || !out || !out_len) { error_code = got_null_pointer; goto error;}
    uint32_t str_total_len = get_str_total_len(src,types,count);
    uint32_t size_of_meta = get_types_size(types,count);
    *out_len = size_of_meta + str_total_len + SIG_TOTAL_SIZE;
    char* data = (char*)calloc(*out_len,sizeof(char));
    if (!data) { error_code = malloc_failed; goto error; }
    uint32_t str_count = get_str_count(types, count);
    str_data_write* strlist = 0;
    if (str_count)
    {
        strlist = (str_data_write*)calloc(sizeof(str_data_write) * str_count,sizeof(char));
        if (!strlist) { error_code = malloc_failed; goto error; }
    }
    char* ptr = data;
    SIG_TYPE sig_front = SIG_FRONT;
    SIG_TYPE sig_back = SIG_BACK;
    memcpy(ptr, &sig_front, sizeof(SIG_TYPE));
    ptr += sizeof(SIG_TYPE);
    uint32_t str_idx = 0;
    uint32_t str_off = 0;
    /* offset inside the string block (zero-based) */
    for (uint32_t i = 0; i < count; ++i)
    {
        void* cur = (char*)src + types[i].off;
        uint32_t elem_size = 0;
        switch (types[i].type)
        {
            case type_bool: elem_size = sizeof(bool); break;
            case type_i64:
            case type_u64:
            case type_f64: elem_size = sizeof(uint64_t); break;
            case type_f32:
            case type_i32:
            case type_u32: elem_size = sizeof(uint32_t); break;
            case type_i16:
            case type_u16: elem_size = sizeof(uint16_t); break;
            case type_i8:
            case type_u8: elem_size = sizeof(uint8_t); break;
            case type_str:
            {
                /* reserve 8 bytes in meta: (len<<32) | offset */
                elem_size = sizeof(uint64_t);
                char* pDest = ptr;
                const char* s = *(const char**)cur;
                if (!s) {
                    uint64_t zero = 0;
                    memcpy(pDest, &zero, sizeof(zero));
                } else {
                    /* record where to write the pair later */
                    strlist[str_idx].ptr_in_buf = pDest;
                    strlist[str_idx].src = s;
                    ++str_idx;
                }
                ptr += elem_size;
                continue; /* already handled advancing ptr */
            }
            default:
                error_code = unknown_type;
                goto error;
        }
        memcpy(ptr, cur, elem_size);
        ptr += elem_size;
    }
    /* sig_back */
    memcpy(ptr, &sig_back, sizeof(SIG_TYPE));
    ptr+=sizeof(SIG_TYPE);
    if(ptr != data+size_of_meta+SIG_TOTAL_SIZE)
    {
        error_code = misalign;
        goto error;
    }
    for (uint32_t i = 0; i < str_idx; ++i)
    {
        uint64_t len = (uint64_t)(strlen(strlist[i].src) + 1); /* include NUL */
        uint64_t packed = ((len << 32) | (uint64_t)str_off);
        memcpy(strlist[i].ptr_in_buf, &packed, sizeof(packed));
        str_off += (uint32_t)len;
        memcpy(ptr,strlist[i].src,len);
        ptr += len;
    }
    /* Done filling meta buffer */
    /* Now fill string descriptors (len and offsets) */
    *out = data;
    free(strlist);
    return success;
error:
    *out = 0;
    *out_len = 0;
    if(data) {free(data);}
    if(strlist) {free(strlist);}
    return error_code;
}

/* ----------------- Deserialize implementation ----------------- */

persist_ec_t persist_deserialize_struct
(void* src, persist_field_t* types, uint32_t count,char* buf)
{

}

/* ----------------- Cross Write implementation ----------------- */

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

/* ----------------- Read implementation ----------------- */

persist_ec_t persist_read_struct(void* src, persist_field_t* types, uint32_t count, FILE* fptr)
{
    persist_ec_t error_code = success;
    if (!src || !types || !fptr) { error_code = got_null_pointer; return false; }
    uint32_t size_of_all = get_types_size(types, count);
    size_t meta_bytes = (size_t)size_of_all + SIG_TOTAL_SIZE;
    char* meta = (char*)malloc(meta_bytes);
    if (!meta) { error_code = malloc_failed; return false; }
    /* Read metadata */
    size_t read_meta = fread(meta, 1, meta_bytes, fptr);
    if (read_meta != meta_bytes) {
        error_code = fread_failed;
        free(meta);
        return false;
    }
    char* ptr = meta;
    /* Check front sig */
    SIG_TYPE read_sig_front = 0;
    memcpy(&read_sig_front, ptr, sizeof(SIG_TYPE));
    if (read_sig_front != SIG_FRONT) {
        error_code = front_sig_diff;
        free(meta);
        return false;
    }
    ptr += sizeof(SIG_TYPE);
    /* Prepare to collect string targets */
    uint32_t str_count = get_str_count(types, count);
    str_data_read* strlist = NULL;
    if (str_count) {
        strlist = (str_data_read*)malloc(sizeof(str_data_read) * str_count);
        if (!strlist) { free(meta); error_code = malloc_failed; return false; }
    }
    uint32_t str_idx = 0;
    uint64_t total_strings_bytes = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        void* cur = (char*)src + types[i].off;
        switch (types[i].type)
        {
            case type_bool:
            {
                bool v;
                memcpy(&v, ptr, sizeof(bool));
                memcpy(cur, &v, sizeof(bool));
                ptr += sizeof(bool);
            } break;
            case type_i64:
            case type_u64:
            case type_f64:
            {
                uint64_t v;
                memcpy(&v, ptr, sizeof(uint64_t));
                memcpy(cur, &v, sizeof(uint64_t));
                ptr += sizeof(uint64_t);
            } break;
            case type_f32:
            case type_i32:
            case type_u32:
            {
                uint32_t v;
                memcpy(&v, ptr, sizeof(uint32_t));
                memcpy(cur, &v, sizeof(uint32_t));
                ptr += sizeof(uint32_t);
            } break;
            case type_i16:
            case type_u16:
            {
                uint16_t v;
                memcpy(&v, ptr, sizeof(uint16_t));
                memcpy(cur, &v, sizeof(uint16_t));
                ptr += sizeof(uint16_t);
            } break;
            case type_i8:
            case type_u8:
            {
                uint8_t v;
                memcpy(&v, ptr, sizeof(uint8_t));
                memcpy(cur, &v, sizeof(uint8_t));
                ptr += sizeof(uint8_t);
            } break;
            case type_str:
            {
                uint64_t packed = 0;
                memcpy(&packed, ptr, sizeof(uint64_t));
                ptr += sizeof(uint64_t);
                if (packed == 0) {
                    /* NULL pointer */
                    *(char**)cur = NULL;
                } else {
                    uint32_t len = (uint32_t)(packed >> 32);
                    /* len includes NUL, since writer stored strlen+1 */
                    /* store destination pointer location and len to fill later */
                    strlist[str_idx].len = len;
                    strlist[str_idx].dst = (char**)cur;
                    total_strings_bytes += len;
                    ++str_idx;
                }
            } break;
            default:
                free(meta);
                free(strlist);
                error_code = unknown_type;
                return false;
        }
    }
    /* Check back sig */
    SIG_TYPE read_sig_back = 0;
    memcpy(&read_sig_back, ptr, sizeof(SIG_TYPE));
    if (read_sig_back != SIG_BACK) {
        error_code = back_sig_diff;
        free(meta);
        free(strlist);
        return false;
    }
    /* read string block (if any) */
    free(meta);
    if (total_strings_bytes > 0)
    {
        char* sblock = (char*)malloc((size_t)total_strings_bytes);
        if (!sblock) { free(strlist); error_code = malloc_failed; return false; }
        size_t read_s = fread(sblock, 1, (size_t)total_strings_bytes, fptr);
        if (read_s != (size_t)total_strings_bytes) {
            free(sblock);
            free(strlist);
            error_code = fread_failed;
            return false;
        }
        /* Fill the pointers in order (they were written in the same order) */
        uint32_t offset = 0;
        for (uint32_t i = 0; i < str_idx; ++i)
        {
            /* Duplicate string into heap for caller */
            char* dup = strdup(sblock + offset);
            if (!dup) {
                /* If strdup fails, free previous allocations and abort */
                for (uint32_t j = 0; j < i; ++j) {
                    free(*(strlist[j].dst)); /* those were strdup'ed */
                }
                free(sblock);
                free(strlist);
                error_code = malloc_failed;
                return false;
            }
            *(strlist[i].dst) = dup;
            offset += strlist[i].len; /* len includes NUL */
        }
        free(sblock);
    }
    free(strlist);
    error_code = success;
    return true;
}

/* ----------------- Error reporting ----------------- */

void persist_error_write(persist_ec_t error_code)
{
    switch (error_code)
    {
        case success: return;
        case got_null_pointer:
            fprintf(stderr, "Persist error: got null pointer as a parameter!\n");
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
        case unknown_type:
            fprintf(stderr, "Persist error: unknown type encountered!\n");
            return;
        default:
            fprintf(stderr, "Persist error: unknown error code: %d\n", (int)error_code);
            return;
    }
}

#endif /* PERSIST_IMPLEMENTATION */

