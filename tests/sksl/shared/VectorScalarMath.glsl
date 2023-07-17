
out vec4 sk_FragColor;
uniform vec4 colorRed;
uniform vec4 colorGreen;
uniform float unknownInput;
bool test_int_b() {
    bool ok = true;
    ivec4 inputRed = ivec4(colorRed);
    ivec4 inputGreen = ivec4(colorGreen);
    ivec4 x = inputRed + 2;
    ok = ok && x == ivec4(3, 2, 2, 3);
    x = inputGreen.ywxz - 2;
    ok = ok && x == ivec4(-1, -1, -2, -2);
    x = inputRed + inputGreen.y;
    ok = ok && x == ivec4(2, 1, 1, 2);
    x.xyz = inputGreen.wyw * 9;
    ok = ok && x == ivec4(9, 9, 9, 2);
    x.xy = x.zw / 3;
    ok = ok && x == ivec4(3, 0, 9, 2);
    x = (inputRed * 5).yxwz;
    ok = ok && x == ivec4(0, 5, 5, 0);
    x = 2 + inputRed;
    ok = ok && x == ivec4(3, 2, 2, 3);
    x = 10 - inputGreen.ywxz;
    ok = ok && x == ivec4(9, 9, 10, 10);
    x = inputRed.x + inputGreen;
    ok = ok && x == ivec4(1, 2, 1, 2);
    x.xyz = 9 * inputGreen.wyw;
    ok = ok && x == ivec4(9, 9, 9, 2);
    x.xy = 36 / x.zw;
    ok = ok && x == ivec4(4, 18, 9, 2);
    x = (36 / x).yxwz;
    ok = ok && x == ivec4(2, 9, 18, 4);
    x += 2;
    x *= 2;
    x -= 4;
    x /= 2;
    ok = ok && x == ivec4(2, 9, 18, 4);
    x = x + 2;
    x = x * 2;
    x = x - 4;
    x = x / 2;
    ok = ok && x == ivec4(2, 9, 18, 4);
    return ok;
}
vec4 main() {
    bool _0_ok = true;
    vec4 _1_inputRed = colorRed;
    vec4 _2_inputGreen = colorGreen;
    vec4 _3_x = _1_inputRed + 2.0;
    _0_ok = _0_ok && _3_x == vec4(3.0, 2.0, 2.0, 3.0);
    _3_x = _2_inputGreen.ywxz - 2.0;
    _0_ok = _0_ok && _3_x == vec4(-1.0, -1.0, -2.0, -2.0);
    _3_x = _1_inputRed + _2_inputGreen.y;
    _0_ok = _0_ok && _3_x == vec4(2.0, 1.0, 1.0, 2.0);
    _3_x.xyz = _2_inputGreen.wyw * 9.0;
    _0_ok = _0_ok && _3_x == vec4(9.0, 9.0, 9.0, 2.0);
    _3_x.xy = _3_x.zw / 0.5;
    _0_ok = _0_ok && _3_x == vec4(18.0, 4.0, 9.0, 2.0);
    _3_x = (_1_inputRed * 5.0).yxwz;
    _0_ok = _0_ok && _3_x == vec4(0.0, 5.0, 5.0, 0.0);
    _3_x = 2.0 + _1_inputRed;
    _0_ok = _0_ok && _3_x == vec4(3.0, 2.0, 2.0, 3.0);
    _3_x = 10.0 - _2_inputGreen.ywxz;
    _0_ok = _0_ok && _3_x == vec4(9.0, 9.0, 10.0, 10.0);
    _3_x = _1_inputRed.x + _2_inputGreen;
    _0_ok = _0_ok && _3_x == vec4(1.0, 2.0, 1.0, 2.0);
    _3_x.xyz = 8.0 * _2_inputGreen.wyw;
    _0_ok = _0_ok && _3_x == vec4(8.0, 8.0, 8.0, 2.0);
    _3_x.xy = 32.0 / _3_x.zw;
    _0_ok = _0_ok && _3_x == vec4(4.0, 16.0, 8.0, 2.0);
    _3_x = (32.0 / _3_x).yxwz;
    _0_ok = _0_ok && _3_x == vec4(2.0, 8.0, 16.0, 4.0);
    _3_x += 2.0;
    _3_x *= 2.0;
    _3_x -= 4.0;
    _3_x /= 2.0;
    _0_ok = _0_ok && _3_x == vec4(2.0, 8.0, 16.0, 4.0);
    _3_x = _3_x + 2.0;
    _3_x = _3_x * 2.0;
    _3_x = _3_x - 4.0;
    _3_x = _3_x / 2.0;
    _0_ok = _0_ok && _3_x == vec4(2.0, 8.0, 16.0, 4.0);
    return _0_ok && test_int_b() ? colorGreen : colorRed;
}
