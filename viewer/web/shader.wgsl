// Every mark the viewer draws -- a tile of the fabric, a cell of a tile's
// bit window, a highlight ring -- is one instanced rectangle. The camera
// maps world units to clip space, so the same pipeline draws the fabric map
// and the bit inset with nothing but a different uniform.

struct Camera {
    scale: vec2<f32>,
    offset: vec2<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;

struct Instance {
    // x, y, width, height, in world units.
    @location(0) rect: vec4<f32>,
    @location(1) color: vec4<f32>,
};

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};

@vertex
fn vertex_main(@builtin(vertex_index) vertex_index: u32, instance: Instance) -> VertexOut {
    // A unit quad as a triangle strip, so no vertex buffer is needed.
    var corners = array<vec2<f32>, 4>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 1.0),
    );
    let corner = corners[vertex_index];
    let world = instance.rect.xy + corner * instance.rect.zw;

    var out: VertexOut;
    out.position = vec4<f32>(world * camera.scale + camera.offset, 0.0, 1.0);
    out.color = instance.color;
    return out;
}

@fragment
fn fragment_main(in: VertexOut) -> @location(0) vec4<f32> {
    return in.color;
}
