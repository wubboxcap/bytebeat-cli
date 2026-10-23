#include "quickjs.h"
#include "strict_expr.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef BYTEBEAT_GUI
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#endif

typedef enum {
    MODE_UNSIGNED,
    MODE_SIGNED,
    MODE_FLOATBEAT,
    MODE_FUNCBEAT,
} BeatMode;

typedef struct {
    BeatMode mode;
    int mode_explicit;
    int speedtest;
    int strict;
    uint32_t sample_rate;
    double duration;
    const char *output_path;
    const char *source;
} Options;

typedef struct {
    uint64_t deadline;
} InterruptState;

typedef struct {
    JSValue values[40];
} MathBindings;

enum {
    BIND_PI, BIND_E, BIND_INFINITY, BIND_NAN,
    BIND_ABS, BIND_ACOS, BIND_ACOSH, BIND_ASIN, BIND_ASINH,
    BIND_ATAN, BIND_ATAN2, BIND_ATANH, BIND_CBRT, BIND_CEIL,
    BIND_CLZ32, BIND_COS, BIND_COSH, BIND_EXP, BIND_EXPM1,
    BIND_FLOOR, BIND_FROUND, BIND_HYPOT, BIND_IMUL, BIND_LOG,
    BIND_LOG1P, BIND_LOG2, BIND_LOG10, BIND_MAX, BIND_MIN,
    BIND_POW, BIND_RANDOM, BIND_ROUND, BIND_SIGN, BIND_SIN,
    BIND_SINH, BIND_SQRT, BIND_TAN, BIND_TANH, BIND_TRUNC,
    BIND_COUNT
};

#define MATH_PARAMETER_LIST \
    "t,PI,E,Infinity,NaN,abs,acos,acosh,asin,asinh,atan,atan2,atanh,cbrt,ceil," \
    "clz32,cos,cosh,exp,expm1,floor,fround,hypot,imul,log,log1p,log2,log10," \
    "max,min,pow,random,round,sign,sin,sinh,sqrt,tan,tanh,trunc"

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: bytebeat [options] SOURCE\n"
            "\n"
            "Options:\n"
            "  -m, --mode MODE          unsigned, signed, floatbeat, or funcbeat\n"
            "  -sr, --sample-rate HZ   Sample rate (default: 8000)\n"
            "  -d, --duration SECONDS  Duration (default: 10)\n"
            "  -o, --output FILE       Write WAV to FILE instead of stdout\n"
            "      --strict, -strct    Evaluate a C-style expression (signed/unsigned only)\n"
            "  speedtest, --speedtest Benchmark sample generation speed\n"
            "  -h, --help              Show this help\n");
}

static int parse_positive_uint(const char *text, uint32_t *result)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *text == '\0' || *end != '\0' || value == 0 || value > UINT32_MAX)
        return -1;
    *result = (uint32_t)value;
    return 0;
}

static int parse_duration(const char *text, double *result)
{
    char *end;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno || *text == '\0' || *end != '\0' || !isfinite(value) || value < 0)
        return -1;
    *result = value;
    return 0;
}

static int parse_mode(const char *text, BeatMode *mode)
{
    if (strcmp(text, "unsigned") == 0) {
        *mode = MODE_UNSIGNED;
    } else if (strcmp(text, "signed") == 0) {
        *mode = MODE_SIGNED;
    } else if (strcmp(text, "floatbeat") == 0 || strcmp(text, "float") == 0) {
        *mode = MODE_FLOATBEAT;
    } else if (strcmp(text, "funcbeat") == 0 || strcmp(text, "func") == 0) {
        *mode = MODE_FUNCBEAT;
    } else {
        return -1;
    }
    return 0;
}

static int option_value(int argc, char **argv, int *index, const char *arg,
                        const char *long_name, const char *short_name,
                        const char **value)
{
    size_t long_length = strlen(long_name);
    size_t short_length = strlen(short_name);

    if (strncmp(arg, long_name, long_length) == 0 && arg[long_length] == '=') {
        *value = arg + long_length + 1;
        return **value ? 0 : -1;
    }
    if (strncmp(arg, short_name, short_length) == 0 && arg[short_length] == '=') {
        *value = arg + short_length + 1;
        return **value ? 0 : -1;
    }
    if (strcmp(arg, long_name) == 0 || strcmp(arg, short_name) == 0) {
        if (*index + 1 >= argc)
            return -1;
        *value = argv[++*index];
        return 0;
    }
    return 1;
}

static int parse_options(int argc, char **argv, Options *options)
{
    int index;

    options->mode = MODE_UNSIGNED;
    options->mode_explicit = 0;
    options->speedtest = 0;
    options->strict = 0;
    options->sample_rate = 8000;
    options->duration = 10.0;
    options->output_path = NULL;
    options->source = NULL;

    for (index = 1; index < argc; index++) {
        const char *arg = argv[index];
        const char *value;
        int matched;

        if ((strcmp(arg, "speedtest") == 0 || strcmp(arg, "--speedtest") == 0) &&
            options->source == NULL) {
            options->speedtest = 1;
            continue;
        }
        if (strcmp(arg, "--strict") == 0 || strcmp(arg, "-strct") == 0) {
            options->strict = 1;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 1;
        }
        if (arg[0] != '-') {
            if (options->source != NULL) {
                fprintf(stderr, "error: expected exactly one SOURCE\n");
                return -1;
            }
            options->source = arg;
            continue;
        }

        matched = option_value(argc, argv, &index, arg, "--mode", "-m", &value);
        if (matched <= 0) {
            if (matched < 0 || parse_mode(value, &options->mode) < 0) {
                fprintf(stderr, "error: invalid mode option\n");
                return -1;
            }
            options->mode_explicit = 1;
            continue;
        }
        matched = option_value(argc, argv, &index, arg, "--sample-rate", "-sr", &value);
        if (matched <= 0) {
            if (matched < 0 || parse_positive_uint(value, &options->sample_rate) < 0) {
                fprintf(stderr, "error: sample rate must be a positive integer\n");
                return -1;
            }
            continue;
        }
        matched = option_value(argc, argv, &index, arg, "--duration", "-d", &value);
        if (matched <= 0) {
            if (matched < 0 || parse_duration(value, &options->duration) < 0) {
                fprintf(stderr, "error: duration must be a nonnegative finite number\n");
                return -1;
            }
            continue;
        }
        matched = option_value(argc, argv, &index, arg, "--output", "-o", &value);
        if (matched <= 0) {
            if (matched < 0) {
                fprintf(stderr, "error: output path is required\n");
                return -1;
            }
            options->output_path = value;
            continue;
        }

        fprintf(stderr, "error: unknown option: %s\n", arg);
        return -1;
    }

    if (options->speedtest) {
        if (options->strict) {
            fprintf(stderr, "error: --strict cannot be used with speedtest\n");
            return -1;
        }
        if (options->output_path != NULL) {
            fprintf(stderr, "error: --output cannot be used with speedtest\n");
            return -1;
        }
        if (options->source != NULL && !options->mode_explicit) {
            fprintf(stderr, "error: a custom speedtest expression requires -m or --mode\n");
            return -1;
        }
        return 0;
    }
    if (options->source == NULL) {
        fprintf(stderr, "error: SOURCE is required\n");
        return -1;
    }
    if (options->strict && options->mode != MODE_UNSIGNED && options->mode != MODE_SIGNED) {
        fprintf(stderr, "error: --strict is only supported with unsigned and signed modes\n");
        return -1;
    }
    return 0;
}

static void write_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static int write_wav_header(FILE *stream, const Options *options, uint64_t sample_count)
{
    uint8_t header[44] = {0};
    uint16_t format = options->mode == MODE_UNSIGNED || options->mode == MODE_SIGNED ? 1 : 3;
    uint16_t bits = format == 1 ? 8 : 32;
    uint16_t block_align = bits / 8;
    uint64_t data_size;
    uint64_t riff_size;

    if (sample_count > UINT32_MAX / block_align) {
        fprintf(stderr, "error: output is too large for a RIFF/WAV file\n");
        return -1;
    }
    data_size = sample_count * block_align;
    riff_size = 36 + data_size;
    memcpy(header, "RIFF", 4);
    write_u32(header + 4, (uint32_t)riff_size);
    memcpy(header + 8, "WAVEfmt ", 8);
    write_u32(header + 16, 16);
    write_u16(header + 20, format);
    write_u16(header + 22, 1);
    write_u32(header + 24, options->sample_rate);
    write_u32(header + 28, options->sample_rate * block_align);
    write_u16(header + 32, block_align);
    write_u16(header + 34, bits);
    memcpy(header + 36, "data", 4);
    write_u32(header + 40, (uint32_t)data_size);
    return fwrite(header, 1, sizeof(header), stream) == sizeof(header) ? 0 : -1;
}

static int interrupt_handler(JSRuntime *runtime, void *opaque)
{
    InterruptState *state = opaque;
    (void)runtime;
    return (uint64_t)clock() >= state->deadline;
}

static void print_exception(JSContext *context, const char *prefix)
{
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    fprintf(stderr, "error: %s%s%s\n", prefix, message != NULL ? ": " : "",
            message != NULL ? message : "JavaScript exception");
    if (message != NULL)
        JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
}

enum {
    MATH_ABS, MATH_ACOS, MATH_ACOSH, MATH_ASIN, MATH_ASINH, MATH_ATAN,
    MATH_ATAN2, MATH_ATANH, MATH_CBRT, MATH_CEIL, MATH_CLZ32, MATH_COS,
    MATH_COSH, MATH_EXP, MATH_EXPM1, MATH_FLOOR, MATH_FROUND, MATH_HYPOT,
    MATH_IMUL, MATH_LOG, MATH_LOG1P, MATH_LOG2, MATH_LOG10, MATH_MAX,
    MATH_MIN, MATH_POW, MATH_RANDOM, MATH_ROUND, MATH_SIGN, MATH_SIN,
    MATH_SINH, MATH_SQRT, MATH_TAN, MATH_TANH, MATH_TRUNC
};

static JSValue math_function(JSContext *context, JSValueConst this_val, int argc,
                             JSValueConst *argv, int magic)
{
    double first;
    double second;
    int index;
    double value;
    (void)this_val;
    if (magic == MATH_RANDOM)
        return JS_NewFloat64(context, (double)rand() / ((double)RAND_MAX + 1.0));
    if (argc < 1 || JS_ToFloat64(context, &first, argv[0]) < 0)
        return JS_ThrowTypeError(context, "math argument must be numeric");
    if ((magic == MATH_ATAN2 || magic == MATH_POW) &&
        (argc < 2 || JS_ToFloat64(context, &second, argv[1]) < 0))
        return JS_ThrowTypeError(context, "math function requires two arguments");
    switch (magic) {
    case MATH_ABS: return JS_NewFloat64(context, fabs(first));
    case MATH_ACOS: return JS_NewFloat64(context, acos(first));
    case MATH_ACOSH: return JS_NewFloat64(context, acosh(first));
    case MATH_ASIN: return JS_NewFloat64(context, asin(first));
    case MATH_ASINH: return JS_NewFloat64(context, asinh(first));
    case MATH_ATAN: return JS_NewFloat64(context, atan(first));
    case MATH_ATAN2: return JS_NewFloat64(context, atan2(first, second));
    case MATH_ATANH: return JS_NewFloat64(context, atanh(first));
    case MATH_CBRT: return JS_NewFloat64(context, cbrt(first));
    case MATH_CEIL: return JS_NewFloat64(context, ceil(first));
    case MATH_CLZ32: {
        uint32_t integer = (uint32_t)first;
        return JS_NewInt32(context, integer == 0 ? 32 : (int32_t)__builtin_clz(integer));
    }
    case MATH_COS: return JS_NewFloat64(context, cos(first));
    case MATH_COSH: return JS_NewFloat64(context, cosh(first));
    case MATH_EXP: return JS_NewFloat64(context, exp(first));
    case MATH_EXPM1: return JS_NewFloat64(context, expm1(first));
    case MATH_FLOOR: return JS_NewFloat64(context, floor(first));
    case MATH_FROUND: return JS_NewFloat64(context, (float)first);
    case MATH_HYPOT:
        value = first;
        for (index = 1; index < argc; index++) {
            if (JS_ToFloat64(context, &second, argv[index]) < 0)
                return JS_ThrowTypeError(context, "math argument must be numeric");
            value = hypot(value, second);
        }
        return JS_NewFloat64(context, value);
    case MATH_IMUL: {
        int32_t left = (int32_t)first;
        int32_t right;
        if (argc < 2 || JS_ToInt32(context, &right, argv[1]) < 0)
            return JS_ThrowTypeError(context, "imul requires two arguments");
        return JS_NewInt32(context, left * right);
    }
    case MATH_LOG: return JS_NewFloat64(context, log(first));
    case MATH_LOG1P: return JS_NewFloat64(context, log1p(first));
    case MATH_LOG2: return JS_NewFloat64(context, log2(first));
    case MATH_LOG10: return JS_NewFloat64(context, log10(first));
    case MATH_MAX:
    case MATH_MIN:
        value = first;
        for (index = 1; index < argc; index++) {
            if (JS_ToFloat64(context, &second, argv[index]) < 0)
                return JS_ThrowTypeError(context, "math argument must be numeric");
            value = magic == MATH_MAX ? fmax(value, second) : fmin(value, second);
        }
        return JS_NewFloat64(context, value);
    case MATH_POW: return JS_NewFloat64(context, pow(first, second));
    case MATH_ROUND: return JS_NewFloat64(context, round(first));
    case MATH_SIGN: return JS_NewFloat64(context, first > 0 ? 1 : first < 0 ? -1 : 0);
    case MATH_SIN: return JS_NewFloat64(context, sin(first));
    case MATH_SINH: return JS_NewFloat64(context, sinh(first));
    case MATH_SQRT: return JS_NewFloat64(context, sqrt(first));
    case MATH_TAN: return JS_NewFloat64(context, tan(first));
    case MATH_TRUNC: return JS_NewFloat64(context, trunc(first));
    default: return JS_ThrowInternalError(context, "unknown math function");
    }
}

static int create_math_bindings(JSContext *context, MathBindings *math)
{
    static const struct { const char *name; int magic; int argc; } functions[] = {
        {"abs", MATH_ABS, 1}, {"acos", MATH_ACOS, 1}, {"acosh", MATH_ACOSH, 1},
        {"asin", MATH_ASIN, 1}, {"asinh", MATH_ASINH, 1}, {"atan", MATH_ATAN, 1},
        {"atan2", MATH_ATAN2, 2}, {"atanh", MATH_ATANH, 1}, {"cbrt", MATH_CBRT, 1},
        {"ceil", MATH_CEIL, 1}, {"clz32", MATH_CLZ32, 1}, {"cos", MATH_COS, 1},
        {"cosh", MATH_COSH, 1}, {"exp", MATH_EXP, 1}, {"expm1", MATH_EXPM1, 1},
        {"floor", MATH_FLOOR, 1}, {"fround", MATH_FROUND, 1}, {"hypot", MATH_HYPOT, 2},
        {"imul", MATH_IMUL, 2}, {"log", MATH_LOG, 1}, {"log1p", MATH_LOG1P, 1},
        {"log2", MATH_LOG2, 1}, {"log10", MATH_LOG10, 1}, {"max", MATH_MAX, 2},
        {"min", MATH_MIN, 2}, {"pow", MATH_POW, 2}, {"random", MATH_RANDOM, 0},
        {"round", MATH_ROUND, 1}, {"sign", MATH_SIGN, 1}, {"sin", MATH_SIN, 1},
        {"sinh", MATH_SINH, 1}, {"sqrt", MATH_SQRT, 1}, {"tan", MATH_TAN, 1},
        {"tanh", MATH_TANH, 1}, {"trunc", MATH_TRUNC, 1}
    };
    size_t index;
    math->values[BIND_PI] = JS_NewFloat64(context, 3.141592653589793);
    math->values[BIND_E] = JS_NewFloat64(context, 2.718281828459045);
    math->values[BIND_INFINITY] = JS_NewFloat64(context, INFINITY);
    math->values[BIND_NAN] = JS_NewFloat64(context, NAN);
    for (index = 0; index < sizeof(functions) / sizeof(functions[0]); index++) {
        math->values[BIND_ABS + index] = JS_NewCFunctionMagic(
            context, math_function, functions[index].name, functions[index].argc,
            JS_CFUNC_generic_magic, functions[index].magic);
        if (JS_IsException(math->values[BIND_ABS + index]))
            return -1;
    }
    return 0;
}

static void free_math_bindings(JSContext *context, MathBindings *math)
{
    size_t index;
    for (index = 0; index < BIND_COUNT; index++)
        JS_FreeValue(context, math->values[index]);
}

static char *normalize_source(const char *source)
{
    size_t length = strlen(source);
    char *result = malloc(length + 1);
    size_t input = 0;
    size_t output = 0;
    if (result == NULL)
        return NULL;
    while (input < length) {
        if (input + 5 <= length && strncmp(source + input, "Math.", 5) == 0)
            input += 5;
        else
            result[output++] = source[input++];
    }
    result[output] = '\0';
    return result;
}

static int is_identifier_start(char character)
{
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '_' || character == '$';
}

static int is_identifier_part(char character)
{
    return is_identifier_start(character) || (character >= '0' && character <= '9');
}

static char *assignment_declarations(const char *source)
{
    size_t length = strlen(source);
    char *declarations = calloc(length + 6, 1);
    size_t output = 0;
    size_t index;
    int found = 0;

    if (declarations == NULL)
        return NULL;
    memcpy(declarations, "var ", 4);
    output = 4;
    for (index = 0; index < length;) {
        size_t start;
        size_t cursor;
        size_t name_length;
        size_t previous;
        int already_declared = 0;

        if (!is_identifier_start(source[index])) {
            index++;
            continue;
        }
        start = index++;
        while (index < length && is_identifier_part(source[index]))
            index++;
        cursor = index;
        while (cursor < length && (source[cursor] == ' ' || source[cursor] == '\t' ||
                                   source[cursor] == '\r' || source[cursor] == '\n'))
            cursor++;
                if (cursor >= length ||
                        !((source[cursor] == '=' &&
                             (cursor + 1 >= length || (source[cursor + 1] != '=' && source[cursor + 1] != '>'))) ||
                            ((source[cursor] == '+' || source[cursor] == '-' || source[cursor] == '*' ||
                                source[cursor] == '/' || source[cursor] == '%' || source[cursor] == '&' ||
                                source[cursor] == '|' || source[cursor] == '^') &&
                             cursor + 1 < length && source[cursor + 1] == '=')))
            continue;
        previous = start;
        while (previous > 0 && (source[previous - 1] == ' ' || source[previous - 1] == '\t'))
            previous--;
        if (previous > 0 && source[previous - 1] == '.')
            continue;
        name_length = index - start;
        if ((name_length == 3 && strncmp(source + start, "var", 3) == 0) ||
            (name_length == 3 && strncmp(source + start, "let", 3) == 0) ||
            (name_length == 5 && strncmp(source + start, "const", 5) == 0))
            continue;
        for (previous = 4; previous < output; previous++) {
            if (strncmp(declarations + previous, source + start, name_length) == 0 &&
                (declarations[previous + name_length] == ',' ||
                 declarations[previous + name_length] == '\0')) {
                already_declared = 1;
                break;
            }
        }
        if (!already_declared) {
            if (found)
                declarations[output++] = ',';
            memcpy(declarations + output, source + start, name_length);
            output += name_length;
            found = 1;
        }
    }
    if (found)
        declarations[output++] = ';';
    else
        output = 0;
    declarations[output] = '\0';
    return declarations;
}

static JSValue evaluate_function_body(JSContext *context, const char *source)
{
    const char *prefix = "(function(" MATH_PARAMETER_LIST ") { ";
    const char *suffix = " })";
    size_t source_length = strlen(source);
    size_t length = strlen(prefix) + source_length + strlen(suffix);
    char *program = malloc(length + 1);
    JSValue function;

    if (program == NULL)
        return JS_ThrowOutOfMemory(context);
    memcpy(program, prefix, strlen(prefix));
    memcpy(program + strlen(prefix), source, source_length);
    memcpy(program + strlen(prefix) + source_length, suffix, strlen(suffix) + 1);
    function = JS_Eval(context, program, length, "<bytebeat>", JS_EVAL_TYPE_GLOBAL);
    free(program);
    return function;
}

static JSValue evaluate_function(JSContext *context, const Options *options)
{
    char *source = normalize_source(options->source);
    char *declarations;
    size_t source_length;
    const char *prefix = "(function(" MATH_PARAMETER_LIST ") { ";
    const char *suffix = "); })";
    size_t length;
    char *program;
    JSValue function;

    if (source == NULL)
        return JS_ThrowOutOfMemory(context);
    declarations = assignment_declarations(source);
    if (declarations == NULL) {
        free(source);
        return JS_ThrowOutOfMemory(context);
    }
    source_length = strlen(source);
    length = strlen(prefix) + strlen(declarations) + strlen("return (") +
             source_length + strlen(suffix);
    program = malloc(length + 1);
    if (program == NULL) {
        free(declarations);
        free(source);
        return JS_ThrowOutOfMemory(context);
    }
    memcpy(program, prefix, strlen(prefix));
    snprintf(program + strlen(prefix), length + 1 - strlen(prefix), "%sreturn (%s%s",
             declarations, source, suffix);
    function = JS_Eval(context, program, length, "<bytebeat>", JS_EVAL_TYPE_GLOBAL);
    free(program);
    free(declarations);
    if (JS_IsException(function)) {
        JS_FreeValue(context, function);
        function = evaluate_function_body(context, source);
    }
    free(source);
    return function;
}

static JSValue evaluate_funcbeat(JSContext *context, const char *source,
                                 const MathBindings *math)
{
    char *normalized = normalize_source(source);
    char *declarations;
    const char *prefix = "(function(PI,E,Infinity,NaN,abs,acos,acosh,asin,asinh,atan,atan2,atanh,cbrt,ceil,"
                         "clz32,cos,cosh,exp,expm1,floor,fround,hypot,imul,log,log1p,log2,log10,max,min,"
                         "pow,random,round,sign,sin,sinh,sqrt,tan,tanh,trunc) { ";
    const char *suffix = "; })";
    size_t length;
    char *program;
    JSValue factory;
    JSValue arguments[BIND_COUNT];
    JSValue function;
    const char *expression;

    if (normalized == NULL)
        return JS_ThrowOutOfMemory(context);
    declarations = assignment_declarations(normalized);
    if (declarations == NULL) {
        free(normalized);
        return JS_ThrowOutOfMemory(context);
    }
    expression = normalized;
    while (*expression == ' ' || *expression == '\t' || *expression == '\r' || *expression == '\n')
        expression++;
    if (strncmp(expression, "return", 6) == 0 &&
        (expression[6] == ' ' || expression[6] == '\t' || expression[6] == '\r' || expression[6] == '\n'))
        expression += 6;
    while (*expression == ' ' || *expression == '\t' || *expression == '\r' || *expression == '\n')
        expression++;
    length = strlen(prefix) + strlen(declarations) + strlen("return ") +
             strlen(expression) + strlen(suffix);
    program = malloc(length + 1);
    if (program == NULL)
        return JS_ThrowOutOfMemory(context);
    snprintf(program, length + 1, "%s%sreturn %s%s", prefix, declarations, expression, suffix);
    factory = JS_Eval(context, program, length, "<funcbeat>", JS_EVAL_TYPE_GLOBAL);
    free(program);
    free(declarations);
    if (JS_IsException(factory))
    {
        free(normalized);
        return factory;
    }
    for (length = 0; length < BIND_COUNT; length++)
        arguments[length] = math->values[length];
    function = JS_Call(context, factory, JS_UNDEFINED, BIND_COUNT, arguments);
    JS_FreeValue(context, factory);
    free(normalized);
    return function;
}

static int write_sample(FILE *stream, BeatMode mode, JSContext *context,
                        JSValueConst function, const MathBindings *math,
                        uint64_t index, uint32_t sample_rate)
{
    double input = mode == MODE_UNSIGNED || mode == MODE_SIGNED
                       ? (double)index
                       : (double)index / sample_rate;
    JSValue arguments[BIND_COUNT + 1];
    JSValue result;
    size_t argument_index;
    arguments[0] = JS_NewFloat64(context, input);
    for (argument_index = 0; argument_index < BIND_COUNT; argument_index++)
        arguments[argument_index + 1] = math->values[argument_index];
    result = JS_Call(context, function, JS_UNDEFINED, BIND_COUNT + 1, arguments);
    int status = 0;

    JS_FreeValue(context, arguments[0]);
    if (JS_IsException(result)) {
        print_exception(context, "sample evaluation failed");
        return -1;
    }

    if (mode == MODE_UNSIGNED || mode == MODE_SIGNED) {
        int32_t value;
        uint8_t encoded;
        if (JS_ToInt32(context, &value, result) < 0) {
            JS_FreeValue(context, result);
            print_exception(context, "sample is not an integer");
            return -1;
        }
        encoded = (uint8_t)value;
        if (mode == MODE_SIGNED)
            encoded = (uint8_t)((int)value + 128);
        status = fwrite(&encoded, 1, 1, stream) == 1 ? 0 : -1;
    } else {
        double value;
        float encoded;
        if (JS_ToFloat64(context, &value, result) < 0 || !isfinite(value)) {
            JS_FreeValue(context, result);
            print_exception(context, "sample must be a finite number");
            return -1;
        }
        if (value < -1.0)
            value = -1.0;
        if (value > 1.0)
            value = 1.0;
        encoded = (float)value;
        status = fwrite(&encoded, sizeof(encoded), 1, stream) == 1 ? 0 : -1;
    }
    JS_FreeValue(context, result);
    if (status < 0)
        fprintf(stderr, "error: failed to write output\n");
    return status;
}

#define SPEEDTEST_SAMPLES 1000000ULL

static const char *mode_name(BeatMode mode)
{
    switch (mode) {
    case MODE_UNSIGNED: return "unsigned";
    case MODE_SIGNED: return "signed";
    case MODE_FLOATBEAT: return "floatbeat";
    case MODE_FUNCBEAT: return "funcbeat";
    default: return "unknown";
    }
}

static const char *speedtest_source(const Options *options, BeatMode mode)
{
    if (options->source != NULL)
        return options->source;
    return mode == MODE_FUNCBEAT ? "t=>t" : "t";
}

static int benchmark_mode(JSContext *context, const Options *options,
                          const MathBindings *math, BeatMode mode)
{
    Options test_options = *options;
    const char *source = speedtest_source(options, mode);
    JSValue function = JS_UNDEFINED;
    clock_t start;
    clock_t finish;
    volatile double float_sink = 0;
    volatile int32_t integer_sink = 0;
    uint64_t index;
    int status = -1;

    test_options.mode = mode;
    test_options.source = source;
    if (mode == MODE_FUNCBEAT)
        function = evaluate_funcbeat(context, source, math);
    else
        function = evaluate_function(context, &test_options);
    if (JS_IsException(function)) {
        print_exception(context, "speedtest source evaluation failed");
        goto cleanup;
    }
    if (!JS_IsFunction(context, function)) {
        fprintf(stderr, "error: speedtest source did not produce a function\n");
        goto cleanup;
    }

    start = clock();
    for (index = 0; index < SPEEDTEST_SAMPLES; index++) {
        double input = mode == MODE_UNSIGNED || mode == MODE_SIGNED
                           ? (double)index
                           : (double)index / options->sample_rate;
        JSValue arguments[BIND_COUNT + 1];
        JSValue result;
        size_t argument_index;
        int32_t integer_value;
        double float_value;

        arguments[0] = JS_NewFloat64(context, input);
        for (argument_index = 0; argument_index < BIND_COUNT; argument_index++)
            arguments[argument_index + 1] = math->values[argument_index];
        result = JS_Call(context, function, JS_UNDEFINED, BIND_COUNT + 1, arguments);
        JS_FreeValue(context, arguments[0]);
        if (JS_IsException(result)) {
            print_exception(context, "speedtest sample evaluation failed");
            goto cleanup;
        }
        if (mode == MODE_UNSIGNED || mode == MODE_SIGNED) {
            if (JS_ToInt32(context, &integer_value, result) < 0) {
                JS_FreeValue(context, result);
                print_exception(context, "speedtest sample conversion failed");
                goto cleanup;
            }
            integer_sink = integer_value;
        } else {
            if (JS_ToFloat64(context, &float_value, result) < 0) {
                JS_FreeValue(context, result);
                print_exception(context, "speedtest sample conversion failed");
                goto cleanup;
            }
            float_sink = float_value;
        }
        JS_FreeValue(context, result);
    }
    finish = clock();
    {
        double elapsed = (double)(finish - start) / CLOCKS_PER_SEC;
        double samples_per_second = elapsed > 0
                                        ? SPEEDTEST_SAMPLES / elapsed
                                        : INFINITY;
        printf("%-9s %10.0f samples/sec  %8.3f sec  (%llu samples)\n",
               mode_name(mode), samples_per_second, elapsed,
               (unsigned long long)SPEEDTEST_SAMPLES);
    }
    status = 0;

cleanup:
    JS_FreeValue(context, function);
    return status;
}

static int run_speedtest(JSContext *context, const Options *options,
                         const MathBindings *math)
{
    static const BeatMode modes[] = {
        MODE_UNSIGNED, MODE_SIGNED, MODE_FLOATBEAT, MODE_FUNCBEAT
    };
    size_t index;

    printf("Speedtest: %llu samples per mode, sample rate %u Hz\n",
           (unsigned long long)SPEEDTEST_SAMPLES, options->sample_rate);
    if (options->mode_explicit)
        return benchmark_mode(context, options, math, options->mode);
    for (index = 0; index < sizeof(modes) / sizeof(modes[0]); index++) {
        if (benchmark_mode(context, options, math, modes[index]) < 0)
            return -1;
    }
    return 0;
}

static int write_strict_sample(FILE *stream, const Options *options, uint64_t index)
{
    char error[160];
    double sample;
    int32_t value;
    uint8_t encoded;
    if (strict_expr_evaluate(options->source, (double)index, &sample,
                             error, sizeof(error)) < 0) {
        fprintf(stderr, "error: strict C expression failed: %s\n", error);
        return -1;
    }
    if (!isfinite(sample)) {
        fprintf(stderr, "error: strict C expression returned a non-finite sample\n");
        return -1;
    }
    value = (int32_t)sample;
    encoded = (uint8_t)value;
    if (options->mode == MODE_SIGNED)
        encoded = (uint8_t)((int)value + 128);
    if (fwrite(&encoded, 1, 1, stream) != 1) {
        fprintf(stderr, "error: failed to write output\n");
        return -1;
    }
    return 0;
}

#ifdef BYTEBEAT_GUI

#define GUI_BLOCK_SAMPLES 1024
#define GUI_RING_SAMPLES 4096
#define GUI_FORMULA_CAPACITY 8192

enum {
    ID_FORMULA = 100,
    ID_TIME_MODE,
    ID_TIME,
    ID_CPU,
    ID_STOP,
    ID_RESET,
    ID_BACK,
    ID_REVERSE,
    ID_SLOWER,
    ID_PAUSE,
    ID_FASTER,
    ID_ZOOM_OUT,
    ID_ZOOM_VALUE,
    ID_ZOOM_IN,
    ID_DISPLAY,
    ID_MODE,
    ID_SAMPLE_RATE,
    ID_DOWNSAMPLE,
    ID_RECORD,
    ID_BYTES,
    ID_VOLUME,
    ID_STATUS
};

typedef struct {
    HWND window;
    HWND formula;
    HWND time_mode;
    HWND time;
    HWND cpu;
    HWND display;
    HWND mode_combo;
    HWND sample_rate_combo;
    HWND downsample;
    HWND record;
    HWND bytes;
    HWND volume;
    HWND status;
    HANDLE worker;
    HANDLE stop_event;
    CRITICAL_SECTION lock;
    char source[GUI_FORMULA_CAPACITY];
    uint64_t source_revision;
    uint64_t position;
    double playhead;
    uint64_t seek_position;
    int seek_requested;
    int playing;
    int time_focused;
    int reverse;
    int speed_exponent;
    int zoom_exponent;
    int display_mode;
    int volume_percent;
    int mode;
    uint32_t sample_rate;
    double cpu_percent;
    float samples[GUI_RING_SAMPLES];
    uint32_t ring_position;
    FILE *record_file;
    int record_stop_requested;
    uint64_t recorded_samples;
    uint32_t recorded_sample_rate;
    char status_text[256];
    HBRUSH background_brush;
} GuiState;

typedef struct {
    WAVEHDR header;
    int16_t samples[GUI_BLOCK_SAMPLES];
} GuiAudioBuffer;

static GuiState gui;

static void gui_set_status(const char *text)
{
    EnterCriticalSection(&gui.lock);
    snprintf(gui.status_text, sizeof(gui.status_text), "%s", text);
    LeaveCriticalSection(&gui.lock);
}

static int gui_recording_active(void)
{
    int active;
    EnterCriticalSection(&gui.lock);
    active = gui.record_file != NULL;
    LeaveCriticalSection(&gui.lock);
    return active;
}

static int16_t gui_to_pcm16(double sample, int volume)
{
    double value = sample * ((double)volume / 100.0);
    if (value < -1.0)
        value = -1.0;
    if (value > 1.0)
        value = 1.0;
    return (int16_t)(value * 32767.0);
}

static int gui_evaluate_sample(JSContext *context, JSRuntime *runtime,
                               InterruptState *interrupt_state, BeatMode mode,
                               JSValueConst function, const MathBindings *math,
                               uint64_t index, uint32_t sample_rate,
                               double *sample)
{
    JSValue arguments[BIND_COUNT + 1];
    JSValue result;
    size_t argument_index;
    double input = mode == MODE_UNSIGNED || mode == MODE_SIGNED
                       ? (double)index
                       : (double)index / sample_rate;
    interrupt_state->deadline = (uint64_t)clock() + (uint64_t)CLOCKS_PER_SEC / 4;
    arguments[0] = JS_NewFloat64(context, input);
    for (argument_index = 0; argument_index < BIND_COUNT; argument_index++)
        arguments[argument_index + 1] = math->values[argument_index];
    result = JS_Call(context, function, JS_UNDEFINED, BIND_COUNT + 1, arguments);
    JS_FreeValue(context, arguments[0]);
    if (JS_IsException(result)) {
        print_exception(context, "sample evaluation failed");
        return -1;
    }
    if (mode == MODE_UNSIGNED || mode == MODE_SIGNED) {
        int32_t value;
        uint8_t encoded;
        if (JS_ToInt32(context, &value, result) < 0) {
            JS_FreeValue(context, result);
            print_exception(context, "sample is not an integer");
            return -1;
        }
        encoded = (uint8_t)value;
        if (mode == MODE_SIGNED)
            encoded = (uint8_t)((int)value + 128);
        *sample = ((double)encoded - 128.0) / 128.0;
    } else {
        if (JS_ToFloat64(context, sample, result) < 0 || !isfinite(*sample)) {
            JS_FreeValue(context, result);
            print_exception(context, "sample must be a finite number");
            return -1;
        }
        if (*sample < -1.0)
            *sample = -1.0;
        if (*sample > 1.0)
            *sample = 1.0;
    }
    JS_FreeValue(context, result);
    (void)runtime;
    return 0;
}

static int gui_compile_source(JSContext *context, const char *source,
                              BeatMode mode, const MathBindings *math,
                              JSValue *function)
{
    Options options;
    JSValue replacement;
    memset(&options, 0, sizeof(options));
    options.mode = mode;
    options.source = source;
    replacement = mode == MODE_FUNCBEAT
                     ? evaluate_funcbeat(context, source, math)
                     : evaluate_function(context, &options);
    if (JS_IsException(replacement) || !JS_IsFunction(context, replacement)) {
        if (JS_IsException(replacement))
            print_exception(context, "could not evaluate source");
        else
            fprintf(stderr, "error: source did not produce a function\n");
        JS_FreeValue(context, replacement);
        return -1;
    }
    JS_FreeValue(context, *function);
    *function = replacement;
    return 0;
}

static void gui_write_wav_header(FILE *stream, uint32_t sample_rate,
                                 uint64_t sample_count)
{
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    write_u32(header + 4, (uint32_t)(36 + sample_count * 2));
    memcpy(header + 8, "WAVEfmt ", 8);
    write_u32(header + 16, 16);
    write_u16(header + 20, 1);
    write_u16(header + 22, 1);
    write_u32(header + 24, sample_rate);
    write_u32(header + 28, sample_rate * 2);
    write_u16(header + 32, 2);
    write_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    write_u32(header + 40, (uint32_t)(sample_count * 2));
    fseek(stream, 0, SEEK_SET);
    fwrite(header, 1, sizeof(header), stream);
}

static void gui_finish_recording(void)
{
    FILE *stream;
    uint32_t sample_rate;
    uint64_t sample_count;
    EnterCriticalSection(&gui.lock);
    stream = gui.record_file;
    sample_count = gui.recorded_samples;
    sample_rate = gui.recorded_sample_rate;
    gui.record_file = NULL;
    gui.record_stop_requested = 0;
    gui.recorded_samples = 0;
    LeaveCriticalSection(&gui.lock);
    if (stream != NULL) {
        gui_write_wav_header(stream, sample_rate, sample_count);
        fclose(stream);
    }
    if (gui.record != NULL)
        SetWindowTextA(gui.record, "Record");
}

static void gui_close_audio(HWAVEOUT *wave, HANDLE audio_event,
                            GuiAudioBuffer *buffers)
{
    size_t index;
    if (*wave == NULL)
        return;
    waveOutReset(*wave);
    for (index = 0; index < 3; index++) {
        if ((buffers[index].header.dwFlags & WHDR_PREPARED) != 0)
            waveOutUnprepareHeader(*wave, &buffers[index].header,
                                   sizeof(buffers[index].header));
        memset(&buffers[index].header, 0, sizeof(buffers[index].header));
    }
    waveOutClose(*wave);
    *wave = NULL;
    ResetEvent(audio_event);
}

static DWORD WINAPI gui_audio_worker(void *unused)
{
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    MathBindings math;
    InterruptState interrupt_state;
    JSValue function = JS_UNDEFINED;
    HWAVEOUT wave = NULL;
    HANDLE audio_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    GuiAudioBuffer buffers[3];
    uint64_t compiled_revision = UINT64_MAX;
    uint32_t opened_rate = 0;
    int wave_paused = 1;
    size_t index;
    (void)unused;
    memset(&math, 0, sizeof(math));
    memset(buffers, 0, sizeof(buffers));
    for (index = 0; index < BIND_COUNT; index++)
        math.values[index] = JS_UNDEFINED;
    if (runtime == NULL || context == NULL || audio_event == NULL) {
        gui_set_status("Could not initialize audio or JavaScript runtime");
        goto cleanup;
    }
    JS_SetMemoryLimit(runtime, 64 * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 1024 * 1024);
    interrupt_state.deadline = (uint64_t)clock() + CLOCKS_PER_SEC;
    JS_SetInterruptHandler(runtime, interrupt_handler, &interrupt_state);
    if (create_math_bindings(context, &math) < 0) {
        gui_set_status("Could not initialize JavaScript math functions");
        goto cleanup;
    }

    while (WaitForSingleObject(gui.stop_event, 0) != WAIT_OBJECT_0) {
        char source[GUI_FORMULA_CAPACITY];
        uint64_t source_revision;
        uint64_t position;
        double playhead;
        uint32_t sample_rate;
        int playing;
        int focused;
        int reverse;
        int speed_exponent;
        int volume;
        BeatMode mode;
        int seek_requested;
        uint64_t seek_position;

        EnterCriticalSection(&gui.lock);
        memcpy(source, gui.source, sizeof(source));
        source_revision = gui.source_revision;
        position = gui.position;
            playhead = gui.playhead;
        sample_rate = gui.sample_rate;
        playing = gui.playing;
        focused = gui.time_focused;
        reverse = gui.reverse;
        speed_exponent = gui.speed_exponent;
        volume = gui.volume_percent;
        mode = (BeatMode)gui.mode;
        seek_requested = gui.seek_requested;
        seek_position = gui.seek_position;
        gui.seek_requested = 0;
        LeaveCriticalSection(&gui.lock);

        if (seek_requested) {
            position = seek_position;
            playhead = (double)seek_position;
            if (wave != NULL)
                waveOutReset(wave);
        }
        if (source_revision != compiled_revision) {
            if (gui_compile_source(context, source, mode, &math, &function) == 0) {
                compiled_revision = source_revision;
                gui_set_status("Formula ready");
            } else {
                compiled_revision = source_revision;
                gui_set_status("Formula error; keeping the previous sound");
            }
        }
        if (wave != NULL && (sample_rate != opened_rate || seek_requested)) {
            gui_close_audio(&wave, audio_event, buffers);
            opened_rate = 0;
            wave_paused = 1;
        }
        if (wave == NULL && sample_rate != 0) {
            WAVEFORMATEX format;
            MMRESULT result;
            memset(&format, 0, sizeof(format));
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = 1;
            format.nSamplesPerSec = sample_rate;
            format.wBitsPerSample = 16;
            format.nBlockAlign = 2;
            format.nAvgBytesPerSec = sample_rate * 2;
            format.cbSize = 0;
            result = waveOutOpen(&wave, WAVE_MAPPER, &format,
                                 (DWORD_PTR)audio_event, 0, CALLBACK_EVENT);
            if (result != MMSYSERR_NOERROR) {
                wave = NULL;
                gui_set_status("Could not open Windows audio output");
                WaitForSingleObject(gui.stop_event, 100);
                continue;
            }
            opened_rate = sample_rate;
            wave_paused = 1;
        }
        if (wave != NULL) {
            int should_play = playing && !focused && JS_IsFunction(context, function);
            if (should_play && wave_paused) {
                waveOutRestart(wave);
                wave_paused = 0;
            } else if (!should_play && !wave_paused) {
                waveOutPause(wave);
                wave_paused = 1;
            }
            if (should_play) {
                int submitted = 0;
                for (index = 0; index < 3; index++) {
                    GuiAudioBuffer *buffer = &buffers[index];
                    clock_t cpu_start;
                    clock_t cpu_finish;
                    float generated[GUI_BLOCK_SAMPLES];
                    size_t sample_index;
                    if ((buffer->header.dwFlags & WHDR_PREPARED) != 0 &&
                        (buffer->header.dwFlags & WHDR_DONE) == 0)
                        continue;
                    if ((buffer->header.dwFlags & WHDR_PREPARED) != 0)
                        waveOutUnprepareHeader(wave, &buffer->header,
                                               sizeof(buffer->header));
                    memset(&buffer->header, 0, sizeof(buffer->header));
                    cpu_start = clock();
                    for (sample_index = 0; sample_index < GUI_BLOCK_SAMPLES;
                         sample_index++) {
                        double sample = 0;
                        if (gui_evaluate_sample(context, runtime, &interrupt_state,
                                                mode, function, &math,
                                                (uint64_t)playhead,
                                                sample_rate, &sample) < 0) {
                            gui_set_status("Sample error; check the formula");
                            playing = 0;
                            EnterCriticalSection(&gui.lock);
                            gui.playing = 0;
                            LeaveCriticalSection(&gui.lock);
                            break;
                        }
                        generated[sample_index] = (float)sample;
                        buffer->samples[sample_index] = gui_to_pcm16(sample, volume);
                        if (reverse)
                            playhead = fmax(0.0, playhead - ldexp(1.0, speed_exponent));
                        else
                            playhead = fmin((double)UINT64_MAX,
                                            playhead + ldexp(1.0, speed_exponent));
                        position = (uint64_t)playhead;
                    }
                    if (!playing)
                        break;
                    cpu_finish = clock();
                    {
                        FILE *record_stream;
                        EnterCriticalSection(&gui.lock);
                        record_stream = gui.record_file;
                        LeaveCriticalSection(&gui.lock);
                        if (record_stream != NULL &&
                            fwrite(buffer->samples, sizeof(buffer->samples[0]),
                                   GUI_BLOCK_SAMPLES, record_stream) !=
                            GUI_BLOCK_SAMPLES) {
                            gui_set_status("Recording write failed");
                            gui_finish_recording();
                        } else if (record_stream != NULL) {
                            EnterCriticalSection(&gui.lock);
                            gui.recorded_samples += GUI_BLOCK_SAMPLES;
                            LeaveCriticalSection(&gui.lock);
                        }
                    }
                    EnterCriticalSection(&gui.lock);
                    for (sample_index = 0; sample_index < GUI_BLOCK_SAMPLES;
                         sample_index++) {
                        gui.samples[gui.ring_position++ % GUI_RING_SAMPLES] =
                            generated[sample_index];
                    }
                    gui.position = position;
                    gui.playhead = playhead;
                    gui.cpu_percent = ((double)(cpu_finish - cpu_start) /
                                       CLOCKS_PER_SEC) /
                                      ((double)GUI_BLOCK_SAMPLES / sample_rate) * 100.0;
                    LeaveCriticalSection(&gui.lock);
                    buffer->header.lpData = (LPSTR)buffer->samples;
                    buffer->header.dwBufferLength = sizeof(buffer->samples);
                    if (waveOutPrepareHeader(wave, &buffer->header,
                                             sizeof(buffer->header)) != MMSYSERR_NOERROR ||
                        waveOutWrite(wave, &buffer->header,
                                     sizeof(buffer->header)) != MMSYSERR_NOERROR) {
                        gui_set_status("Windows audio output failed");
                        break;
                    }
                    submitted = 1;
                }
                if (!submitted)
                    WaitForSingleObject(audio_event, 20);
            } else {
                WaitForSingleObject(gui.stop_event, 20);
            }
        } else {
            WaitForSingleObject(gui.stop_event, 50);
        }
        if (gui.record_stop_requested)
        EnterCriticalSection(&gui.lock);
        index = (size_t)gui.record_stop_requested;
        LeaveCriticalSection(&gui.lock);
        if (index != 0)
            gui_finish_recording();
        EnterCriticalSection(&gui.lock);
        gui.position = position;
        gui.playhead = playhead;
        LeaveCriticalSection(&gui.lock);
    }

cleanup:
    gui_close_audio(&wave, audio_event, buffers);
    if (gui.record_file != NULL)
    if (gui_recording_active())
        gui_finish_recording();
    JS_FreeValue(context, function);
    if (context != NULL)
        free_math_bindings(context, &math);
    if (context != NULL)
        JS_FreeContext(context);
    if (runtime != NULL)
        JS_FreeRuntime(runtime);
    if (audio_event != NULL)
        CloseHandle(audio_event);
    return 0;
}

static HWND gui_create_control(HWND parent, const char *class_name,
                               const char *text, DWORD style, int identifier)
{
    return CreateWindowExA(0, class_name, text,
                           WS_CHILD | WS_VISIBLE | style,
                           0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)identifier,
                           GetModuleHandleA(NULL), NULL);
}

static void gui_layout(HWND window)
{
    RECT client;
    int width;
    int height;
    int formula_height;
    int first_row;
    int second_row;
    int x;
    GetClientRect(window, &client);
    width = client.right;
    height = client.bottom;
    formula_height = height * 46 / 100;
    if (formula_height < 150)
        formula_height = 150;
    if (formula_height > height - 150)
        formula_height = height - 150;
    if (formula_height < 60)
        formula_height = 60;
    first_row = formula_height + 5;
    second_row = first_row + 34;
    MoveWindow(gui.formula, 4, 4, width - 8, formula_height - 8, TRUE);
    x = 4;
#define GUI_PLACE(control, control_width) \
    MoveWindow((control), x, first_row, (control_width), 28, TRUE); \
    x += (control_width) + 3
    GUI_PLACE(gui.time_mode, 38);
    GUI_PLACE(gui.time, 112);
    GUI_PLACE(gui.cpu, 72);
    GUI_PLACE(GetDlgItem(window, ID_STOP), 48);
    GUI_PLACE(GetDlgItem(window, ID_RESET), 48);
    GUI_PLACE(GetDlgItem(window, ID_BACK), 48);
    GUI_PLACE(GetDlgItem(window, ID_REVERSE), 44);
    GUI_PLACE(GetDlgItem(window, ID_SLOWER), 34);
    GUI_PLACE(GetDlgItem(window, ID_PAUSE), 52);
    GUI_PLACE(GetDlgItem(window, ID_FASTER), 34);
    GUI_PLACE(GetDlgItem(window, ID_ZOOM_OUT), 30);
    GUI_PLACE(GetDlgItem(window, ID_ZOOM_VALUE), 50);
    GUI_PLACE(GetDlgItem(window, ID_ZOOM_IN), 30);
    GUI_PLACE(gui.display, 130);
#undef GUI_PLACE
    x = 4;
    MoveWindow(gui.mode_combo, x, second_row, 132, 28, TRUE);
    x += 135;
    MoveWindow(gui.sample_rate_combo, x, second_row, 118, 28, TRUE);
    x += 121;
    MoveWindow(GetDlgItem(window, ID_DOWNSAMPLE), x, second_row, 64, 28, TRUE);
    x += 67;
    MoveWindow(gui.record, x, second_row, 76, 28, TRUE);
    x += 79;
    MoveWindow(gui.bytes, x, second_row, 90, 28, TRUE);
    x += 94;
    MoveWindow(gui.volume, x, second_row, 150, 28, TRUE);
    MoveWindow(gui.status, x + 154, second_row, width - x - 158, 28, TRUE);
    MoveWindow(GetDlgItem(window, ID_STATUS), 0, second_row, 1, 1, FALSE);
    InvalidateRect(window, NULL, TRUE);
}

static void gui_update_formula(void)
{
    int length = GetWindowTextLengthA(gui.formula);
    if (length >= GUI_FORMULA_CAPACITY)
        length = GUI_FORMULA_CAPACITY - 1;
    EnterCriticalSection(&gui.lock);
    GetWindowTextA(gui.formula, gui.source, length + 1);
    gui.source_revision++;
    LeaveCriticalSection(&gui.lock);
    SetWindowTextA(gui.bytes, "");
    {
        char text[64];
        snprintf(text, sizeof(text), "%d B", length);
        SetWindowTextA(gui.bytes, text);
    }
}

static void gui_update_time_display(void)
{
    uint64_t position;
    uint32_t sample_rate;
    int seconds;
    char text[64];
    if (GetFocus() == gui.time)
        return;
    EnterCriticalSection(&gui.lock);
    position = gui.position;
    sample_rate = gui.sample_rate;
    LeaveCriticalSection(&gui.lock);
    seconds = (int)(SendMessageA(gui.time_mode, BM_GETCHECK, 0, 0) == BST_CHECKED);
        SetWindowTextA(gui.time_mode, seconds ? "s" : "t");
    if (seconds)
        snprintf(text, sizeof(text), "%.3f", (double)position / sample_rate);
    else
        snprintf(text, sizeof(text), "%llu", (unsigned long long)position);
    SetWindowTextA(gui.time, text);
}

static void gui_seek_from_time(void)
{
    char text[96];
    char *end;
    double value;
    uint32_t sample_rate;
    int seconds;
    GetWindowTextA(gui.time, text, sizeof(text));
    value = strtod(text, &end);
    if (end == text || !isfinite(value) || value < 0)
        value = 0;
    EnterCriticalSection(&gui.lock);
    sample_rate = gui.sample_rate;
    seconds = (int)(SendMessageA(gui.time_mode, BM_GETCHECK, 0, 0) == BST_CHECKED);
    gui.seek_position = seconds ? (uint64_t)(value * sample_rate) : (uint64_t)value;
    gui.seek_requested = 1;
    gui.time_focused = 0;
    LeaveCriticalSection(&gui.lock);
}

static void gui_start_recording(HWND window)
{
    char path[MAX_PATH];
    char downloads[MAX_PATH];
    OPENFILENAMEA dialog;
    FILE *stream;
    DWORD length = GetEnvironmentVariableA("USERPROFILE", downloads,
                                           sizeof(downloads));
    if (length > 0 && length < sizeof(downloads))
        snprintf(path, sizeof(path), "%s\\Downloads\\bytebeat.wav", downloads);
    else
        snprintf(path, sizeof(path), "bytebeat.wav");
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = "WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrDefExt = "wav";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&dialog))
        return;
    stream = fopen(path, "wb+");
    if (stream == NULL) {
        MessageBoxA(window, "The recording file could not be opened.",
                    "bytebeat", MB_ICONERROR | MB_OK);
        return;
    }
    gui_write_wav_header(stream, gui.sample_rate, 0);
    EnterCriticalSection(&gui.lock);
    gui.record_file = stream;
    gui.recorded_samples = 0;
    gui.recorded_sample_rate = gui.sample_rate;
    gui.record_stop_requested = 0;
    LeaveCriticalSection(&gui.lock);
    SetWindowTextA(gui.record, "Stop rec");
}

static void gui_draw_visualizer(HWND window, HDC dc)
{
    RECT client;
    RECT area;
    HPEN pen;
    HPEN dot_pen;
    HBRUSH brush;
    float samples[GUI_RING_SAMPLES];
    uint32_t ring_position;
    int display_mode;
    int zoom_exponent;
    int width;
    int height;
    int center;
    int index;
    int previous_y = 0;
    GetClientRect(window, &client);
    EnterCriticalSection(&gui.lock);
    memcpy(samples, gui.samples, sizeof(samples));
    ring_position = gui.ring_position;
    display_mode = gui.display_mode;
    zoom_exponent = gui.zoom_exponent;
    LeaveCriticalSection(&gui.lock);
    area.left = 4;
    area.right = client.right - 4;
    area.top = client.bottom * 46 / 100 + 72;
    area.bottom = client.bottom - 4;
    if (area.top >= area.bottom)
        area.top = client.bottom / 2;
    width = area.right - area.left;
    height = area.bottom - area.top;
    center = area.top + height / 2;
    brush = CreateSolidBrush(RGB(8, 9, 11));
    FillRect(dc, &area, brush);
    DeleteObject(brush);
    pen = CreatePen(PS_SOLID, 1, RGB(130, 145, 160));
    dot_pen = CreatePen(PS_SOLID, 2, RGB(120, 210, 190));
    SelectObject(dc, pen);
    MoveToEx(dc, area.left, center, NULL);
    LineTo(dc, area.right, center);
    for (index = 0; index < GUI_RING_SAMPLES; index++) {
        int source_index = (int)((ring_position + index) % GUI_RING_SAMPLES);
        int x = area.left + (index * (width - 1)) / (GUI_RING_SAMPLES - 1);
        double scale = (double)(1U << (zoom_exponent > 20 ? 20 : zoom_exponent));
        int y = center - (int)(samples[source_index] * height * 0.45 * scale);
        if (y < area.top)
            y = area.top;
        if (y >= area.bottom)
            y = area.bottom - 1;
        if (display_mode == 0) {
            SelectObject(dc, dot_pen);
            SetPixelV(dc, x, y, RGB(120, 210, 190));
            SelectObject(dc, pen);
        } else if (display_mode == 2) {
            if (index == 0)
                MoveToEx(dc, x, y, NULL);
            else {
                LineTo(dc, x, previous_y);
                LineTo(dc, x, y);
            }
        } else if (index == 0) {
            MoveToEx(dc, x, y, NULL);
        } else {
            LineTo(dc, x, y);
        }
        if (display_mode == 3)
            SetPixelV(dc, x, y, RGB(120, 210, 190));
        previous_y = y;
    }
    DeleteObject(dot_pen);
    DeleteObject(pen);
}

static LRESULT CALLBACK gui_window_proc(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX controls;
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_BAR_CLASSES | ICC_UPDOWN_CLASS;
        InitCommonControlsEx(&controls);
        gui.window = window;
        InitializeCriticalSection(&gui.lock);
        gui.background_brush = CreateSolidBrush(RGB(28, 30, 34));
        gui.formula = gui_create_control(window, "EDIT", "",
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_BORDER,
            ID_FORMULA);
        SendMessageA(gui.formula, EM_SETLIMITTEXT, GUI_FORMULA_CAPACITY - 1, 0);
        gui.time_mode = gui_create_control(window, "BUTTON", "t",
            BS_AUTOCHECKBOX | BS_PUSHLIKE, ID_TIME_MODE);
        gui.time = gui_create_control(window, "EDIT", "0",
            ES_AUTOHSCROLL | ES_RIGHT | WS_BORDER, ID_TIME);
        gui.cpu = gui_create_control(window, "STATIC", "CPU 0%", SS_CENTER,
                                     ID_CPU);
        gui_create_control(window, "BUTTON", "Stop", BS_PUSHBUTTON, ID_STOP);
        gui_create_control(window, "BUTTON", "Reset", BS_PUSHBUTTON, ID_RESET);
        gui_create_control(window, "BUTTON", "Back", BS_PUSHBUTTON, ID_BACK);
        gui_create_control(window, "BUTTON", "Rev", BS_PUSHBUTTON, ID_REVERSE);
        gui_create_control(window, "BUTTON", "-", BS_PUSHBUTTON, ID_SLOWER);
        gui_create_control(window, "BUTTON", "Pause", BS_PUSHBUTTON, ID_PAUSE);
        gui_create_control(window, "BUTTON", "+", BS_PUSHBUTTON, ID_FASTER);
        gui_create_control(window, "BUTTON", "-", BS_PUSHBUTTON, ID_ZOOM_OUT);
        gui_create_control(window, "STATIC", "1x", SS_CENTER, ID_ZOOM_VALUE);
        gui_create_control(window, "BUTTON", "+", BS_PUSHBUTTON, ID_ZOOM_IN);
        gui.display = gui_create_control(window, "COMBOBOX", "",
            CBS_DROPDOWNLIST | WS_VSCROLL, ID_DISPLAY);
        SendMessageA(gui.display, CB_ADDSTRING, 0, (LPARAM)"Points");
        SendMessageA(gui.display, CB_ADDSTRING, 0, (LPARAM)"Waveform");
        SendMessageA(gui.display, CB_ADDSTRING, 0, (LPARAM)"Diagram");
        SendMessageA(gui.display, CB_ADDSTRING, 0, (LPARAM)"Combined");
        SendMessageA(gui.display, CB_SETCURSEL, 1, 0);
        gui.mode_combo = gui_create_control(window, "COMBOBOX", "",
            CBS_DROPDOWNLIST | WS_VSCROLL, ID_MODE);
        SendMessageA(gui.mode_combo, CB_ADDSTRING, 0, (LPARAM)"Bytebeat");
        SendMessageA(gui.mode_combo, CB_ADDSTRING, 0, (LPARAM)"Signed bytebeat");
        SendMessageA(gui.mode_combo, CB_ADDSTRING, 0, (LPARAM)"Floatbeat");
        SendMessageA(gui.mode_combo, CB_ADDSTRING, 0, (LPARAM)"Funcbeat");
        SendMessageA(gui.mode_combo, CB_SETCURSEL, 0, 0);
        gui.sample_rate_combo = gui_create_control(window, "COMBOBOX", "",
            CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, ID_SAMPLE_RATE);
        {
            static const char *rates[] = {
                "8000", "11025", "16000", "22050", "32000", "44100", "48000"
            };
            size_t index;
            for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
                SendMessageA(gui.sample_rate_combo, CB_ADDSTRING, 0, (LPARAM)rates[index]);
        }
        SendMessageA(gui.sample_rate_combo, CB_SETCURSEL, 0, 0);
        gui.downsample = gui_create_control(window, "STATIC", "Step 1",
            SS_CENTER, ID_DOWNSAMPLE);
        EnableWindow(gui.downsample, FALSE);
        gui.record = gui_create_control(window, "BUTTON", "Record",
            BS_PUSHBUTTON, ID_RECORD);
        gui.bytes = gui_create_control(window, "STATIC", "0 B", SS_CENTER,
                                       ID_BYTES);
        gui.volume = gui_create_control(window, TRACKBAR_CLASSA, "",
            TBS_AUTOTICKS, ID_VOLUME);
        SendMessageA(gui.volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageA(gui.volume, TBM_SETPOS, TRUE, 50);
        gui.status = gui_create_control(window, "STATIC", "Formula ready",
                                        SS_LEFT, ID_STATUS);
        gui_create_control(window, "STATIC", "", 0, ID_STATUS + 1);
        gui.sample_rate = 8000;
        gui.mode = MODE_UNSIGNED;
        gui.volume_percent = 50;
        gui.playing = 1;
        gui.zoom_exponent = 0;
        gui.display_mode = 1;
        gui.reverse = 0;
        gui.speed_exponent = 0;
        snprintf(gui.source, sizeof(gui.source),
                 "10*(t>>7|t>>6)+4*(t&t>>13|t>>6)");
        SetWindowTextA(gui.formula, gui.source);
        gui.source_revision = 1;
        gui.stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        gui.worker = CreateThread(NULL, 0, gui_audio_worker, NULL, 0, NULL);
        SetTimer(window, 1, 100, NULL);
        SetTimer(window, 2, 250, NULL);
        gui_layout(window);
        return 0;
    }
    case WM_SIZE:
        gui_layout(window);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_FORMULA:
            if (HIWORD(wparam) == EN_CHANGE)
                SetTimer(window, 2, 250, NULL);
            break;
        case ID_TIME:
            if (HIWORD(wparam) == EN_SETFOCUS) {
                EnterCriticalSection(&gui.lock);
                gui.time_focused = 1;
                LeaveCriticalSection(&gui.lock);
            } else if (HIWORD(wparam) == EN_KILLFOCUS) {
                gui_seek_from_time();
            }
            break;
        case ID_TIME_MODE:
            gui_update_time_display();
            break;
        case ID_DISPLAY:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                EnterCriticalSection(&gui.lock);
                gui.display_mode = (int)SendMessageA(gui.display, CB_GETCURSEL, 0, 0);
                LeaveCriticalSection(&gui.lock);
                InvalidateRect(window, NULL, FALSE);
            }
            break;
        case ID_MODE:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                EnterCriticalSection(&gui.lock);
                gui.mode = (int)SendMessageA(gui.mode_combo, CB_GETCURSEL, 0, 0);
                gui.source_revision++;
                LeaveCriticalSection(&gui.lock);
            }
            break;
        case ID_SAMPLE_RATE:
            if (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_EDITCHANGE) {
                char rate_text[32];
                uint32_t rate;
                GetWindowTextA(gui.sample_rate_combo, rate_text, sizeof(rate_text));
                if (parse_positive_uint(rate_text, &rate) == 0 && rate <= 192000) {
                    EnterCriticalSection(&gui.lock);
                    gui.sample_rate = rate;
                    LeaveCriticalSection(&gui.lock);
                }
            }
            break;
        case ID_STOP:
            EnterCriticalSection(&gui.lock);
            gui.playing = 0;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_RESET:
            EnterCriticalSection(&gui.lock);
            gui.seek_position = 0;
            gui.seek_requested = 1;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_BACK: {
            uint64_t offset;
            EnterCriticalSection(&gui.lock);
            offset = (uint64_t)gui.sample_rate * 5;
            gui.seek_position = gui.position > offset ? gui.position - offset : 0;
            gui.seek_requested = 1;
            LeaveCriticalSection(&gui.lock);
            break;
        }
        case ID_REVERSE:
            EnterCriticalSection(&gui.lock);
            gui.reverse = !gui.reverse;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_SLOWER:
            EnterCriticalSection(&gui.lock);
            if (gui.speed_exponent > -4)
                gui.speed_exponent--;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_FASTER:
            EnterCriticalSection(&gui.lock);
            if (gui.speed_exponent < 4)
                gui.speed_exponent++;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_PAUSE:
            EnterCriticalSection(&gui.lock);
            gui.playing = !gui.playing;
            LeaveCriticalSection(&gui.lock);
            break;
        case ID_ZOOM_OUT:
            EnterCriticalSection(&gui.lock);
            if (gui.zoom_exponent > 0)
                gui.zoom_exponent--;
            LeaveCriticalSection(&gui.lock);
            InvalidateRect(window, NULL, FALSE);
            break;
        case ID_ZOOM_IN:
            EnterCriticalSection(&gui.lock);
            if (gui.zoom_exponent < 20)
                gui.zoom_exponent++;
            LeaveCriticalSection(&gui.lock);
            InvalidateRect(window, NULL, FALSE);
            break;
        case ID_RECORD:
            if (gui_recording_active()) {
                EnterCriticalSection(&gui.lock);
                gui.record_stop_requested = 1;
                LeaveCriticalSection(&gui.lock);
            } else {
                gui_start_recording(window);
            }
            break;
        }
        return 0;
    case WM_HSCROLL:
        if ((HWND)lparam == gui.volume) {
            EnterCriticalSection(&gui.lock);
            gui.volume_percent = (int)SendMessageA(gui.volume, TBM_GETPOS, 0, 0);
            LeaveCriticalSection(&gui.lock);
        }
        return 0;
    case WM_TIMER:
        if (wparam == 2) {
            KillTimer(window, 2);
            gui_update_formula();
        } else {
            char status[256];
            double cpu;
            int zoom;
            EnterCriticalSection(&gui.lock);
            snprintf(status, sizeof(status), "%s", gui.status_text);
            cpu = gui.cpu_percent;
            zoom = gui.zoom_exponent;
            LeaveCriticalSection(&gui.lock);
            {
                char cpu_text[32];
                char zoom_text[32];
                snprintf(cpu_text, sizeof(cpu_text), "CPU %.0f%%", cpu);
                SetWindowTextA(gui.cpu, cpu_text);
                if (zoom == 0)
                    snprintf(zoom_text, sizeof(zoom_text), "1x");
                else
                    snprintf(zoom_text, sizeof(zoom_text), "2^-%d", zoom);
                SetWindowTextA(GetDlgItem(window, ID_ZOOM_VALUE), zoom_text);
                SetWindowTextA(gui.status, status);
            }
            gui_update_time_display();
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wparam;
        SetTextColor(dc, RGB(224, 228, 234));
        SetBkColor(dc, RGB(28, 30, 34));
        return (LRESULT)gui.background_brush;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        gui_draw_visualizer(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        KillTimer(window, 2);
        if (gui.stop_event != NULL)
            SetEvent(gui.stop_event);
        if (gui.worker != NULL) {
            WaitForSingleObject(gui.worker, INFINITE);
            CloseHandle(gui.worker);
        }
        if (gui.stop_event != NULL)
            CloseHandle(gui.stop_event);
        DeleteObject(gui.background_brush);
        DeleteCriticalSection(&gui.lock);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show_command)
{
    WNDCLASSA window_class;
    HWND window;
    MSG message;
    (void)previous;
    (void)command_line;
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = gui_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = "BytebeatNativePlayer";
    if (!RegisterClassA(&window_class))
        return EXIT_FAILURE;
    window = CreateWindowExA(0, window_class.lpszClassName,
                             "bytebeat | native player",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1050, 720,
                             NULL, NULL, instance, NULL);
    if (window == NULL)
        return EXIT_FAILURE;
    ShowWindow(window, show_command);
    UpdateWindow(window);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return (int)message.wParam;
}

#else

int main(int argc, char **argv)
{
    Options options;
    FILE *stream = stdout;
    JSRuntime *runtime = NULL;
    JSContext *context = NULL;
    JSValue function = JS_UNDEFINED;
    InterruptState interrupt_state;
    MathBindings math;
    size_t math_index;
    uint64_t sample_count;
    uint64_t index;
    int parse_result;
    int status = EXIT_FAILURE;

    for (math_index = 0; math_index < BIND_COUNT; math_index++)
        math.values[math_index] = JS_UNDEFINED;

    parse_result = parse_options(argc, argv, &options);
    if (parse_result != 0)
        return parse_result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    if (options.duration > (double)UINT64_MAX / options.sample_rate) {
        fprintf(stderr, "error: duration produces too many samples\n");
        return EXIT_FAILURE;
    }
    sample_count = (uint64_t)(options.duration * options.sample_rate);

    if (options.output_path != NULL) {
        stream = fopen(options.output_path, "wb");
        if (stream == NULL) {
            fprintf(stderr, "error: cannot open output '%s': %s\n", options.output_path,
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }
#ifdef _WIN32
    else {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    if (options.strict) {
        if (write_wav_header(stream, &options, sample_count) < 0)
            goto cleanup;
        for (index = 0; index < sample_count; index++) {
            if (write_strict_sample(stream, &options, index) < 0)
                goto cleanup;
        }
        if (fflush(stream) != 0) {
            fprintf(stderr, "error: failed to flush output\n");
            goto cleanup;
        }
        status = EXIT_SUCCESS;
        goto cleanup;
    }

    runtime = JS_NewRuntime();
    context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    if (runtime == NULL || context == NULL) {
        fprintf(stderr, "error: could not create JavaScript runtime\n");
        goto cleanup;
    }
    JS_SetMemoryLimit(runtime, 64 * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 1024 * 1024);
    interrupt_state.deadline = (uint64_t)clock() + (uint64_t)CLOCKS_PER_SEC * 30;
    JS_SetInterruptHandler(runtime, interrupt_handler, &interrupt_state);
    if (create_math_bindings(context, &math) < 0) {
        fprintf(stderr, "error: could not create math bindings\n");
        goto cleanup;
    }
    if (options.speedtest) {
        status = run_speedtest(context, &options, &math) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        goto cleanup;
    }
    if (options.mode == MODE_FUNCBEAT) {
        function = evaluate_funcbeat(context, options.source, &math);
    } else {
        function = evaluate_function(context, &options);
    }
    if (JS_IsException(function)) {
        print_exception(context, "could not evaluate source");
        goto cleanup;
    }
    if (!JS_IsFunction(context, function)) {
        fprintf(stderr, "error: source did not produce a function\n");
        goto cleanup;
    }
    if (write_wav_header(stream, &options, sample_count) < 0) {
        fprintf(stderr, "error: failed to write WAV header\n");
        goto cleanup;
    }
    for (index = 0; index < sample_count; index++) {
        if (write_sample(stream, options.mode, context, function, &math, index, options.sample_rate) < 0)
            goto cleanup;
    }
    if (fflush(stream) != 0) {
        fprintf(stderr, "error: failed to flush output\n");
        goto cleanup;
    }
    status = EXIT_SUCCESS;

cleanup:
    if (context != NULL)
        free_math_bindings(context, &math);
    JS_FreeValue(context, function);
    if (context != NULL)
        JS_FreeContext(context);
    if (runtime != NULL)
        JS_FreeRuntime(runtime);
    if (stream != stdout)
        fclose(stream);
    return status;
}

#endif