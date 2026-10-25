#include "as_vars.h"

#include <stdlib.h>
#include <string.h>

#define AS_NVARS 962

typedef struct Array {
    int used;
    int name;
    int type;
    int ndims;
    int dim[8];
    size_t count;
    double *num;
    int16_t *in;
    AsStr *str;
} Array;

struct AsVars {
    double flt[AS_NVARS];
    int16_t in[AS_NVARS];
    AsStr str[AS_NVARS];
    Array *arr;
    size_t narr, acap;
};

int as_name_index(int c1, int c2) {
    int second;
    if (c1 < 'A' || c1 > 'Z') return -1;
    if (c2 == 0) second = 0;
    else if (c2 >= 'A' && c2 <= 'Z') second = 1 + (c2 - 'A');
    else if (c2 >= '0' && c2 <= '9') second = 27 + (c2 - '0');
    else return -1;
    return (c1 - 'A') * 37 + second;
}

AsVars *as_vars_new(void) {
    return (AsVars *)as_xmalloc(sizeof(AsVars));
}

static void array_clear(Array *a) {
    size_t i;
    if (a->str) {
        for (i = 0; i < a->count; i++) as_str_clear(&a->str[i]);
        free(a->str);
    }
    free(a->num);
    free(a->in);
    memset(a, 0, sizeof *a);
}

void as_vars_clear(AsVars *v) {
    size_t i;
    for (i = 0; i < AS_NVARS; i++) as_str_clear(&v->str[i]);
    memset(v->flt, 0, sizeof v->flt);
    memset(v->in, 0, sizeof v->in);
    for (i = 0; i < v->narr; i++) array_clear(&v->arr[i]);
    v->narr = 0;
}

void as_vars_free(AsVars *v) {
    if (!v) return;
    as_vars_clear(v);
    free(v->arr);
    free(v);
}

static int num_of(const AsValue *val, double *out) {
    if (val->type != AS_NUM) return AS_ERR_TYPE_MISMATCH;
    *out = val->num;
    return AS_OK;
}

int as_vars_get_scalar(AsVars *v, int type, int name, double *out) {
    if (name < 0 || name >= AS_NVARS) return AS_ERR_SYNTAX;
    if (type == AS_T_INT) *out = v->in[name];
    else if (type == AS_T_FLOAT) *out = v->flt[name];
    else return AS_ERR_TYPE_MISMATCH;
    return AS_OK;
}

int as_vars_set_scalar(AsVars *v, int type, int name, double value) {
    if (name < 0 || name >= AS_NVARS) return AS_ERR_SYNTAX;
    if (type == AS_T_INT) {
        if (value < -32767 || value > 32767) return AS_ERR_ILLEGAL_QUANTITY;
        v->in[name] = (int16_t)value;
        return AS_OK;
    }
    if (type == AS_T_FLOAT) {
        v->flt[name] = value;
        return AS_OK;
    }
    return AS_ERR_TYPE_MISMATCH;
}

static Array *find_array(AsVars *v, int name, int type) {
    size_t i;
    for (i = 0; i < v->narr; i++) {
        if (v->arr[i].used && v->arr[i].name == name && v->arr[i].type == type) return &v->arr[i];
    }
    return NULL;
}

static int alloc_array(AsVars *v, int name, int type, const int *dim, int ndims, Array **out) {
    Array *a;
    size_t count = 1;
    int i;
    if (ndims < 1 || ndims > 8) return AS_ERR_BAD_SUBSCRIPT;
    for (i = 0; i < ndims; i++) {
        if (dim[i] < 1) return AS_ERR_BAD_SUBSCRIPT;
        if (count > 100000u / (size_t)dim[i]) return AS_ERR_OUT_OF_MEMORY;
        count *= (size_t)dim[i];
    }
    if (v->narr == v->acap) {
        size_t ncap = v->acap ? v->acap * 2 : 8;
        v->arr = (Array *)as_xrealloc(v->arr, ncap * sizeof(Array));
        memset(v->arr + v->acap, 0, (ncap - v->acap) * sizeof(Array));
        v->acap = ncap;
    }
    a = &v->arr[v->narr++];
    memset(a, 0, sizeof *a);
    a->used = 1;
    a->name = name;
    a->type = type;
    a->ndims = ndims;
    a->count = count;
    for (i = 0; i < ndims; i++) a->dim[i] = dim[i];
    if (type == AS_T_FLOAT) a->num = (double *)as_xmalloc(count * sizeof(double));
    else if (type == AS_T_INT) a->in = (int16_t *)as_xmalloc(count * sizeof(int16_t));
    else a->str = (AsStr *)as_xmalloc(count * sizeof(AsStr));
    *out = a;
    return AS_OK;
}

int as_vars_dim(AsVars *v, int name, int type, const int *max_index, int ndims) {
    int dim[8];
    int i;
    Array *existing;
    if (ndims < 1 || ndims > 8) return AS_ERR_BAD_SUBSCRIPT;
    existing = find_array(v, name, type);
    if (existing) return AS_ERR_REDIMD_ARRAY;
    for (i = 0; i < ndims; i++) {
        if (max_index[i] < 0) return AS_ERR_BAD_SUBSCRIPT;
        dim[i] = max_index[i] + 1;
    }
    return alloc_array(v, name, type, dim, ndims, &existing);
}

static int index_of(Array *a, const int *subs, size_t *off) {
    int i;
    size_t o = 0;
    size_t stride = 1;
    if (!a) return AS_ERR_BAD_SUBSCRIPT;
    for (i = a->ndims - 1; i >= 0; i--) {
        if (subs[i] < 0 || subs[i] >= a->dim[i]) return AS_ERR_BAD_SUBSCRIPT;
        o += (size_t)subs[i] * stride;
        stride *= (size_t)a->dim[i];
    }
    *off = o;
    return AS_OK;
}

static int ensure_auto(AsVars *v, const AsRef *ref, Array **out) {
    int dim[8];
    int i;
    Array *a = find_array(v, ref->name, ref->type);
    if (a) {
        if (a->ndims != ref->ndims) return AS_ERR_BAD_SUBSCRIPT;
        *out = a;
        return AS_OK;
    }
    for (i = 0; i < ref->ndims; i++) dim[i] = 11;
    return alloc_array(v, ref->name, ref->type, dim, ref->ndims, out);
}

int as_vars_load(AsVars *v, const AsRef *ref, AsValue *out) {
    memset(out, 0, sizeof *out);
    if (ref->name < 0 || ref->name >= AS_NVARS) return AS_ERR_SYNTAX;
    if (!ref->is_array) {
        if (ref->type == AS_T_STR) {
            out->type = AS_STR;
            return as_str_set(&out->str, v->str[ref->name].data, v->str[ref->name].len);
        }
        out->type = AS_NUM;
        return as_vars_get_scalar(v, ref->type, ref->name, &out->num);
    }
    {
        Array *a = NULL;
        size_t off = 0;
        int rc = ensure_auto(v, ref, &a);
        if (rc) return rc;
        rc = index_of(a, ref->subs, &off);
        if (rc) return rc;
        if (ref->type == AS_T_STR) {
            out->type = AS_STR;
            return as_str_set(&out->str, a->str[off].data, a->str[off].len);
        }
        out->type = AS_NUM;
        out->num = (ref->type == AS_T_INT) ? a->in[off] : a->num[off];
        return AS_OK;
    }
}

int as_vars_store(AsVars *v, const AsRef *ref, const AsValue *val) {
    double num = 0;
    int rc;
    if (ref->name < 0 || ref->name >= AS_NVARS) return AS_ERR_SYNTAX;
    if (ref->type == AS_T_STR) {
        if (val->type != AS_STR) return AS_ERR_TYPE_MISMATCH;
    } else {
        rc = num_of(val, &num);
        if (rc) return rc;
        if (ref->type == AS_T_INT && (num < -32767 || num > 32767)) return AS_ERR_ILLEGAL_QUANTITY;
    }
    if (!ref->is_array) {
        if (ref->type == AS_T_STR) return as_str_set(&v->str[ref->name], val->str.data, val->str.len);
        if (ref->type == AS_T_INT) v->in[ref->name] = (int16_t)num;
        else v->flt[ref->name] = num;
        return AS_OK;
    }
    {
        Array *a = NULL;
        size_t off = 0;
        rc = ensure_auto(v, ref, &a);
        if (rc) return rc;
        rc = index_of(a, ref->subs, &off);
        if (rc) return rc;
        if (ref->type == AS_T_STR) return as_str_set(&a->str[off], val->str.data, val->str.len);
        if (ref->type == AS_T_INT) a->in[off] = (int16_t)num;
        else a->num[off] = num;
        return AS_OK;
    }
}
