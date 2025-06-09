#ifndef JSON_H
#define JSON_H

#include <stdint.h>
#include <stdbool.h>

#ifndef json_memcpy
#include <string.h>
#define json_memcpy memcpy
#endif

#ifndef json_strcmp
#include <string.h>
#define json_strcmp strcmp
#endif

#if !json_assert
#include <assert.h>
#define json_assert assert
#endif

typedef struct JsonSlice {
    // String is NOT required to be null-terminated.
    // "abcdef"
    //  ^     ^
    //  head  tail
    const char *head;
    const char *tail;
} JsonSlice;

typedef struct JsonReadFn {
    void *user_data;
    // JsonSlice read_fn(void *user_data);
    JsonSlice (*closure)(void *user_data);
} JsonReadFn;

typedef struct JsonAllocStringFn {
    void *user_data;
    // char *alloc_str_fn(void *user_data, char *oldptr, size_t old_size, size_t new_size);
    char *(*closure)(void *user_data, char *oldptr, size_t old_size, size_t new_size);
} JsonAllocStringFn;

typedef struct JsonStringBuffer {
    char *ptr; // String is NOT required to be null-terminated.
    size_t len;
    size_t capacity;
    JsonAllocStringFn alloc_str_fn;
} JsonStringBuffer;

typedef struct JsonSource {
    JsonReadFn read_fn;

    JsonSlice remainder;
    JsonStringBuffer string_buffer;

    // Source column number at the end of the last read slice. Use json_source_column to get the actual cursor's column
    size_t column_at_slice_end;
#define json_source_column(Src) ((Src)->column_at_slice_end - ((Src)->remainder.tail - (Src)->remainder.head))
    size_t line;
} JsonSource;

#ifndef JSON_INTMAX_TYPE
#define JSON_INTMAX_TYPE uintmax_t
#endif

typedef JSON_INTMAX_TYPE JsonUintmax;
typedef uint8_t JsonUintmaxSize; // used to count JsonUintmax digits in read_digit_seq()

typedef struct JsonNumber {
    // digits = 3141592653589793238L
    //           ^ point_pos = 18
    //       -> 3.141592653589793238
    JsonUintmax digits;
#define JSON_EXPECT_NUMBER_EXPONENT_MAX INT16_MAX // signed
    int16_t exponent;
    uint8_t point_pos;
    bool negative;
} JsonNumber;

bool json_expect_string(JsonSource *src, const char **out);
bool json_expect_bool(JsonSource *src, bool *out);
bool json_expect_null(JsonSource *src);
bool json_expect_number(JsonSource *src, JsonNumber *out);
bool json_expect_float(JsonSource *src, float *out);
bool json_expect_integer(JsonSource *src, void *out, bool is_signed, uint8_t bitwidth);

static inline bool json_expect_char(JsonSource *src, char *out) { return json_expect_integer(src, out, true, 8 * sizeof(char)); }
static inline bool json_expect_uchar(JsonSource *src, unsigned char *out) { return json_expect_integer(src, out, false, 8 * sizeof(unsigned char)); }
static inline bool json_expect_short(JsonSource *src, short *out) { return json_expect_integer(src, out, true, 8 * sizeof(short)); }
static inline bool json_expect_ushort(JsonSource *src, unsigned short *out) { return json_expect_integer(src, out, false, 8 * sizeof(unsigned short)); }
static inline bool json_expect_int(JsonSource *src, int *out) { return json_expect_integer(src, out, true, 8 * sizeof(int)); }
static inline bool json_expect_uint(JsonSource *src, unsigned int *out) { return json_expect_integer(src, out, false, 8 * sizeof(unsigned int)); }
static inline bool json_expect_long(JsonSource *src, long *out) { return json_expect_integer(src, out, true, 8 * sizeof(long)); }
static inline bool json_expect_ulong(JsonSource *src, unsigned long *out) { return json_expect_integer(src, out, false, 8 * sizeof(unsigned long)); }

#if __STDC_VERSION__ >= 201112L
#define json_expect(Src, Storage) _Generic((Storage), \
                const char **: json_expect_string, \
              unsigned char *: json_expect_uchar, \
                       char *: json_expect_char, \
             unsigned short *: json_expect_ushort, \
                      short *: json_expect_short, \
               unsigned int *: json_expect_uint, \
                        int *: json_expect_int, \
              unsigned long *: json_expect_ulong, \
                       long *: json_expect_long, \
                     float *: json_expect_float, \
                       bool *: json_expect_bool \
            )((Src), (Storage))
#endif

bool json_ignore_any(JsonSource *src, size_t max_depth);
bool json_ignore_object(JsonSource *src, size_t max_depth);
bool json_ignore_array(JsonSource *src, size_t max_depth);
bool json_skip_to_key(JsonSource *src, size_t max_depth, const char *key);

bool json_begin_object(JsonSource *src);
bool json_end_object(JsonSource *src);
const char *json_next_key(JsonSource *src);

static inline bool json_expect_key(JsonSource *src, const char *expected_key) {
    const char *key = json_next_key(src);
    return key != NULL && json_strcmp(key, expected_key) == 0;
}

// Usage exemple for `{"nested_obj":{"key":"val"}}`
// ```c
// json_expect_object(&src) {
//     assert(json_expect_key(&src, "nested_obj"));
//     json_expect_object(&src) {
//         assert(json_expect_key(&src, "key"));
//         assert(json_expect_string(&src, NULL));
//     }
// } else {
//     assert(false && "expected an object at the JSON top-level");
// }
#define json_expect_object(Src) if (json_begin_object(Src)) for (int __json_expect_object_guard = 1; __json_expect_object_guard; __json_expect_object_guard = (json_end_object(Src), 0))

bool json_begin_array(JsonSource *src);
bool json_array_next(JsonSource *src);

// SCHEMAS

#if 0
#if __STDC_VERSION__ >= 201112L
#define json_member_as(Type, Member, Key) \
    { .key = { .kind = JSON_SCHEMA_KEY, .key = (Key) } }, \
    { .value = { .kind = _Generic((((Type *)NULL)->Member), \
        const char *: JSON_SCHEMA_STRING, \
             float: JSON_SCHEMA_DOUBLE, \
               bool: JSON_SCHEMA_BOOL \
    ), .offset = offsetof(Type, Member) } }
#endif

#define json_schema_begin_object() { .object = { .kind = JSON_SCHEMA_OBJECT, .begin = true } }
#define json_schema_end_object() { .object = { .kind = JSON_SCHEMA_OBJECT, .begin = false } }
#endif

#define JSON_SCHEMA_TAG_BYTES 1
typedef enum {
    KIND_CUSTOM = 1,
#define JSON_SCHEMA_CUSTOM  "\x1"

    KIND_OBJECT = 2,
#define JSON_SCHEMA_OBJECT  "\x2"

    KIND_ARRAY = 3,
#define JSON_SCHEMA_ARRAY   "\x3"

    KIND_STRING = 4,
#define JSON_SCHEMA_STRING  "\x4"

    KIND_DOUBLE = 5,
#define JSON_SCHEMA_DOUBLE  "\x5"

    KIND_INTEGER = 6,
#define JSON_SCHEMA_INTEGER "\x6"

    KIND_BOOL = 7,
#define JSON_SCHEMA_BOOL    "\x7"
} JsonSchemaTag;

typedef struct JsonArrayDescription JsonArrayDescription;
typedef union JsonObjectProperty JsonObjectProperty;

typedef union JsonValue {
    // Custom
    bool (*custom_deserialize_fn)(JsonSource *src, void **out);

    // Object
#define JSON_INLINE_OBJ_BEGIN ((const JsonObjectProperty *)UINTPTR_MAX)
#define JSON_INLINE_OBJ_END ((const JsonObjectProperty *)NULL)
    const JsonObjectProperty *obj_desc;

    // Array
    JsonArrayDescription (*array_describe)(void *cursor);

    struct {
        bool is_signed;
        uint8_t bitwidth;
    } integer;
} JsonValue;

typedef union JsonObjectProperty {
    struct {
        // Key-val pair
        const char *key;
        JsonValue val;
    };
    struct {
        // Padding bytes. This variant is valid when key is NULL (closely mirrored by this placeholder zero array).
        char zero[sizeof(const char*)];
        size_t padding_bytes;
    };
} JsonObjectProperty;
static_assert(sizeof(JsonObjectProperty) <= 2*sizeof(void*));
#define OBJECT_PROPERTIES_END() ((JsonObjectProperty){ .padding_bytes = UINTPTR_MAX })

typedef struct JsonObjectDescription {
    const JsonObjectProperty *properties;
    size_t property_count;
} JsonObjectDescription;

typedef struct JsonArrayDescription {
    void *exitpoint;
    void *(*array_reserve_fn)(void *cursor, size_t i);

    JsonSchemaTag item_tag;
    JsonValue item_val;
} JsonArrayDescription;

bool json_deserialize_array(JsonSource *src, void *out, JsonArrayDescription description);
bool json_deserialize_object(JsonSource *src, void **out, const JsonObjectProperty *propreties);
bool json_deserialize(JsonSource *src, void **out, JsonSchemaTag tag, JsonValue val);

#endif /* !JSON_H */
