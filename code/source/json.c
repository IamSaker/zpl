// file: source/json.c

////////////////////////////////////////////////////////////////
//
// JSON5 Parser
//
//

#ifdef ZPL_EDITOR
#include <zpl.h>
#endif

#include <math.h> /* needed for INFINITY and NAN */

ZPL_BEGIN_C_DECLS

char *zpl__json_parse_object(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code);
char *zpl__json_parse_array(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code);
char *zpl__json_parse_value(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code);
char *zpl__json_parse_number(zpl_json_object *obj, char *base);
void zpl__json_write_value(zpl_file *f, zpl_json_object *o, zpl_json_object *t, zpl_isize indent, zpl_b32 is_inline, zpl_b32 is_last);
#define zpl___ind(x) for (int i = 0; i < x; ++i) zpl_fprintf(f, " ");

void zpl_json_parse(zpl_json_object *root, zpl_usize len, char *source, zpl_allocator a, zpl_b32 handle_comments,
                    zpl_u8 *err_code) {

    if (!root || !source)
    {
        ZPL_JSON_ASSERT;
        if (err_code) *err_code = ZPL_JSON_ERROR_OBJECT_OR_SOURCE_WAS_NULL;
        return;
    }

    zpl_unused(len);

    char *dest = (char *)source;

    if (handle_comments) {
        char *p = dest;
        do {
            if (!!zpl_strchr("\"'`", *p)) {
                char c = *p;
                char *e = p;
                do {
                    ++e;
                    e = zpl_str_skip(e, c);
                }
                while (*(e-1) == '\\');
                p += (e-p)+1;
            }
            else if (!zpl_strncmp(p, "//", 2)) {
                const char *e = zpl_str_skip(p, '\n');
                zpl_memset(p, ' ', (e-p));
                p += (e-p)+1;
            }
            else if (!zpl_strncmp(p, "/*", 2)) {
                const char *e = zpl_str_skip(p+2, '*');
                if (*e && *(e+1) == '/') {
                    e+=2; /* advance past end comment block */
                    zpl_memset(p, ' ', (e-p));
                    p += (e-p)+1;
                }
                else ++p;
            }
            else ++p;
        } while (*p);
    }

    if (err_code) *err_code = ZPL_JSON_ERROR_NONE;
    zpl_json_object root_ = { 0 };

    dest = zpl_str_trim(dest, false);

    zpl_b32 starts_with_bracket = false;

    if (*dest != '{' && *dest != '[') { root_.cfg_mode = true; }
    if (*dest == '[') { starts_with_bracket = true; }

    char *endp = NULL;

    if (!starts_with_bracket)
        endp = zpl__json_parse_object(&root_, dest, a, err_code);
    else
        endp = zpl__json_parse_array(&root_, (dest+1), a, err_code);

    if (!root_.cfg_mode && endp == NULL) {
        if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
    }

    // Replace root node with its child if the JSON document is an array.
    if (starts_with_bracket && root_.type != ZPL_JSON_TYPE_ARRAY) {
        zpl_json_object *replace = &root_;
        *replace = root_.nodes[0];
    }

    *root = root_;
}

void zpl_json_write(zpl_file *f, zpl_json_object *o, zpl_isize indent) {
    if (!o)
        return;

    zpl___ind(indent - 4);
    if (!o->cfg_mode)
        zpl_fprintf(f, "{\n");
    else {
        indent -= 4;
    }

    if (o->nodes)
    {
        zpl_isize cnt = zpl_array_count(o->nodes);

        for (int i = 0; i < cnt; ++i) {
            zpl__json_write_value(f, o->nodes + i, o, indent, false, !(i < cnt - 1));
        }
    }

    zpl___ind(indent);

    if (indent > 0) {
        zpl_fprintf(f, "}");
    } else {
        if (!o->cfg_mode) zpl_fprintf(f, "}\n");
    }
}

zpl_string zpl_json_write_string(zpl_allocator a, zpl_json_object *obj, zpl_isize indent) {
    zpl_file tmp;
    zpl_file_stream_new(&tmp, a);
    zpl_json_write(&tmp, obj, indent);
    zpl_isize fsize;
    zpl_u8* buf = zpl_file_stream_buf(&tmp, &fsize);
    zpl_string output = zpl_string_make_length(a, (char *)buf, fsize+1);
    zpl_file_close(&tmp);
    return output;
}

void zpl_json_free(zpl_json_object *obj) {
    if ((obj->type == ZPL_JSON_TYPE_OBJECT || obj->type == ZPL_JSON_TYPE_ARRAY) && obj->nodes) {
        for (zpl_isize i = 0; i < zpl_array_count(obj->nodes); ++i) { zpl_json_free(obj->nodes + i); }

        zpl_array_free(obj->nodes);
    }
}

zpl_json_object *zpl_json_find(zpl_json_object *obj, char const *name, zpl_b32 deep_search)
{
    if (obj->type != ZPL_JSON_TYPE_OBJECT) {
        return NULL;
    }

    for (zpl_isize i = 0; i < zpl_array_count(obj->nodes); i++) {
        if (!zpl_strcmp(obj->nodes[i].name, name)) {
            return (obj->nodes + i);
        }
    }

    if (deep_search) {
        for (zpl_isize i = 0; i < zpl_array_count(obj->nodes); i++) {
            zpl_json_object *res = zpl_json_find(obj->nodes + i, name, deep_search);

            if (res != NULL)
                return res;
        }
    }

    return NULL;
}

void zpl_json_init_node(zpl_json_object *obj, zpl_allocator backing, char const *name, zpl_u8 type)
{
    zpl_zero_item(obj);
    obj->name = (char *)name;
    obj->type = type;
    obj->backing = backing;

    if (type == ZPL_JSON_TYPE_ARRAY || type == ZPL_JSON_TYPE_OBJECT) {
        zpl_array_init(obj->nodes, backing);
    }
}

zpl_json_object *zpl_json_add_at(zpl_json_object *obj, zpl_isize index, char const *name, zpl_u8 type)
{
    if (!obj || (obj->type != ZPL_JSON_TYPE_OBJECT && obj->type != ZPL_JSON_TYPE_ARRAY)) {
        return NULL;
    }

    if (!obj->nodes)
        return NULL;

    if (index < 0 || index > zpl_array_count(obj->nodes))
        return NULL;

    zpl_json_object o = {0};
    zpl_json_init_node(&o, obj->backing, name, type);
    zpl_array_append_at(obj->nodes, o, index);

    return obj->nodes + index;
}

zpl_json_object *zpl_json_add(zpl_json_object *obj, char const *name, zpl_u8 type)
{
    if (!obj || (obj->type != ZPL_JSON_TYPE_OBJECT && obj->type != ZPL_JSON_TYPE_ARRAY)) {
        return NULL;
    }

    if (!obj->nodes)
        return NULL;

    return zpl_json_add_at(obj, zpl_array_count(obj->nodes), name, type);
}

void zpl_json_set_obj(zpl_json_object *obj, char const *name, zpl_allocator backing) {
    zpl_json_init_node(obj, backing, name, ZPL_JSON_TYPE_OBJECT);
}
void zpl_json_set_arr(zpl_json_object *obj, char const *name, zpl_allocator backing) {
    zpl_json_init_node(obj, backing, name, ZPL_JSON_TYPE_ARRAY);
}
void zpl_json_set_str(zpl_json_object *obj, char const *name, char const *value) {
    obj->type = ZPL_JSON_TYPE_STRING;
    obj->string = value;
}
void zpl_json_set_flt(zpl_json_object *obj, char const *name, zpl_f64 value) {
    obj->type = ZPL_JSON_TYPE_REAL;
    obj->real = value;
}
void zpl_json_set_int(zpl_json_object *obj, char const *name, zpl_i64 value) {
    obj->type = ZPL_JSON_TYPE_INTEGER;
    obj->integer = value;
}

zpl_json_object *zpl_json_inset_obj(zpl_json_object *parent, char const *name) {
    zpl_json_object *o = zpl_json_add(parent, name, ZPL_JSON_TYPE_OBJECT);
    return o;
}
zpl_json_object *zpl_json_inset_arr(zpl_json_object *parent, char const *name) {
    zpl_json_object *o = zpl_json_add(parent, name, ZPL_JSON_TYPE_ARRAY);
    return o;
}
zpl_json_object *zpl_json_inset_str(zpl_json_object *parent, char const *name, char const *value) {
    zpl_json_object *o = zpl_json_add(parent, name, ZPL_JSON_TYPE_STRING);
    o->string = value;
    return o;
}
zpl_json_object *zpl_json_inset_flt(zpl_json_object *parent, char const *name, zpl_f64 value) {
    zpl_json_object *o = zpl_json_add(parent, name, ZPL_JSON_TYPE_REAL);
    o->real = value;
    return o;
}
zpl_json_object *zpl_json_inset_int(zpl_json_object *parent, char const *name, zpl_i64 value) {
    zpl_json_object *o = zpl_json_add(parent, name, ZPL_JSON_TYPE_INTEGER);
    o->integer = value;
    return o;
}

void zpl_json_str_to_flt(zpl_json_object *obj) {
    ZPL_ASSERT(obj);

    if (obj->type == ZPL_JSON_TYPE_REAL) return; /* this is already converted/parsed */
    ZPL_ASSERT(obj->type == ZPL_JSON_TYPE_STRING || obj->type == ZPL_JSON_TYPE_MULTISTRING);
    zpl__json_parse_number(obj, (char *)obj->string);
}

/* private */

static ZPL_ALWAYS_INLINE zpl_b32 zpl__json_is_assign_char(char c) { return !!zpl_strchr(":=|", c); }
static ZPL_ALWAYS_INLINE zpl_b32 zpl__json_is_delim_char(char c) { return !!zpl_strchr(",|\n", c); }

#define jx(x) !zpl_char_is_hex_digit(str[x])
ZPL_ALWAYS_INLINE zpl_b32 zpl__json_validate_name(char *str, char *err) {
    while (*str) {
        if ((str[0] == '\\' && !zpl_char_is_control(str[1])) &&
            (str[0] == '\\' && jx(1) && jx(2) && jx(3) && jx(4))) {
            if (err) *err = *str;
            return false;
        }

        ++str;
    }

    return true;
}
#undef jx

void zpl__json_write_value(zpl_file *f, zpl_json_object *o, zpl_json_object *t, zpl_isize indent, zpl_b32 is_inline, zpl_b32 is_last) {
    zpl_json_object *node = o;
    indent += 4;

    if (!is_inline) {
        zpl___ind(indent);
        switch (node->name_style) {
            case ZPL_JSON_NAME_STYLE_DOUBLE_QUOTE: {
                zpl_fprintf(f, "\"%s\"", node->name);
            } break;

            case ZPL_JSON_NAME_STYLE_SINGLE_QUOTE: {
                zpl_fprintf(f, "\'%s\'", node->name);
            } break;

            case ZPL_JSON_NAME_STYLE_NO_QUOTES: {
                zpl_fprintf(f, "%s", node->name);
            } break;
        }

        if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_COLON)
            zpl_fprintf(f, ": ");
        else {
            zpl___ind(zpl_max(o->assign_line_width, 1));

            if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_EQUALS)
                zpl_fprintf(f, "= ");
            else if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_LINE) {
                zpl_fprintf(f, "| ");
            }
        }

    }

    switch (node->type) {
        case ZPL_JSON_TYPE_STRING: {
            zpl_fprintf(f, "\"%s\"", node->string);
        } break;

        case ZPL_JSON_TYPE_MULTISTRING: {
            zpl_fprintf(f, "`%s`", node->string);
        } break;

        case ZPL_JSON_TYPE_ARRAY: {
            zpl_fprintf(f, "[");
            zpl_isize elemn = zpl_array_count(node->nodes);
            for (int j = 0; j < elemn; ++j) {
                if ((node->nodes + j)->type == ZPL_JSON_TYPE_OBJECT || (node->nodes + j)->type == ZPL_JSON_TYPE_ARRAY) {
                    zpl__json_write_value(f, node->nodes + j, o, 0, true, true);
                } else {
                    zpl__json_write_value(f, node->nodes + j, o, -4, true, true);
                }

                if (j < elemn - 1) { zpl_fprintf(f, ", "); }
            }
            zpl_fprintf(f, "]");
        } break;

        case ZPL_JSON_TYPE_INTEGER: {
            if (node->props == ZPL_JSON_PROPS_IS_HEX) {
                zpl_fprintf(f, "0x%llx", (long long)node->integer);
            } else {
                zpl_fprintf(f, "%lld", (long long)node->integer);
            }
        } break;

        case ZPL_JSON_TYPE_REAL: {
            if (node->props == ZPL_JSON_PROPS_NAN) {
                zpl_fprintf(f, "NaN");
            } else if (node->props == ZPL_JSON_PROPS_NAN_NEG) {
                zpl_fprintf(f, "-NaN");
            } else if (node->props == ZPL_JSON_PROPS_INFINITY) {
                zpl_fprintf(f, "Infinity");
            } else if (node->props == ZPL_JSON_PROPS_INFINITY_NEG) {
                zpl_fprintf(f, "-Infinity");
            } else if (node->props == ZPL_JSON_PROPS_IS_EXP) {
                zpl_fprintf(f, "%lld.%0*d%llde%c%lld", (long long)node->base, node->base2_offset, 0, (long long)node->base2, node->exp_neg ? '-' : '+',
                            (long long)node->exp);
            } else if (node->props == ZPL_JSON_PROPS_IS_PARSED_REAL) {
                if (!node->lead_digit)
                    zpl_fprintf(f, ".%0*d%lld", node->base2_offset, 0, (long long)node->base2);
                else
                    zpl_fprintf(f, "%lld.%0*d%lld", (long long int)node->base2_offset, 0, (int)node->base, (long long)node->base2);
            } else {
                zpl_fprintf(f, "%f", node->real);
            }
        } break;

        case ZPL_JSON_TYPE_OBJECT: {
            zpl_json_write(f, node, indent);
        } break;

        case ZPL_JSON_TYPE_CONSTANT: {
            if (node->constant == ZPL_JSON_CONST_TRUE) {
                zpl_fprintf(f, "true");
            } else if (node->constant == ZPL_JSON_CONST_FALSE) {
                zpl_fprintf(f, "false");
            } else if (node->constant == ZPL_JSON_CONST_NULL) {
                zpl_fprintf(f, "null");
            }
        } break;
    }

    if (!is_inline) {

        if (o->delim_style != ZPL_JSON_DELIM_STYLE_COMMA)
        {
            if (o->delim_style == ZPL_JSON_DELIM_STYLE_NEWLINE)
                zpl_fprintf(f, "\n");
            else if (o->delim_style == ZPL_JSON_DELIM_STYLE_LINE)
            {
                zpl___ind(o->delim_line_width);
                zpl_fprintf(f, "|\n");
            }
        }
        else
        {
            if (!is_last) {
                zpl_fprintf(f, ",\n");
            } else {
                zpl_fprintf(f, "\n");
            }
        }
    }
}

char *zpl__json_parse_array(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;

    obj->type = ZPL_JSON_TYPE_ARRAY;
    zpl_array_init(obj->nodes, a);
    obj->backing = a;

    while (*p) {
        p = zpl_str_trim(p, false);

        if (p && *p == ']') {
            return p;
        }

        zpl_json_object elem = { 0 };
        elem.backing = a;
        p = zpl__json_parse_value(&elem, p, a, err_code);

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        zpl_array_append(obj->nodes, elem);

        p = zpl_str_trim(p, false);

        if (*p == ',') {
            ++p;
            continue;
        } else {
            if (*p != ']') {
                if (err_code) { *err_code = ZPL_JSON_ERROR_INVALID_VALUE; }
            }
            return p;
        }
    }

    if (err_code) { *err_code = ZPL_JSON_ERROR_INVALID_VALUE; }
    return p;
}

char *zpl__json_parse_number(zpl_json_object *obj, char* base) {
    ZPL_ASSERT(obj && base);
    char *p = base;
    char *b = base;
    char *e = base;

    obj->type = ZPL_JSON_TYPE_INTEGER;

    b = p;
    e = b;

    zpl_isize ib = 0;
    char buf[48] = { 0 };

    if (*e == '+')
        ++e;
    else if (*e == '-') {
        buf[ib++] = *e++;
    }

    if (*e == '.') {
        obj->type = ZPL_JSON_TYPE_REAL;
        obj->props = ZPL_JSON_PROPS_IS_PARSED_REAL;
        buf[ib++] = '0';
        obj->lead_digit = false;

        do {
            buf[ib++] = *e;
        } while (zpl_char_is_digit(*++e));
    } else {
        if (!zpl_strncmp(e, "0x", 2) || !zpl_strncmp(e, "0X", 2)) { obj->props = ZPL_JSON_PROPS_IS_HEX; }
        while (zpl_char_is_hex_digit(*e) || zpl_char_to_lower(*e) == 'x') { buf[ib++] = *e++; }

        if (*e == '.') {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->lead_digit = true;
            zpl_u32 step = 0;

            do {
                buf[ib++] = *e;
                ++step;
            } while (zpl_char_is_digit(*++e));

            if (step < 2) { buf[ib++] = '0'; }
        }
    }

    zpl_i32 exp = 0;
    zpl_f32 eb = 10;
    char expbuf[6] = { 0 };
    zpl_isize expi = 0;

    if (zpl_char_to_lower(*e) == 'e') {
        ++e;
        if (*e == '+' || *e == '-' || zpl_char_is_digit(*e)) {
            if (*e == '-') { eb = 0.1f; }
            if (!zpl_char_is_digit(*e)) { ++e; }
            while (zpl_char_is_digit(*e)) { expbuf[expi++] = *e++; }
        }

        exp = (zpl_i32)zpl_str_to_i64(expbuf, NULL, 10);
    }

    if (obj->type == ZPL_JSON_TYPE_INTEGER) {
        obj->integer = zpl_str_to_i64(buf, 0, 0);

        while (exp-- > 0) { obj->integer *= (zpl_i64)eb; }
    } else {
        obj->real = zpl_str_to_f64(buf, 0);

        char *q = buf, *qp = q, *qp2 = q;
        while (*qp != '.') ++qp;
        *qp = '\0';
        qp2 = qp + 1;
        char *qpOff = qp2;
        while (*qpOff++ == '0') obj->base2_offset++;

        obj->base = (zpl_i32)zpl_str_to_i64(q, 0, 0);
        obj->base2 = (zpl_i32)zpl_str_to_i64(qp2, 0, 0);

        if (exp) {
            obj->exp = exp;
            obj->exp_neg = !(eb == 10.f);
            obj->props = ZPL_JSON_PROPS_IS_EXP;
        }

        while (exp-- > 0) { obj->real *= eb; }
    }
    return e;
}

char *zpl__json_parse_value(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;
    char *b = base;
    char *e = base;

    if (!!zpl_strchr("`\"'", *p)) {
        char c = *p;
        obj->type = ZPL_JSON_TYPE_STRING;
        b = p + 1;
        e = p;
        obj->string = b;

        do {
            ++e;
            e = zpl_str_skip(e, c);
        } while (*(e-1) == '\\');

        if (c == '`') obj->type = ZPL_JSON_TYPE_MULTISTRING;

        *e = '\0';
        p = e + 1;
    } else if (zpl_char_is_alpha(*p) || (*p == '-' && !zpl_char_is_digit(*(p + 1)))) {
        obj->type = ZPL_JSON_TYPE_CONSTANT;

        if (!zpl_strncmp(p, "true", 4)) {
            obj->constant = ZPL_JSON_CONST_TRUE;
            p += 4;
        } else if (!zpl_strncmp(p, "false", 5)) {
            obj->constant = ZPL_JSON_CONST_FALSE;
            p += 5;
        } else if (!zpl_strncmp(p, "null", 4)) {
            obj->constant = ZPL_JSON_CONST_NULL;
            p += 4;
        } else if (!zpl_strncmp(p, "Infinity", 8)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = INFINITY;
            obj->props = ZPL_JSON_PROPS_INFINITY;
            p += 8;
        } else if (!zpl_strncmp(p, "-Infinity", 9)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = -INFINITY;
            obj->props = ZPL_JSON_PROPS_INFINITY_NEG;
            p += 9;
        } else if (!zpl_strncmp(p, "NaN", 3)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = NAN;
            obj->props = ZPL_JSON_PROPS_NAN;
            p += 3;
        } else if (!zpl_strncmp(p, "-NaN", 4)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = -NAN;
            obj->props = ZPL_JSON_PROPS_NAN_NEG;
            p += 4;
        } else {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
            return NULL;
        }
    } else if (zpl_char_is_digit(*p) || *p == '+' || *p == '-' || *p == '.') {
        p = zpl__json_parse_number(obj, p);
    } else if (*p == '[') {
        p = zpl_str_trim(p + 1, false);
        obj->type = ZPL_JSON_TYPE_ARRAY;
        if (*p == ']') return (p+1);
        p = zpl__json_parse_array(obj, p, a, err_code);
        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        ++p;
    } else if (*p == '{') {
        p = zpl_str_trim(p + 1, false);
        obj->type = ZPL_JSON_TYPE_OBJECT;
        if (*p == '}') return (p+1);
        p = zpl__json_parse_object(obj, p, a, err_code);
        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        ++p;
    }

    return p;
}

char *zpl__json_parse_object(zpl_json_object *obj, char *base, zpl_allocator a, zpl_u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;
    char *b = base;
    char *e = base;

    zpl_array_init(obj->nodes, a);
    obj->backing = a;

    p = zpl_str_trim(p, false);
    zpl_b32 starts_with_brace = false;
    zpl_b32 starts_with_bracket = false;
    /**/ if (*p == '{') { ++p; starts_with_brace = true; obj->type = ZPL_JSON_TYPE_OBJECT; }
    else if (*p == '[') { ++p; starts_with_bracket = true; obj->type = ZPL_JSON_TYPE_ARRAY; }

    while (*p) {
        zpl_json_object node = { 0 };
        p = zpl_str_trim(p, false);
        if (*p == '}' && starts_with_brace) return p;
        if (*p == ']' && starts_with_bracket) return p;
        if (*p == '\0') return p;

        if (*p == ']' && starts_with_brace)     { if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE; return p; }
        if (*p == '}' && starts_with_bracket)   { if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE; return p; }

        if (*p == '[') {
            starts_with_bracket = true;
            if (node.name) *node.name = '\0';
            p = zpl__json_parse_value(&node, p, a, err_code);
            goto l_parsed;
        }
        else if (*p == '"' || *p == '\'' || zpl_char_is_alpha(*p) || *p == '_' || *p == '$') {
            if (*p == '"' || *p == '\'') {
                if (*p == '"') {
                    node.name_style = ZPL_JSON_NAME_STYLE_DOUBLE_QUOTE;
                } else if (*p == '\'') {
                    node.name_style = ZPL_JSON_NAME_STYLE_SINGLE_QUOTE;
                }

                char c = *p;
                b = ++p;
                e = zpl_str_control_skip(b, c);
                node.name = b;
                *e = '\0';
                ++e;
            }
            else {
                b = p;
                e = b;

                do {
                    ++e;
                } while (*e && (zpl_char_is_alphanumeric(*e) || *e == '_') && !zpl_char_is_space(*e) && !zpl__json_is_assign_char(*e));

                node.name = b;
                node.name_style = ZPL_JSON_NAME_STYLE_NO_QUOTES;
            }

            char *assign_p = e;
            p = zpl_str_trim(e, false);
            node.assign_line_width = cast(zpl_u8)(p-assign_p);

            if (*p && !zpl__json_is_assign_char(*p)) {
                ZPL_JSON_ASSERT;
                if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_NAME;
                return NULL;
            }
            else
            {
                if (*p == '=')
                    node.assign_style = ZPL_JSON_ASSIGN_STYLE_EQUALS;
                else if (*p == '|')
                    node.assign_style = ZPL_JSON_ASSIGN_STYLE_LINE;
                else node.assign_style = ZPL_JSON_ASSIGN_STYLE_COLON;
            }

            if (node.name_style == ZPL_JSON_NAME_STYLE_NO_QUOTES && *e)
                *e = '\0';
        }

        if (node.name && !zpl__json_validate_name(node.name, NULL)) {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_NAME;
            return NULL;
        }

        p = zpl_str_trim(p + 1, false);
        p = zpl__json_parse_value(&node, p, a, err_code);
        node.backing = obj->backing;
        l_parsed:

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        zpl_array_append(obj->nodes, node);

        char *end_p = p;
        p = zpl_str_trim(p, true);

        if (zpl__json_is_delim_char(*p) || *p == '\0') {
            zpl_json_object *n = zpl_array_end(obj->nodes);
            n->delim_style = ZPL_JSON_DELIM_STYLE_COMMA;

            if (*p == '\n')
                n->delim_style = ZPL_JSON_DELIM_STYLE_NEWLINE;
            else if (*p == '|') {
                n->delim_style = ZPL_JSON_DELIM_STYLE_LINE;
                n->delim_line_width = cast(zpl_u8)(p-end_p);
            }
            else if (*p == '\0') {
                return p;
            }
            ++p;
        }

        p = zpl_str_trim(p, false);
        if (*p == '\0' || *p == '}' || *p == ']') {
            if (*p == '\0') {
                return p;
            }
            if (starts_with_brace && *p != '}') {
                if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
                return NULL;
            }

            if (starts_with_bracket && *p != ']') {
                if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
                return NULL;
            }
            return p;
        }
    }
    if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
    return p;
}

#undef zpl___ind

ZPL_END_C_DECLS
