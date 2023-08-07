#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct InnerLUT {
    float3 values;
};
struct OuterLUT {
    array<InnerLUT, 3> inner;
};
struct Root {
    int valueAtRoot;
    array<OuterLUT, 3> outer;
};
struct Uniforms {
    half4 colorGreen;
    half4 colorRed;
};
struct Inputs {
};
struct Outputs {
    half4 sk_FragColor [[color(0)]];
};
fragment Outputs fragmentMain(Inputs _in [[stage_in]], constant Uniforms& _uniforms [[buffer(0)]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _out;
    (void)_out;
    Root data;
    data.valueAtRoot = 1234;
    float3 values = float3(0.0);
    for (int i = 0;i < 3; ++i) {
        for (int j = 0;j < 3; ++j) {
            values += float3(1.0, 10.0, 100.0);
            for (int k = 0;k < 3; ++k) {
                data.outer[i].inner[j].values[k] = values[k];
            }
        }
    }
    bool ok = ((((((((data.valueAtRoot == 1234 && all(data.outer[0].inner[0].values == float3(1.0, 10.0, 100.0))) && all(data.outer[0].inner[1].values == float3(2.0, 20.0, 200.0))) && all(data.outer[0].inner[2].values == float3(3.0, 30.0, 300.0))) && all(data.outer[1].inner[0].values == float3(4.0, 40.0, 400.0))) && all(data.outer[1].inner[1].values == float3(5.0, 50.0, 500.0))) && all(data.outer[1].inner[2].values == float3(6.0, 60.0, 600.0))) && all(data.outer[2].inner[0].values == float3(7.0, 70.0, 700.0))) && all(data.outer[2].inner[1].values == float3(8.0, 80.0, 800.0))) && all(data.outer[2].inner[2].values == float3(9.0, 90.0, 900.0));
    _out.sk_FragColor = ok ? _uniforms.colorGreen : _uniforms.colorRed;
    return _out;
}
