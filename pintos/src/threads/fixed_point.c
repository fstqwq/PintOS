
#include "fixed_point.h"
const int FLOAT_POINT = 1 << 14;

struct fixed32 fixed32_init(int32_t x) {
  struct fixed32 res = {x * FLOAT_POINT};
  return res;
}

void fixed32_print(struct fixed32 a) {
  int num = fixed32_round(fixed32_mul_int(a, 1000));
  if (num >= 0) {
    printf("%d.%03d", num / 1000, num % 1000);
  } else {
    printf("-%d.%03d", -num / 1000, -num % 1000);
  }
}

int32_t fixed32_trunc(struct fixed32 a) {
  return a.num / FLOAT_POINT;
}

int32_t fixed32_round(struct fixed32 a) {
  /* int div round to zero */
  return (a.num + ((a.num >= 0) ? 1 : -1) * (FLOAT_POINT >> 1)) / FLOAT_POINT;
}

struct fixed32 fixed32_add(struct fixed32 a, struct fixed32 b) {
  struct fixed32 res = {a.num + b.num};
  return res;
}

struct fixed32 fixed32_add_int(struct fixed32 a, int32_t b) {
  struct fixed32 res = {a.num + b * FLOAT_POINT};
  return res;
}

struct fixed32 fixed32_sub(struct fixed32 a, struct fixed32 b) {
  struct fixed32 res = {a.num - b.num};
  return res;
}

struct fixed32 fixed32_sub_int(struct fixed32 a, int32_t b) {
  struct fixed32 res = {a.num - b * FLOAT_POINT};
  return res;
}

struct fixed32 fixed32_mul(struct fixed32 a, struct fixed32 b) {
  struct fixed32 res = {(int64_t)a.num * b.num / FLOAT_POINT};
  return res;
}

struct fixed32 fixed32_mul_int(struct fixed32 a, int32_t b) {
  struct fixed32 res = {a.num * b};
  return res;
}

struct fixed32 fixed32_div(struct fixed32 a, struct fixed32 b) {
  struct fixed32 res = {(int64_t)a.num * FLOAT_POINT / b.num};
  return res;
}

struct fixed32 fixed32_div_int(struct fixed32 a, int32_t b) {
  struct fixed32 res = {a.num / b};
  return res;
}

struct fixed32 fixed32_div_int_int(int32_t a, int32_t b) {
  struct fixed32 res = {a * FLOAT_POINT / b};
  return res;
}

bool fixed32_less_than(struct fixed32 a, struct fixed32 b) {
  return a.num < b.num;
}