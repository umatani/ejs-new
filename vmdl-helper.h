#define concat(s1,s2)      ejs_string_concat(context, (s1), (s2))
#define toString(v)        to_string(context, (v))
/* #define FlonumToCdouble(f) to_double(context, (f)) */
#define FlonumToCdouble(v) flonum_to_double((v))
#define CdoubleToNumber(x) double_to_number((x))
#define FixnumToCint(v)    fixnum_to_cint((v))
#define CintToNumber(x)    cint_to_number((x))
#define toCdouble(v)       to_double(context, (v))
#define toNumber(v)        to_number(context, (v))
#define toObject(v)        to_object(context, (v))
#define getArrayProp(v1,v2)   get_array_prop(context, (v1), (v2))
#define getObjectProp(v1,v2)  get_object_prop(context, (v1), (v2))
#define String_to_cstr(v)  string_to_cstr((v))


#define FIXNUM_LESSTHAN(v1,v2) ((int64_t) (v1) < (int64_t) (v2))
#define Object_to_primitive_hint_number(v) object_to_primitive(context, (v) ,HINT_NUMBER)
#define Strcmp(x1,x2)     strcmp((x1), (x2))

