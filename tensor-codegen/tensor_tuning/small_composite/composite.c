#include <stddef.h>
#include <string.h>

// Define vector types
#define INTVEC(N) int __attribute__((__vector_size__(N * sizeof(int)), __aligned__(16)))
typedef INTVEC(4) _shape_t;
typedef INTVEC(4) _layout_t;
typedef INTVEC(4) _padding_t;
typedef int _token_t;

#ifndef M0
#define M0 200
#endif
#ifndef K0
#define K0 200
#endif
#ifndef M1
#define M1 200
#endif
#ifndef M2
#define M2 200
#endif
#ifndef M3
#define M3 200
#endif
#ifndef M4
#define M4 200
#endif
#ifndef M5
#define M5 200
#endif
#ifndef M6
#define M6 200
#endif
#ifndef M7
#define M7 200
#endif
#ifndef M8
#define M8 200
#endif

#ifndef NITER
#define NITER 200
#endif

typedef INTVEC(M0*K0) _t_mm0l;
typedef INTVEC(K0*M1) _t_mm0r;
typedef INTVEC(M0*M1) _t_mm0o;
typedef INTVEC(M1*M0) _t_mm1l;
typedef INTVEC(M0*M2) _t_mm1r;
typedef INTVEC(M1*M2) _t_mm1o;
typedef INTVEC(M2*M1) _t_mm2l;
typedef INTVEC(M1*M3) _t_mm2r;
typedef INTVEC(M2*M3) _t_mm2o;
typedef INTVEC(M3*M2) _t_mm3l;
typedef INTVEC(M2*M4) _t_mm3r;
typedef INTVEC(M3*M4) _t_mm3o;
typedef INTVEC(M4*M3) _t_mm4l;
typedef INTVEC(M3*M5) _t_mm4r;
typedef INTVEC(M4*M5) _t_mm4o;
typedef INTVEC(M5*M4) _t_mm5l;
typedef INTVEC(M4*M6) _t_mm5r;
typedef INTVEC(M5*M6) _t_mm5o;
typedef INTVEC(M6*M5) _t_mm6l;
typedef INTVEC(M5*M7) _t_mm6r;
typedef INTVEC(M6*M7) _t_mm6o;
typedef INTVEC(M7*M6) _t_mm7l;
typedef INTVEC(M6*M8) _t_mm7r;
typedef INTVEC(M7*M8) _t_mm7o;
typedef INTVEC(M8*M7) _t_mm8l;

_token_t tensor_typeinfo_mm0l(_t_mm0l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm0r(_t_mm0r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm0o(_t_mm0o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm1l(_t_mm1l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm1r(_t_mm1r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm1o(_t_mm1o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm2l(_t_mm2l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm2r(_t_mm2r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm2o(_t_mm2o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm3l(_t_mm3l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm3r(_t_mm3r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm3o(_t_mm3o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm4l(_t_mm4l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm4r(_t_mm4r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm4o(_t_mm4o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm5l(_t_mm5l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm5r(_t_mm5r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm5o(_t_mm5o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm6l(_t_mm6l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm6r(_t_mm6r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm6o(_t_mm6o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm7l(_t_mm7l, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm7r(_t_mm7r, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm7o(_t_mm7o, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo_mm8l(_t_mm8l, _shape_t, _layout_t, _padding_t);

_t_mm0o tensor_matmul_0(_token_t, _token_t);
_t_mm1o tensor_matmul_1(_token_t, _token_t);
_t_mm2o tensor_matmul_2(_token_t, _token_t);
_t_mm3o tensor_matmul_3(_token_t, _token_t);
_t_mm0o tensor_matmul_4(_token_t, _token_t);
_t_mm1o tensor_matmul_5(_token_t, _token_t);
_t_mm2o tensor_matmul_6(_token_t, _token_t);
_t_mm3o tensor_matmul_7(_token_t, _token_t);

_t_mm1l tensor_transpose_0(_token_t);
_t_mm2l tensor_transpose_1(_token_t);
_t_mm3l tensor_transpose_2(_token_t);
_t_mm4l tensor_transpose_3(_token_t);
_t_mm1l tensor_transpose_4(_token_t);
_t_mm2l tensor_transpose_5(_token_t);
_t_mm3l tensor_transpose_6(_token_t);
_t_mm4l tensor_transpose_7(_token_t);

_t_mm1l tensor_relu_0(_token_t);
_t_mm2l tensor_relu_1(_token_t);
_t_mm3l tensor_relu_2(_token_t);
_t_mm4l tensor_relu_3(_token_t);

int main(void) {
  _layout_t layout = {0, 1, 2, 3};
  _padding_t padding = {0, 0, 0, 0};

  _shape_t shape_mm0l = {1, 1, M0, K0};
  _shape_t shape_mm0r = {1, 1, K0, M1};
  _shape_t shape_mm0o = {1, 1, M0, M1};
  _shape_t shape_mm1l = {1, 1, M1, M0};
  _shape_t shape_mm1r = {1, 1, M0, M2};
  _shape_t shape_mm1o = {1, 1, M1, M2};
  _shape_t shape_mm2l = {1, 1, M2, M1};
  _shape_t shape_mm2r = {1, 1, M1, M3};
  _shape_t shape_mm2o = {1, 1, M2, M3};
  _shape_t shape_mm3l = {1, 1, M3, M2};
  _shape_t shape_mm3r = {1, 1, M2, M4};
  _shape_t shape_mm3o = {1, 1, M3, M4};
  _shape_t shape_mm4l = {1, 1, M4, M3};
  _shape_t shape_mm4r = {1, 1, M3, M5};
  _shape_t shape_mm4o = {1, 1, M4, M5};
  _shape_t shape_mm5l = {1, 1, M5, M4};
  _shape_t shape_mm5r = {1, 1, M4, M6};
  _shape_t shape_mm5o = {1, 1, M5, M6};
  _shape_t shape_mm6l = {1, 1, M6, M5};
  _shape_t shape_mm6r = {1, 1, M5, M7};
  _shape_t shape_mm6o = {1, 1, M6, M7};
  _shape_t shape_mm7l = {1, 1, M7, M6};
  _shape_t shape_mm7r = {1, 1, M6, M8};
  _shape_t shape_mm7o = {1, 1, M7, M8};
  _shape_t shape_mm8l = {1, 1, M8, M7};

  _t_mm0l mm0l;
  _t_mm0r mm0r;
  _t_mm1r mm1r;
  _t_mm2r mm2r;
  _t_mm3r mm3r;
  _t_mm4r mm4r;
  _t_mm5r mm5r;
  _t_mm6r mm6r;
  _t_mm7r mm7r;

  memset(&mm0l, 1, sizeof(mm0l));
  memset(&mm0r, 1, sizeof(mm0r));
  memset(&mm1r, 1, sizeof(mm1r));
  memset(&mm2r, 1, sizeof(mm2r));
  memset(&mm3r, 1, sizeof(mm3r));
  memset(&mm4r, 1, sizeof(mm4r));
  memset(&mm5r, 1, sizeof(mm5r));
  memset(&mm6r, 1, sizeof(mm6r));
  memset(&mm7r, 1, sizeof(mm7r));
  _token_t mm0l_token = tensor_typeinfo_mm0l(mm0l, shape_mm0l, layout, padding);
  _token_t mm0r_token = tensor_typeinfo_mm0r(mm0r, shape_mm0r, layout, padding);
  _token_t mm1r_token = tensor_typeinfo_mm1r(mm1r, shape_mm1r, layout, padding);
  _token_t mm2r_token = tensor_typeinfo_mm2r(mm2r, shape_mm2r, layout, padding);
  _token_t mm3r_token = tensor_typeinfo_mm3r(mm3r, shape_mm3r, layout, padding);
  _token_t mm4r_token = tensor_typeinfo_mm4r(mm4r, shape_mm4r, layout, padding);
  _token_t mm5r_token = tensor_typeinfo_mm5r(mm5r, shape_mm5r, layout, padding);
  _token_t mm6r_token = tensor_typeinfo_mm6r(mm6r, shape_mm6r, layout, padding);
  _token_t mm7r_token = tensor_typeinfo_mm7r(mm7r, shape_mm7r, layout, padding);

  for (size_t i = 0; i < NITER; i++) {
    _t_mm0o mm0o = tensor_matmul_0(mm0l_token, mm0r_token);
    _token_t mm0o_token = tensor_typeinfo_mm0o(mm0o, shape_mm0o, layout, padding);
    _t_mm1l mm1l = tensor_transpose_0(mm0o_token);
    _token_t mm1l_token = tensor_typeinfo_mm1l(mm1l, shape_mm1l, layout, padding);

    _t_mm1o mm1o = tensor_matmul_1(mm1l_token, mm1r_token);
    _token_t mm1o_token = tensor_typeinfo_mm1o(mm1o, shape_mm1o, layout, padding);
    _t_mm2l mm2l = tensor_transpose_1(mm1o_token);
    _token_t mm2l_token = tensor_typeinfo_mm2l(mm2l, shape_mm2l, layout, padding);

    _t_mm2o mm2o = tensor_matmul_2(mm2l_token, mm2r_token);
    _token_t mm2o_token = tensor_typeinfo_mm2o(mm2o, shape_mm2o, layout, padding);
    _t_mm3l mm3l = tensor_transpose_2(mm2o_token);
    _token_t mm3l_token = tensor_typeinfo_mm3l(mm3l, shape_mm3l, layout, padding);

    _t_mm3o mm3o = tensor_matmul_3(mm3l_token, mm3r_token);
    _token_t mm3o_token = tensor_typeinfo_mm3o(mm3o, shape_mm3o, layout, padding);
    _t_mm4l mm4l = tensor_transpose_3(mm3o_token);
    _token_t mm4l_token = tensor_typeinfo_mm3l(mm4l, shape_mm4l, layout, padding);

    _t_mm4o mm4o = tensor_matmul_4(mm4l_token, mm4r_token);
    _token_t mm4o_token = tensor_typeinfo_mm4o(mm4o, shape_mm4o, layout, padding);
    _t_mm5l mm5l = tensor_transpose_0(mm4o_token);
    _token_t mm5l_token = tensor_typeinfo_mm5l(mm5l, shape_mm5l, layout, padding);

    _t_mm5o mm5o = tensor_matmul_5(mm5l_token, mm5r_token);
    _token_t mm5o_token = tensor_typeinfo_mm5o(mm5o, shape_mm5o, layout, padding);
    _t_mm6l mm6l = tensor_transpose_1(mm5o_token);
    _token_t mm6l_token = tensor_typeinfo_mm6l(mm6l, shape_mm6l, layout, padding);

    _t_mm6o mm6o = tensor_matmul_6(mm6l_token, mm6r_token);
    _token_t mm6o_token = tensor_typeinfo_mm6o(mm6o, shape_mm6o, layout, padding);
    _t_mm7l mm7l = tensor_transpose_2(mm6o_token);
    _token_t mm7l_token = tensor_typeinfo_mm7l(mm7l, shape_mm7l, layout, padding);

    _t_mm7o mm7o = tensor_matmul_7(mm7l_token, mm7r_token);
    _token_t mm7o_token = tensor_typeinfo_mm7o(mm7o, shape_mm7o, layout, padding);
    _t_mm8l mm8l = tensor_transpose_3(mm7o_token);
    _token_t mm8l_token = tensor_typeinfo_mm8l(mm8l, shape_mm8l, layout, padding);
  }

  return 0;
}
