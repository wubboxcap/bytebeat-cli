#ifndef BYTEBEAT_STRICT_EXPR_H
#define BYTEBEAT_STRICT_EXPR_H

int strict_expr_evaluate(const char *source, double t, double *result,
                         char *error, unsigned long error_size);

#endif