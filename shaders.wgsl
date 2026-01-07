struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec4f
};
struct VertexOutput {
    @builtin(position) position: vec4f, 
    @location(1) color: vec4f
};

@group(0) @binding(0) var<uniform> time: f32;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    let position = vec4f(in.position, 0.0, 1.0);
    return VertexOutput(position, in.color);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return in.color;
}