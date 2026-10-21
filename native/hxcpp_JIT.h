#ifndef HXCPP_JIT_H
#define HXCPP_JIT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JitEngine JitEngine;
typedef struct JitValue  JitValue;
typedef struct JitFn     JitFn;

enum { JIT_VOID = 0, JIT_INT = 1, JIT_DOUBLE = 2, JIT_PTR = 3, JIT_STRING = 4, JIT_BOOL = 5 };

JitEngine*  jit_create(const char* const* extra_args, int n_args);
JitEngine*  jit_create_args(const char* args_newline_separated);
void        jit_destroy(JitEngine* e);
const char* jit_last_error(JitEngine* e);
const char* jit_llvm_version(void);

int   jit_eval(JitEngine* e, const char* code);
int   jit_enable_std_string(JitEngine* e);
int   jit_get(JitEngine* e, const char* expr, JitValue* out);
int   jit_set(JitEngine* e, const char* lvalue, JitValue* value);
void* jit_symbol(JitEngine* e, const char* ir_name);

JitFn* jit_bind(JitEngine* e, const char* name, const char* ret_type,
                const char* const* arg_types, int n_args);
JitFn* jit_bind_str(JitEngine* e, const char* name, const char* ret_type,
                    const char* arg_types_newline_separated);
int    jit_call(JitFn* f, JitValue* args, int count, JitValue* ret);
const char* jit_fn_last_error(JitFn* f);
void   jit_fn_free(JitFn* f);

JitValue*   jit_value_new(int count);
void        jit_value_free(JitValue* v);
JitValue*   jit_value_at(JitValue* v, int index);

void        jit_value_set_int(JitValue* v, long long x);
void        jit_value_set_double(JitValue* v, double x);
void        jit_value_set_ptr(JitValue* v, void* p);
void        jit_value_set_string(JitValue* v, const char* s);
void        jit_value_set_addr(JitValue* v, double address);

int         jit_value_kind(JitValue* v);
long long   jit_value_get_int(JitValue* v);
double      jit_value_get_double(JitValue* v);
void*       jit_value_get_ptr(JitValue* v);
const char* jit_value_get_string(JitValue* v);
double      jit_value_get_addr(JitValue* v);

#ifdef __cplusplus
}
#endif
#endif
