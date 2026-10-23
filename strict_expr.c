#include "strict_expr.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TOKEN_END, TOKEN_NUMBER, TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_OPERATOR };

typedef struct {
    char name[16];
    double value;
} Variable;

typedef struct {
    int kind;
    char text[16];
    double number;
    const char *string;
    size_t string_length;
} Token;

typedef struct {
    double number;
    const char *string;
    size_t string_length;
    int variable;
} Value;

typedef struct {
    const char *source;
    const char *cursor;
    Token token;
    Variable variables[128];
    size_t variable_count;
    double t;
    char *error;
    unsigned long error_size;
    int failed;
} Parser;

static void set_error(Parser *parser, const char *message)
{
    if (!parser->failed && parser->error != NULL && parser->error_size > 0)
        snprintf(parser->error, parser->error_size, "%s at byte %lu", message,
                 (unsigned long)(parser->cursor - parser->source));
    parser->failed = 1;
}

static void next_token(Parser *parser)
{
    static const char *operators[] = {
        "<<=", ">>=", "++", "--", "+=", "-=", "*=", "/=", "%=",
        "&=", "|=", "^=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||"
    };
    size_t index;
    const char *start;

    while (isspace((unsigned char)*parser->cursor))
        parser->cursor++;
    memset(&parser->token, 0, sizeof(parser->token));
    start = parser->cursor;
    if (*start == '\0') {
        parser->token.kind = TOKEN_END;
        return;
    }
    if (isalpha((unsigned char)*start) || *start == '_') {
        size_t length = 0;
        parser->token.kind = TOKEN_IDENTIFIER;
        while (isalnum((unsigned char)*parser->cursor) || *parser->cursor == '_') {
            if (length + 1 < sizeof(parser->token.text))
                parser->token.text[length++] = *parser->cursor;
            parser->cursor++;
        }
        parser->token.text[length] = '\0';
        return;
    }
    if (isdigit((unsigned char)*start) || (*start == '.' && isdigit((unsigned char)start[1]))) {
        char *end;
        parser->token.kind = TOKEN_NUMBER;
        parser->token.number = strtod(start, &end);
        if (end == start) {
            set_error(parser, "invalid number");
            return;
        }
        parser->cursor = end;
        return;
    }
    if (*start == '\'' || *start == '"') {
        char quote = *start++;
        parser->cursor = start;
        parser->token.kind = TOKEN_STRING;
        parser->token.string = start;
        while (*parser->cursor != '\0' && *parser->cursor != quote) {
            if (*parser->cursor == '\\' && parser->cursor[1] != '\0')
                parser->cursor++;
            parser->cursor++;
        }
        if (*parser->cursor != quote) {
            set_error(parser, "unterminated string literal");
            return;
        }
        parser->token.string_length = (size_t)(parser->cursor - start);
        parser->cursor++;
        return;
    }
    for (index = 0; index < sizeof(operators) / sizeof(operators[0]); index++) {
        size_t length = strlen(operators[index]);
        if (strncmp(start, operators[index], length) == 0) {
            parser->token.kind = TOKEN_OPERATOR;
            memcpy(parser->token.text, operators[index], length + 1);
            parser->cursor += length;
            return;
        }
    }
    if (strchr("+-*/%&|^~!<>=?:(),[]", *start) != NULL) {
        parser->token.kind = TOKEN_OPERATOR;
        parser->token.text[0] = *start;
        parser->token.text[1] = '\0';
        parser->cursor++;
        return;
    }
    set_error(parser, "unsupported token");
}

static int find_variable(Parser *parser, const char *name, int create)
{
    size_t index;
    for (index = 0; index < parser->variable_count; index++) {
        if (strcmp(parser->variables[index].name, name) == 0)
            return (int)index;
    }
    if (!create || parser->variable_count >= sizeof(parser->variables) / sizeof(parser->variables[0]))
        return -1;
    snprintf(parser->variables[parser->variable_count].name,
             sizeof(parser->variables[parser->variable_count].name), "%s", name);
    parser->variables[parser->variable_count].value = 0;
    return (int)parser->variable_count++;
}

static Value number_value(double number)
{
    Value value = {number, NULL, 0, -1};
    return value;
}

static int precedence(const char *operator)
{
    if (strcmp(operator, ",") == 0) return 1;
    if (strcmp(operator, "=") == 0 || strcmp(operator, "+=") == 0 ||
        strcmp(operator, "-=") == 0 || strcmp(operator, "*=") == 0 ||
        strcmp(operator, "/=") == 0 || strcmp(operator, "%=") == 0 ||
        strcmp(operator, "&=") == 0 || strcmp(operator, "|=") == 0 ||
        strcmp(operator, "^=") == 0 || strcmp(operator, "<<=") == 0 ||
        strcmp(operator, ">>=") == 0) return 2;
    if (strcmp(operator, "||") == 0) return 4;
    if (strcmp(operator, "&&") == 0) return 5;
    if (strcmp(operator, "|") == 0) return 6;
    if (strcmp(operator, "^") == 0) return 7;
    if (strcmp(operator, "&") == 0) return 8;
    if (strcmp(operator, "==") == 0 || strcmp(operator, "!=") == 0) return 9;
    if (strcmp(operator, "<") == 0 || strcmp(operator, "<=") == 0 ||
        strcmp(operator, ">") == 0 || strcmp(operator, ">=") == 0) return 10;
    if (strcmp(operator, "<<") == 0 || strcmp(operator, ">>") == 0) return 11;
    if (strcmp(operator, "+") == 0 || strcmp(operator, "-") == 0) return 12;
    if (strcmp(operator, "*") == 0 || strcmp(operator, "/") == 0 ||
        strcmp(operator, "%") == 0) return 13;
    return 0;
}

static Value parse_expression(Parser *parser, int minimum_precedence);

static Value call_function(Parser *parser, const char *name, Value *args, int argc)
{
    double first = argc > 0 ? args[0].number : 0;
    double second = argc > 1 ? args[1].number : 0;
    int index;
    if (strcmp(name, "abs") == 0 || strcmp(name, "fabs") == 0) return number_value(fabs(first));
    if (strcmp(name, "acos") == 0) return number_value(acos(first));
    if (strcmp(name, "acosh") == 0) return number_value(acosh(first));
    if (strcmp(name, "asin") == 0) return number_value(asin(first));
    if (strcmp(name, "asinh") == 0) return number_value(asinh(first));
    if (strcmp(name, "atan") == 0) return number_value(atan(first));
    if (strcmp(name, "atan2") == 0 && argc >= 2) return number_value(atan2(first, second));
    if (strcmp(name, "atanh") == 0) return number_value(atanh(first));
    if (strcmp(name, "cbrt") == 0) return number_value(cbrt(first));
    if (strcmp(name, "ceil") == 0) return number_value(ceil(first));
    if (strcmp(name, "cos") == 0) return number_value(cos(first));
    if (strcmp(name, "cosh") == 0) return number_value(cosh(first));
    if (strcmp(name, "exp") == 0) return number_value(exp(first));
    if (strcmp(name, "expm1") == 0) return number_value(expm1(first));
    if (strcmp(name, "floor") == 0) return number_value(floor(first));
    if (strcmp(name, "fmod") == 0 && argc >= 2) return number_value(fmod(first, second));
    if (strcmp(name, "log") == 0) return number_value(log(first));
    if (strcmp(name, "log10") == 0) return number_value(log10(first));
    if (strcmp(name, "log2") == 0) return number_value(log2(first));
    if (strcmp(name, "max") == 0 || strcmp(name, "fmax") == 0) {
        for (index = 1; index < argc; index++) first = fmax(first, args[index].number);
        return number_value(first);
    }
    if (strcmp(name, "min") == 0 || strcmp(name, "fmin") == 0) {
        for (index = 1; index < argc; index++) first = fmin(first, args[index].number);
        return number_value(first);
    }
    if (strcmp(name, "pow") == 0 && argc >= 2) return number_value(pow(first, second));
    if (strcmp(name, "round") == 0) return number_value(round(first));
    if (strcmp(name, "sin") == 0) return number_value(sin(first));
    if (strcmp(name, "sinh") == 0) return number_value(sinh(first));
    if (strcmp(name, "sqrt") == 0) return number_value(sqrt(first));
    if (strcmp(name, "tan") == 0) return number_value(tan(first));
    if (strcmp(name, "tanh") == 0) return number_value(tanh(first));
    if (strcmp(name, "trunc") == 0) return number_value(trunc(first));
    set_error(parser, "unsupported C math function or invalid arguments");
    return number_value(0);
}

static Value parse_primary(Parser *parser)
{
    Value value = number_value(0);
    if (parser->token.kind == TOKEN_NUMBER) {
        value = number_value(parser->token.number);
        next_token(parser);
    } else if (parser->token.kind == TOKEN_STRING) {
        value.string = parser->token.string;
        value.string_length = parser->token.string_length;
        next_token(parser);
    } else if (parser->token.kind == TOKEN_IDENTIFIER) {
        char name[sizeof(parser->token.text)];
        int variable;
        memcpy(name, parser->token.text, sizeof(name));
        next_token(parser);
        if (strcmp(name, "t") == 0) {
            value = number_value(parser->t);
            value.variable = -2;
        } else if (strcmp(name, "PI") == 0) value = number_value(3.14159265358979323846);
        else if (strcmp(name, "E") == 0) value = number_value(2.71828182845904523536);
        else if (strcmp(name, "INFINITY") == 0 || strcmp(name, "HUGE_VAL") == 0)
            value = number_value(INFINITY);
        else if (strcmp(name, "NAN") == 0) value = number_value(NAN);
        else if (strcmp(name, "true") == 0) value = number_value(1);
        else if (strcmp(name, "false") == 0) value = number_value(0);
        else if (strcmp(parser->token.text, "(") == 0) {
            Value args[32];
            int argc = 0;
            next_token(parser);
            if (strcmp(parser->token.text, ")") != 0) {
                do {
                    if (argc == 32) {
                        set_error(parser, "too many function arguments");
                        break;
                    }
                    args[argc++] = parse_expression(parser, 2);
                    if (strcmp(parser->token.text, ",") != 0)
                        break;
                    next_token(parser);
                } while (!parser->failed);
            }
            if (strcmp(parser->token.text, ")") != 0)
                set_error(parser, "expected ')' after function arguments");
            else
                next_token(parser);
            value = call_function(parser, name, args, argc);
        } else {
            variable = find_variable(parser, name, 1);
            if (variable < 0)
                set_error(parser, "too many C expression variables");
            else {
                value = number_value(parser->variables[variable].value);
                value.variable = variable;
            }
        }
    } else if (strcmp(parser->token.text, "(") == 0) {
        next_token(parser);
        value = parse_expression(parser, 1);
        if (strcmp(parser->token.text, ")") != 0)
            set_error(parser, "expected ')' ");
        else
            next_token(parser);
    } else {
        set_error(parser, "expected C expression");
        return value;
    }

    while (!parser->failed && strcmp(parser->token.text, "[") == 0) {
        Value subscript;
        next_token(parser);
        subscript = parse_expression(parser, 1);
        if (strcmp(parser->token.text, "]") != 0) {
            set_error(parser, "expected ']' after subscript");
            break;
        }
        next_token(parser);
        if (value.string == NULL) {
            set_error(parser, "strict C expression supports indexing string literals only");
            break;
        }
        {
            int64_t position = (int64_t)subscript.number;
            if (position < 0 || (uint64_t)position >= value.string_length)
                value = number_value(0);
            else
                value = number_value((unsigned char)value.string[position]);
        }
    }
    return value;
}

static Value parse_unary(Parser *parser)
{
    char operation[16];
    Value value;
    if (parser->token.kind == TOKEN_OPERATOR &&
        (strcmp(parser->token.text, "+") == 0 || strcmp(parser->token.text, "-") == 0 ||
         strcmp(parser->token.text, "!") == 0 || strcmp(parser->token.text, "~") == 0 ||
         strcmp(parser->token.text, "++") == 0 || strcmp(parser->token.text, "--") == 0)) {
        memcpy(operation, parser->token.text, sizeof(operation));
        next_token(parser);
        value = parse_unary(parser);
        if (strcmp(operation, "+") == 0) return number_value(+value.number);
        if (strcmp(operation, "-") == 0) return number_value(-value.number);
        if (strcmp(operation, "!") == 0) return number_value(!value.number);
        if (strcmp(operation, "~") == 0) return number_value((double)~(int64_t)value.number);
        if (value.variable < 0) set_error(parser, "increment requires a variable");
        else {
            parser->variables[value.variable].value += strcmp(operation, "++") == 0 ? 1 : -1;
            value.number = parser->variables[value.variable].value;
        }
        return value;
    }
    return parse_primary(parser);
}

static int is_assignment_operator(const char *operation)
{
    return strcmp(operation, "=") == 0 || strcmp(operation, "+=") == 0 ||
           strcmp(operation, "-=") == 0 || strcmp(operation, "*=") == 0 ||
           strcmp(operation, "/=") == 0 || strcmp(operation, "%=") == 0 ||
           strcmp(operation, "&=") == 0 || strcmp(operation, "|=") == 0 ||
           strcmp(operation, "^=") == 0 || strcmp(operation, "<<=") == 0 ||
           strcmp(operation, ">>=") == 0;
}

static Value apply_operator(Parser *parser, const char *operation, Value left, Value right)
{
    int64_t first = (int64_t)left.number;
    int64_t second = (int64_t)right.number;
    if (strcmp(operation, ",") == 0) return right;
    if (is_assignment_operator(operation)) {
        double result = right.number;
        if (left.variable < 0) {
            set_error(parser, "assignment requires a variable");
            return number_value(0);
        }
        if (strcmp(operation, "+=") == 0) result = left.number + right.number;
        else if (strcmp(operation, "-=") == 0) result = left.number - right.number;
        else if (strcmp(operation, "*=") == 0) result = left.number * right.number;
        else if (strcmp(operation, "/=") == 0) result = left.number / right.number;
        else if (strcmp(operation, "%=") == 0) result = (double)(first % (second == 0 ? 1 : second));
        else if (strcmp(operation, "&=") == 0) result = (double)(first & second);
        else if (strcmp(operation, "|=") == 0) result = (double)(first | second);
        else if (strcmp(operation, "^=") == 0) result = (double)(first ^ second);
        else if (strcmp(operation, "<<=") == 0) result = (double)(first << ((unsigned)second & 63));
        else if (strcmp(operation, ">>=") == 0) result = (double)(first >> ((unsigned)second & 63));
        parser->variables[left.variable].value = result;
        return number_value(result);
    }
    if (strcmp(operation, "+") == 0) return number_value(left.number + right.number);
    if (strcmp(operation, "-") == 0) return number_value(left.number - right.number);
    if (strcmp(operation, "*") == 0) return number_value(left.number * right.number);
    if (strcmp(operation, "/") == 0) return number_value(left.number / right.number);
    if (strcmp(operation, "%") == 0) return number_value((double)(first % (second == 0 ? 1 : second)));
    if (strcmp(operation, "&") == 0) return number_value((double)(first & second));
    if (strcmp(operation, "|") == 0) return number_value((double)(first | second));
    if (strcmp(operation, "^") == 0) return number_value((double)(first ^ second));
    if (strcmp(operation, "<<") == 0) return number_value((double)(first << ((unsigned)second & 63)));
    if (strcmp(operation, ">>") == 0) return number_value((double)(first >> ((unsigned)second & 63)));
    if (strcmp(operation, "==") == 0) return number_value(left.number == right.number);
    if (strcmp(operation, "!=") == 0) return number_value(left.number != right.number);
    if (strcmp(operation, "<") == 0) return number_value(left.number < right.number);
    if (strcmp(operation, "<=") == 0) return number_value(left.number <= right.number);
    if (strcmp(operation, ">") == 0) return number_value(left.number > right.number);
    if (strcmp(operation, ">=") == 0) return number_value(left.number >= right.number);
    if (strcmp(operation, "&&") == 0) return number_value(left.number != 0 && right.number != 0);
    if (strcmp(operation, "||") == 0) return number_value(left.number != 0 || right.number != 0);
    set_error(parser, "unsupported operator");
    return number_value(0);
}

static Value parse_expression(Parser *parser, int minimum_precedence)
{
    Value left = parse_unary(parser);
    while (!parser->failed && parser->token.kind == TOKEN_OPERATOR) {
        char operation[16];
        int current_precedence = precedence(parser->token.text);
        Value right;
        if (current_precedence < minimum_precedence || current_precedence == 0)
            break;
        memcpy(operation, parser->token.text, sizeof(operation));
        next_token(parser);
        right = parse_expression(parser, current_precedence +
                                 (current_precedence == 2 ? 0 : 1));
        left = apply_operator(parser, operation, left, right);
    }
    if (!parser->failed && minimum_precedence <= 3 && strcmp(parser->token.text, "?") == 0) {
        Value yes;
        next_token(parser);
        yes = parse_expression(parser, 1);
        if (strcmp(parser->token.text, ":") != 0)
            set_error(parser, "expected ':' in conditional expression");
        else {
            Value no;
            next_token(parser);
            no = parse_expression(parser, 3);
            left = left.number != 0 ? yes : no;
        }
    }
    return left;
}

int strict_expr_evaluate(const char *source, double t, double *result,
                         char *error, unsigned long error_size)
{
    Parser parser;
    Value value;
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.cursor = source;
    parser.t = t;
    parser.error = error;
    parser.error_size = error_size;
    next_token(&parser);
    value = parse_expression(&parser, 1);
    if (!parser.failed && parser.token.kind != TOKEN_END)
        set_error(&parser, "unexpected token after C expression");
    if (parser.failed)
        return -1;
    *result = value.number;
    return 0;
}