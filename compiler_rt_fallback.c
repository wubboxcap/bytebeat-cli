#include <stdint.h>

typedef unsigned __int128 uint128_t;

static uint128_t divide_unsigned(uint128_t dividend, uint128_t divisor,
                                 uint128_t *remainder)
{
    uint64_t dividend_words[2] = {(uint64_t)dividend, (uint64_t)(dividend >> 64)};
    uint64_t divisor_words[2] = {(uint64_t)divisor, (uint64_t)(divisor >> 64)};
    uint64_t quotient_words[2] = {0, 0};
    uint64_t remainder_words[2] = {0, 0};
    int bit;

    if (divisor == 0) {
        *remainder = 0;
        return 0;
    }
    for (bit = 127; bit >= 0; bit--) {
        uint64_t incoming = (dividend_words[bit / 64] >> (bit % 64)) & 1;
        uint64_t carry = incoming;
        int word;
        for (word = 0; word < 2; word++) {
            uint64_t next = remainder_words[word] >> 63;
            remainder_words[word] = (remainder_words[word] << 1) | carry;
            carry = next;
        }
        if (remainder_words[1] > divisor_words[1] ||
            (remainder_words[1] == divisor_words[1] &&
             remainder_words[0] >= divisor_words[0])) {
            uint64_t borrow = remainder_words[0] < divisor_words[0];
            remainder_words[0] -= divisor_words[0];
            remainder_words[1] -= divisor_words[1] + borrow;
            quotient_words[bit / 64] |= (uint64_t)1 << (bit % 64);
        }
    }
    *remainder = (uint128_t)remainder_words[0] | ((uint128_t)remainder_words[1] << 64);
    return (uint128_t)quotient_words[0] | ((uint128_t)quotient_words[1] << 64);
}

uint128_t __udivti3(uint128_t dividend, uint128_t divisor)
{
    uint128_t remainder;
    return divide_unsigned(dividend, divisor, &remainder);
}

uint128_t __umodti3(uint128_t dividend, uint128_t divisor)
{
    uint128_t remainder;
    divide_unsigned(dividend, divisor, &remainder);
    return remainder;
}

uint128_t __udivmodti4(uint128_t dividend, uint128_t divisor,
                       uint128_t *remainder)
{
    return divide_unsigned(dividend, divisor, remainder);
}