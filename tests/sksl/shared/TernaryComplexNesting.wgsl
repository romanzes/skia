struct FSIn {
    @builtin(front_facing) sk_Clockwise: bool,
    @builtin(position) sk_FragCoord: vec4<f32>,
};
struct FSOut {
    @location(0) sk_FragColor: vec4<f32>,
};
struct _GlobalUniforms {
    colorWhite: vec4<f32>,
};
@binding(0) @group(0) var<uniform> _globalUniforms: _GlobalUniforms;
fn IsEqual_bh4h4(x: vec4<f32>, y: vec4<f32>) -> bool {
    return all(x == y);
}
fn main(coords: vec2<f32>) -> vec4<f32> {
    var colorBlue: vec4<f32> = vec4<f32>(0.0, 0.0, _globalUniforms.colorWhite.zw);
    var colorGreen: vec4<f32> = vec4<f32>(0.0, _globalUniforms.colorWhite.y, 0.0, _globalUniforms.colorWhite.w);
    var colorRed: vec4<f32> = vec4<f32>(_globalUniforms.colorWhite.x, 0.0, 0.0, _globalUniforms.colorWhite.w);
    var _skTemp0: vec4<f32>;
    if !IsEqual_bh4h4(_globalUniforms.colorWhite, colorBlue) {
        var _skTemp1: vec4<f32>;
        if IsEqual_bh4h4(colorGreen, colorRed) {
            _skTemp1 = colorRed;
        } else {
            _skTemp1 = colorGreen;
        }
        _skTemp0 = _skTemp1;
    } else {
        var _skTemp2: vec4<f32>;
        if !IsEqual_bh4h4(colorRed, colorGreen) {
            _skTemp2 = colorBlue;
        } else {
            _skTemp2 = _globalUniforms.colorWhite;
        }
        _skTemp0 = _skTemp2;
    }
    var result: vec4<f32> = _skTemp0;
    var _skTemp3: vec4<f32>;
    if IsEqual_bh4h4(colorRed, colorBlue) {
        _skTemp3 = _globalUniforms.colorWhite;
    } else {
        var _skTemp4: vec4<f32>;
        if !IsEqual_bh4h4(colorRed, colorGreen) {
            _skTemp4 = result;
        } else {
            var _skTemp5: vec4<f32>;
            if IsEqual_bh4h4(colorRed, _globalUniforms.colorWhite) {
                _skTemp5 = colorBlue;
            } else {
                _skTemp5 = colorRed;
            }
            _skTemp4 = _skTemp5;
        }
        _skTemp3 = _skTemp4;
    }
    return _skTemp3;
}
@fragment fn fragmentMain(_stageIn: FSIn) -> FSOut {
    var _stageOut: FSOut;
    _stageOut.sk_FragColor = main(_stageIn.sk_FragCoord.xy);
    return _stageOut;
}
