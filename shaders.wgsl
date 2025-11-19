struct VertexInput {
    @location(0) pos: vec2f,
    @location(1) color: vec4f
};

struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    let pos = vec4f(in.pos, 1.0, 0.0);
    return VertexOutput(pos, in.color);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return in.color;
}
