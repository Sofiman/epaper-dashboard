#include "immjson.h"

#define CALL_FN_ARGS(Fn, ...) ((Fn).closure((Fn).user_data, __VA_ARGS__))
#define READ_SLICE() do { \
        slice = (src->read_fn.closure(src->read_fn.user_data)); \
        src->column_at_slice_end += slice.tail - slice.head; \
    } while (0)

#ifndef PRIVATE
#define PRIVATE static
#endif

#define string_buffer_append_slice(Buf, Slice) string_buffer_append((Buf), (Slice)->head, (Slice)->tail - (Slice)->head)

PRIVATE bool string_buffer_append(JsonStringBuffer *buf, const char *str, size_t to_insert_count)
{
    const size_t new_len = buf->len + to_insert_count;
    if (new_len > buf->capacity) {
        const size_t old_capacity = buf->capacity;
        buf->capacity += to_insert_count;
        buf->ptr = CALL_FN_ARGS(buf->alloc_str_fn, buf->ptr, old_capacity, buf->capacity);
        if (!buf->ptr)
            return false;
    }
    json_memcpy(buf->ptr + buf->len, str, to_insert_count);
    buf->len = new_len;
    return true;
}

PRIVATE bool json_read_expected(JsonSource *src, const char *expected) {
    // TODO: What happens if we expect to read `[ab][c]` ([] denotes a chunk) but we read `[ab][d]` ? It should be fine if the first char of `expected` is enough to distinguish two different JSON tokens
    JsonSlice slice = src->remainder;
    do {
        while (slice.head != slice.tail) {
            if (*expected != *slice.head)
                break;
            ++slice.head;

            if (*(++expected) == '\0') {
                src->remainder = slice;
                return true; // found
            }
        }

        READ_SLICE();
    } while (slice.head != slice.tail);

    src->remainder = slice;
    return false;
}

PRIVATE const char *next_string_boundary(const JsonSlice *slice) {
    for (const char *head = slice->head; head != slice->tail; ++head) {
        if (*head == '"' || *head == '\\')
            return head;
    }

    return NULL;
}

PRIVATE bool json_read_string_chunk(JsonSource *src) {
    JsonSlice slice = src->remainder;
    do {
        const char *delimiter = next_string_boundary(&slice);
        if (delimiter != NULL) {
            // Split left the slice
            // "abcd.def"
            //      ^ delimiter
            const JsonSlice remainder = { .head = delimiter + 1, .tail = slice.tail };
            slice.tail = remainder.head;

            if (!string_buffer_append_slice(&src->string_buffer, &slice)) { // Alloc failed
                slice.tail = remainder.tail; // revert back to original read slice
                break;
            }

            src->remainder = remainder;
            return true; // collected chars are in string_buffer
        }

        if (!string_buffer_append_slice(&src->string_buffer, &slice)) { // Alloc failed
            break;
        }
        READ_SLICE();
    } while (slice.head != slice.tail);

    src->remainder = slice;
    return false;
}

PRIVATE uint8_t read_digit_seq(JsonSource *src, JsonUintmax *out) {
    JsonSlice slice = src->remainder;
    JsonUintmaxSize count = 0;
    do {
        for (; slice.head != slice.tail; ++slice.head) {
            const unsigned char digit = (unsigned char)(*slice.head - '0');
            if (digit > 9) {
                src->remainder = slice;
                return count;
            }
            if (__builtin_mul_overflow(*out, 10, out) || __builtin_add_overflow(*out, digit, out))
                goto end;
            ++count;
        }

        READ_SLICE();
    } while (slice.head != slice.tail);

end:
    src->remainder = slice;
    return 0;
}

static inline bool json_is_whitespace(char c) {
    switch (c) {
    case  ' ': /* FALLTROUGH */
    case '\t': /* FALLTROUGH */
    case '\n': /* FALLTROUGH */
    case '\r': return true;
    default:   return false;
    }
}

PRIVATE bool json_trim_left_expect_char(JsonSource *src, char c) {
    JsonSlice slice = src->remainder;
    do {
        for (; slice.head != slice.tail; ++slice.head) {
            const char cur = *slice.head;
            if (!json_is_whitespace(cur)) {
                if (cur == c) {
                    ++slice.head;
                    src->remainder = slice;
                    return true;
                } else {
                    goto end;
                }
            }
            if (cur == '\n') {
                src->line += 1;
                src->column_at_slice_end = slice.tail - slice.head;
            }
        }

        READ_SLICE();
    } while (slice.head != slice.tail);

end:
    src->remainder = slice;
    return false;
}

#define READ_ANY 0
PRIVATE bool json_read_expect_char(JsonSource *src, char expected) {
    JsonSlice slice = src->remainder;
    if (slice.head == slice.tail) {
        READ_SLICE();
        if (slice.head == slice.tail) return false;
    }

    bool ok = (expected == READ_ANY) || (*slice.head == expected);
    slice.head += ok;
    src->remainder = slice;
    return ok;
}

/* Value functions */

bool json_expect_string(JsonSource *src, const char **out) {
    if (!json_trim_left_expect_char(src, '"')) return false;

    src->string_buffer.len = 0;
    while (json_read_string_chunk(src)) {
        // TODO: Maybe SWAR can be useful
        // TODO: Do not accept unescaped control chars
        // TODO: Validate UTF-8 ?
        json_assert(src->string_buffer.len >= 1);

        if (src->string_buffer.ptr[src->string_buffer.len - 1] == '"') {
            src->string_buffer.ptr[src->string_buffer.len - 1] = '\0';
            src->string_buffer.len -= 1;
            if (out) *out = src->string_buffer.ptr;

#ifndef JSON_REUSE_STRING_BUFFER
            src->string_buffer.ptr = NULL;
            src->string_buffer.len = 0;
            src->string_buffer.capacity = 0;
#endif
            return true;
        }

        if (!json_read_expect_char(src, READ_ANY)) return false;
         // remainder SHOULD contain at least one char after json_read_expect_char succeeded
        char cur = src->remainder.head[-1];
        switch (cur) {
        case '"':
        case '\\':
        case '/': /*       */ break;
        case 'b': cur = '\b'; break;
        case 'f': cur = '\f'; break;
        case 'n': cur = '\n'; break;
        case 'r': cur = '\r'; break;
        case 't': cur = '\t'; break;
        case 'u': return false; // TODO: \u4HEX
        default: return false; /* error: invalid escape */
        }

        src->string_buffer.ptr[src->string_buffer.len - 1] = cur;
    }

    return false;
}

bool json_expect_bool(JsonSource *src, bool *out) {
    json_trim_left_expect_char(src, ' '); // only trim left
    if (json_read_expected(src, "true")) {
        if (out) *out = true;
        return true;
    }
    if (json_read_expected(src, "false")) {
        if (out) *out = false;
        return true;
    }
    return false;
}

bool json_expect_null(JsonSource *src) {
    json_trim_left_expect_char(src, ' '); // only trim left
    return json_read_expected(src, "null");
}

bool json_expect_number(JsonSource *src, JsonNumber *out) {
    out->negative = json_trim_left_expect_char(src, '-');
    out->digits = 0;

    if (!json_read_expect_char(src, '0')) { // leading zeros are not allowed
        if (!read_digit_seq(src, &out->digits)) {
            return false;
        }
    }

    if (json_read_expect_char(src, '.')) {
        out->point_pos = read_digit_seq(src, &out->digits);
        if (out->point_pos == 0) {
            return false;
        }
    } else {
        out->point_pos = 0;
    }

    if (json_read_expect_char(src, 'e') || json_read_expect_char(src, 'E')) {
        bool exp_negative;
        if (json_read_expect_char(src, '-')) {
            exp_negative = true;
        } else {
            exp_negative = false;
            json_read_expect_char(src, '+');
        }

        JsonUintmax exponent = 0;
        if (!read_digit_seq(src, &exponent)) {
            return false;
        }

        if (exponent > JSON_EXPECT_NUMBER_EXPONENT_MAX) return false;
        out->exponent = exponent;
        if (exp_negative) out->exponent = -out->exponent;
    } else {
        out->exponent = 0;
    }

    return true;
}

bool json_expect_float(JsonSource *src, float *out) {
    JsonNumber num;
    if (!json_expect_number(src, &num)) return 0;

    float v = num.digits;
    if (num.point_pos > 0) { // not an integer
        const float powers_of_ten[19] = {
            1.0/1e1, 1.0/1e2, 1.0/1e3, 1.0/1e4, 1.0/1e5, 1.0/1e6, 1.0/1e7, 1.0/1e8, 1.0/1e9, 1.0/1e10,
            1.0/1e11, 1.0/1e12, 1.0/1e13, 1.0/1e14, 1.0/1e15, 1.0/1e16, 1.0/1e17, 1.0/1e18, 1.0/1e19
        };
        v *= powers_of_ten[num.point_pos - 1];
    }
    if (num.negative) v = -v;
    // TODO: exponent and exponent range check
    *out = v;
    return true;
}

bool json_expect_integer(JsonSource *src, void *out, bool is_signed, uint8_t bitwidth) {
    enum { JSON_UINTMAX_WIDTH = sizeof(JsonUintmax)*8 };
    json_assert(bitwidth > 0 && bitwidth <= JSON_UINTMAX_WIDTH);
    JsonUintmax max_val = ~((JsonUintmax)0) >> (JSON_UINTMAX_WIDTH - bitwidth + (uint8_t)is_signed);

    JsonNumber num;
    if (!json_expect_number(src, &num)) return false;
    if (num.point_pos != 0 || num.exponent != 0) return false; // TODO: Allow exponents

    if (num.digits > max_val + num.negative) return false;
    if (!is_signed && num.negative) return false;
    if (is_signed) num.digits |= ((JsonUintmax)num.negative) << (bitwidth - 1);

    switch ((bitwidth - 1)/8 + 1) {
        case sizeof( uint8_t): *( uint8_t*)out = ( uint8_t)num.digits; break;
        case sizeof(uint16_t): *(uint16_t*)out = (uint16_t)num.digits; break;
        case sizeof(uint32_t): *(uint32_t*)out = (uint32_t)num.digits; break;
        case sizeof(uint64_t): *(uint64_t*)out = (uint64_t)num.digits; break;
        default: return false; // UNSUPPORTED
    }
    return true;
}

bool json_begin_object(JsonSource *src) {
    return json_trim_left_expect_char(src, '{');
}

bool json_end_object(JsonSource *src) {
    return json_trim_left_expect_char(src, '}');
}

const char *json_next_key(JsonSource *src) {
    const char *key = NULL;
    // TODO: Not strict enough, someone can input "key": "123""otherkey" and still passes this check and the following json_expect_string check
    json_trim_left_expect_char(src, ',');
    if (!json_expect_string(src, &key)) return NULL;
    return json_trim_left_expect_char(src, ':') ? key : NULL;
}

bool json_begin_array(JsonSource *src) {
    return json_trim_left_expect_char(src, '[') && !json_trim_left_expect_char(src, ']');
}

bool json_array_next(JsonSource *src) {
    if (json_trim_left_expect_char(src, ',')) {
        return true;
    }
    /* check last read char by json_trim_left_expect_char */
    if (src->remainder.head != src->remainder.tail && *src->remainder.head == ']') {
        src->remainder.head++;
        return false; /* end of array */
    }
    return false; /* error */
}

bool json_ignore_any(JsonSource *src, size_t max_depth) {
    if (max_depth-- == 0) return false;
    json_trim_left_expect_char(src, ' ');
    if (src->remainder.head == src->remainder.tail) return false;

    const char cur = *src->remainder.head; // peek
    switch (cur) {
    case 't': case 'f': return json_expect_bool(src, NULL);
    case 'n': return json_expect_null(src);
    case '"': return json_expect_string(src, NULL);
    case '{': return json_ignore_object(src, max_depth);
    case '[': return json_ignore_array(src, max_depth);
    default: break;
    }

    if (cur == '-' || (unsigned char)(cur - '0') < 9) {
        JsonNumber ignored;
        return json_expect_number(src, &ignored);
    }

    return false;
}

bool json_skip_to_key(JsonSource *src, size_t max_depth, const char *key) {
    for (;;) {
        // ws string ws ':' ws value ws [',']
        const char *cur_key = json_next_key(src);
        if (cur_key == NULL) return key == NULL;
        if (key != NULL && strcmp(key, cur_key) == 0) return true;

        if (!json_ignore_any(src, max_depth)) break;
    }
    return false;
}

bool json_ignore_object(JsonSource *src, size_t max_depth) {
    if (max_depth-- == 0) return false;
    if (!json_trim_left_expect_char(src, '{')) return false;
    if (json_trim_left_expect_char(src, '}')) return true; // empty
    if (!json_skip_to_key(src, max_depth, NULL)) return false;

    return json_read_expect_char(src, '}');
}

bool json_ignore_array(JsonSource *src, size_t max_depth) {
    if (max_depth-- == 0) return false;
    if (!json_trim_left_expect_char(src, '[')) return false;
    if (json_trim_left_expect_char(src, ']')) return true; // empty

    do {
        if (!json_ignore_any(src, max_depth)) return false;
    } while (json_trim_left_expect_char(src, ','));

    return json_read_expect_char(src, ']');
}

#ifdef DEBUG
#include <stdio.h>

static const char *KIND_LABELS[] = {
    [KIND_CUSTOM] = "<CUSTOM>",
    [KIND_OBJECT] = "OBJECT",
    [KIND_ARRAY] = "ARRAY",
    [KIND_STRING] = "STRING",
    [KIND_DOUBLE] = "DOUBLE",
    [KIND_INTEGER] = "INTEGER",
    [KIND_BOOL] = "BOOL",
};
#define diagf printf
#else
#define diagf(...)
#endif

bool json_deserialize_array(JsonSource *src, void *out, const struct JsonArrayDescription desc) {
    if (!json_trim_left_expect_char(src, '[')) return false;
    if (json_trim_left_expect_char(src, ']')) return true;

    size_t i = 0;
    do {
        void *item_storage = desc.array_reserve_fn(out, i);
        if (item_storage == NULL) return false;
        if (!json_deserialize(src, &item_storage, desc.item_tag, desc.item_val)) {
            diagf("Failed to parse %s at array index %zu\n", KIND_LABELS[desc.item_tag], i);
            return false;
        }
        ++i;
    } while (json_trim_left_expect_char(src, ','));
    return json_read_expect_char(src, ']');
}

bool json_deserialize_object(JsonSource *src, void **out, const JsonObjectProperty *propreties) {
    if (!json_begin_object(src)) return false;
    if (propreties == JSON_INLINE_OBJ_BEGIN) return true;
    size_t i = 0;
    for (;;) {
        const JsonObjectProperty *p = &propreties[i];
        if (p->key == NULL) {
            if (p->padding_bytes == UINTPTR_MAX) break;
            *out = ((uint8_t*)*out) + p->padding_bytes;
        } else {
            const JsonSchemaTag tag = *p->key;
            if (tag == KIND_OBJECT && p->val.obj_desc == JSON_INLINE_OBJ_END) {
                json_skip_to_key(src, 16, NULL);
                if (!json_end_object(src)) return false;
                ++i;
                continue;
            }
            const char *key = json_next_key(src);
            if (key == NULL) return false;
            if (strcmp(key, p->key + JSON_SCHEMA_TAG_BYTES) != 0) {
                if (!json_ignore_any(src, 16)) return false;
                if (!json_read_expect_char(src, ',')) break;
                continue;
            }
            if (!json_deserialize(src, out, tag, p->val)) {
                diagf("Failed to parse %s for key `%s` (i = %zu)\n", KIND_LABELS[tag], key, i);
                return false;
            }
        }
        ++i;
    }
    json_skip_to_key(src, 16, NULL);
    return json_end_object(src);
}

bool json_deserialize(JsonSource *src, void **out, JsonSchemaTag tag, JsonValue val) {
    void *cursor = *out;
    switch (tag) {
    case KIND_CUSTOM:
        return val.custom_deserialize_fn(src, out);
    case KIND_OBJECT:
        return json_deserialize_object(src, out, val.obj_desc);
    case KIND_ARRAY: {
            const JsonArrayDescription desc = val.array_describe(cursor);
            *out = desc.exitpoint;
            return json_deserialize_array(src, cursor, desc);
        } break;
    case KIND_STRING:
        *out = (uint8_t*)cursor + sizeof(const char *);
        return json_expect_string(src, cursor);
    case KIND_DOUBLE:
        *out = (uint8_t*)cursor + sizeof(float);
        return json_expect_float(src, cursor);
    case KIND_INTEGER:
        *out = (uint8_t*)cursor + (val.integer.bitwidth-1)/8+1;
        return json_expect_integer(src, cursor, val.integer.is_signed, val.integer.bitwidth);
    case KIND_BOOL:
        *out = (uint8_t*)cursor + sizeof(bool);
        return json_expect_bool(src, cursor);
    default: break;
    }
    return false;
}
