#ifndef PINTOS_FIXED_POINT
#define PINTOS_FIXED_POINT

#include "lib/stdint.h"
#include "lib/stdio.h"
/*
 * [31 sgn][30:14 int][13:0 point]
 * value = num / FLOAT_POINT;
 */

struct fixed32 {
  int32_t num;
};
struct fixed32 fixed32_init(int32_t);
void fixed32_print(struct fixed32);
int32_t fixed32_trunc(struct fixed32);
int32_t fixed32_round(struct fixed32);
struct fixed32 fixed32_add(struct fixed32, struct fixed32);
struct fixed32 fixed32_add_int(struct fixed32, int32_t);
struct fixed32 fixed32_sub(struct fixed32, struct fixed32);
struct fixed32 fixed32_sub_int(struct fixed32, int32_t);
struct fixed32 fixed32_mul(struct fixed32, struct fixed32);
struct fixed32 fixed32_mul_int(struct fixed32, int32_t);
struct fixed32 fixed32_div(struct fixed32, struct fixed32);
struct fixed32 fixed32_div_int(struct fixed32, int32_t);
struct fixed32 fixed32_div_int_int(int32_t, int32_t);
bool fixed32_less_than(struct fixed32, struct fixed32);

#endif