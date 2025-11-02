#define PERSIST_IMPLEMENTATION
#include "persist.h"
#include <stdio.h>

typedef struct {
    int32_t id;
    double  score;
    char*   name;
    bool    active;
} user_t;

/* Field layout metadata */
persist_field_t user_fields[] = {
    { type_i32, offsetof(user_t, id) },
    { type_f64, offsetof(user_t, score) },
    { type_str, offsetof(user_t, name) },
    { type_bool, offsetof(user_t, active) },
};

const uint32_t user_field_count = sizeof(user_fields) / sizeof(user_fields[0]);

int main(void)
{
    user_t u1 = {
        .id = 42,
        .score = 99.7,
        .name = "Hans",
        .active = true,
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

    return 0;
}
