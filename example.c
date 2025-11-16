#define PERSIST_IMPLEMENTATION
#include "persist.h"
#include <stdio.h>

typedef struct {
    int32_t id;
    double  score;
    char*   name;
    char**   arra;
    count_t   arra_c;
    bool    active;
    int32_t* scores;
    count_t scores_count;
} user_t;

/* Field layout metadata */
persist_field_t user_fields[] = {
    STRUCT_FIELD(user_t,type_i32,id),
    STRUCT_FIELD(user_t,type_f64,score),
    STRUCT_FIELD(user_t,type_str,name),
    STRUCT_FIELD(user_t,type_bool,active),
    STRUCT_FIELD_ARR(user_t,type_i32,scores,scores_count),
    STRUCT_FIELD_ARR(user_t,type_str,arra,arra_c),
};

const uint32_t user_field_count = sizeof(user_fields) / sizeof(user_fields[0]);

int main(void)
{
    uint32_t arr[] = {1,2,3,4};
    char* arra[] = {"a","b",""};
    user_t u1 = {
        .id = 42,
        .score = 99.7,
        .name = "Hans",
        .arra = arra,
        .arra_c = sizeof(arra)/sizeof(*arra),
        .active = true,
        .scores = arr,
        .scores_count = sizeof(arr)/sizeof(*arr),
    };

    /* --- Write struct to file --- */
    FILE* f = fopen("user.dat", "wb");
    if (!f) {
        perror("fopen for write");
        return 1;
    }
    persist_ec_t ec = persist_write_struct(&u1, user_fields, user_field_count, f);
    fclose(f);
    //Alternative
    // if(ec) {persist_error_write(ec);return 1;}
    if (ec != success) {
        persist_error_write(ec);
        return 1;
    }
    /* --- Read struct back --- */
    user_t u2 = {0};
    FILE* fr = fopen("user.dat", "rb");
    if (!fr) {
        perror("fopen for read");
        return 1;
    }

    ec = persist_read_struct(&u2, user_fields, user_field_count, fr);
    fclose(fr);

    // if(ec) {persist_error_write(ec);return 1;}
    if (ec != success) {
        persist_error_write(ec);
        return 1;
    }

    /* --- Print result --- */
    printf("Loaded user:\n");
    printf("  id: %d\n", u2.id);
    printf("  score: %.2f\n", u2.score);
    printf("  name: %s\n", u2.name);
    printf("  active: %s\n", u2.active ? "true" : "false");

    /* Free dynamically allocated string from deserialization */
    free(u2.name);
    free(u2.scores);
    for(uint32_t c = 0;c < u2.arra_c;++c)
    {free(u2.arra[c]);}
    free(u2.arra);
    return 0;
}
