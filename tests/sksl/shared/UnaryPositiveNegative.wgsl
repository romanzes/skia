struct FSIn {
    @builtin(front_facing) sk_Clockwise: bool,
    @builtin(position) sk_FragCoord: vec4<f32>,
};
struct FSOut {
    @location(0) sk_FragColor: vec4<f32>,
};
struct _GlobalUniforms {
    colorWhite: vec4<f32>,
    colorGreen: vec4<f32>,
    colorRed: vec4<f32>,
    testMatrix2x2: mat2x2<f32>,
    testMatrix3x3: mat3x3<f32>,
    testMatrix4x4: mat4x4<f32>,
};
@binding(0) @group(0) var<uniform> _globalUniforms: _GlobalUniforms;
fn mat2x2f32_eq_mat2x2f32(left: mat2x2<f32>, right: mat2x2<f32>) -> bool {
    return all(left[0] == right[0]) &&
           all(left[1] == right[1]);
}
fn mat3x3f32_eq_mat3x3f32(left: mat3x3<f32>, right: mat3x3<f32>) -> bool {
    return all(left[0] == right[0]) &&
           all(left[1] == right[1]) &&
           all(left[2] == right[2]);
}
fn mat4x4f32_eq_mat4x4f32(left: mat4x4<f32>, right: mat4x4<f32>) -> bool {
    return all(left[0] == right[0]) &&
           all(left[1] == right[1]) &&
           all(left[2] == right[2]) &&
           all(left[3] == right[3]);
}
fn test_iscalar_b() -> bool {
    var x: i32 = i32(_globalUniforms.colorWhite.x);
    x = -x;
    return x == -1;
}
fn test_fvec_b() -> bool {
    var x: vec2<f32> = _globalUniforms.colorWhite.xy;
    x = -x;
    return all(x == vec2<f32>(-1.0));
}
fn test_ivec_b() -> bool {
    var x: vec2<i32> = vec2<i32>(i32(_globalUniforms.colorWhite.x));
    x = -x;
    return all(x == vec2<i32>(-1));
}
fn test_mat2_b() -> bool {
    let negated: mat2x2<f32> = mat2x2<f32>(vec2<f32>(-1.0, -2.0), vec2<f32>(-3.0, -4.0));
    var x: mat2x2<f32> = _globalUniforms.testMatrix2x2;
    x = (-1.0 * x);
    return mat2x2f32_eq_mat2x2f32(x, negated);
}
fn test_mat3_b() -> bool {
    let negated: mat3x3<f32> = mat3x3<f32>(vec3<f32>(-1.0, -2.0, -3.0), vec3<f32>(-4.0, -5.0, -6.0), vec3<f32>(-7.0, -8.0, -9.0));
    var x: mat3x3<f32> = _globalUniforms.testMatrix3x3;
    x = (-1.0 * x);
    return mat3x3f32_eq_mat3x3f32(x, negated);
}
fn test_mat4_b() -> bool {
    let negated: mat4x4<f32> = mat4x4<f32>(vec4<f32>(-1.0, -2.0, -3.0, -4.0), vec4<f32>(-5.0, -6.0, -7.0, -8.0), vec4<f32>(-9.0, -10.0, -11.0, -12.0), vec4<f32>(-13.0, -14.0, -15.0, -16.0));
    var x: mat4x4<f32> = _globalUniforms.testMatrix4x4;
    x = (-1.0 * x);
    return mat4x4f32_eq_mat4x4f32(x, negated);
}
fn main(coords: vec2<f32>) -> vec4<f32> {
    var _0_x: f32 = f32(_globalUniforms.colorWhite.x);
    _0_x = -_0_x;
    var _skTemp0: vec4<f32>;
    var _skTemp1: bool;
    var _skTemp2: bool;
    var _skTemp3: bool;
    var _skTemp4: bool;
    var _skTemp5: bool;
    var _skTemp6: bool;
    if _0_x == -1.0 {
        _skTemp6 = test_iscalar_b();
    } else {
        _skTemp6 = false;
    }
    if _skTemp6 {
        _skTemp5 = test_fvec_b();
    } else {
        _skTemp5 = false;
    }
    if _skTemp5 {
        _skTemp4 = test_ivec_b();
    } else {
        _skTemp4 = false;
    }
    if _skTemp4 {
        _skTemp3 = test_mat2_b();
    } else {
        _skTemp3 = false;
    }
    if _skTemp3 {
        _skTemp2 = test_mat3_b();
    } else {
        _skTemp2 = false;
    }
    if _skTemp2 {
        _skTemp1 = test_mat4_b();
    } else {
        _skTemp1 = false;
    }
    if _skTemp1 {
        _skTemp0 = _globalUniforms.colorGreen;
    } else {
        _skTemp0 = _globalUniforms.colorRed;
    }
    return _skTemp0;
}
@fragment fn fragmentMain(_stageIn: FSIn) -> FSOut {
    var _stageOut: FSOut;
    _stageOut.sk_FragColor = main(_stageIn.sk_FragCoord.xy);
    return _stageOut;
}
