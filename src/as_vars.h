#ifndef AS_VARS_H
#define AS_VARS_H

#include "as_common.h"

typedef struct AsVars AsVars;

typedef struct AsRef {
    int is_array;
    int type;
    int name;
    int ndims;
    int subs[8];
} AsRef;

AsVars *as_vars_new(void);
void as_vars_free(AsVars *v);
void as_vars_clear(AsVars *v);

int as_name_index(int c1, int c2);

int as_vars_load(AsVars *v, const AsRef *ref, AsValue *out);
int as_vars_store(AsVars *v, const AsRef *ref, const AsValue *val);
int as_vars_dim(AsVars *v, int name, int type, const int *max_index, int ndims);

int as_vars_get_scalar(AsVars *v, int type, int name, double *out);
int as_vars_set_scalar(AsVars *v, int type, int name, double value);

#endif
