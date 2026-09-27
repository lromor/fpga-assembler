//! The wgpu side: one instanced-rectangle pipeline, drawn twice per frame.

use bytemuck::{Pod, Zeroable};

/// One rectangle. The renderer draws nothing else.
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct Instance {
    pub rect: [f32; 4],
    pub color: [f32; 4],
}

impl Instance {
    pub fn new(x: f32, y: f32, width: f32, height: f32, color: [f32; 4]) -> Self {
        Instance {
            rect: [x, y, width, height],
            color,
        }
    }
}

/// Maps world units to clip space: `clip = world * scale + offset`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct CameraUniform {
    pub scale: [f32; 2],
    pub offset: [f32; 2],
}

/// A rectangle of the canvas, in physical pixels.
#[derive(Clone, Copy, Debug)]
pub struct Viewport {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

/// One draw: a slice of the instance buffer, a camera and where to put it.
pub struct Pass<'a> {
    pub instances: &'a [Instance],
    pub camera: CameraUniform,
    pub viewport: Viewport,
}

pub struct Renderer {
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pipeline: wgpu::RenderPipeline,
    bind_group_layout: wgpu::BindGroupLayout,
    /// One camera buffer and bind group per pass of the frame, grown as
    /// needed: a uniform cannot change inside a render pass.
    cameras: Vec<(wgpu::Buffer, wgpu::BindGroup)>,
    instance_buffer: wgpu::Buffer,
    instance_capacity: usize,
    pub background: wgpu::Color,
}

impl Renderer {
    pub async fn new(
        canvas: web_sys::HtmlCanvasElement,
        width: u32,
        height: u32,
    ) -> Result<Self, String> {
        // WebGPU when the browser really has it, WebGL2 otherwise: the
        // detecting constructor exists because `navigator.gpu` can be
        // present on a browser that still cannot hand out an adapter.
        let instance = wgpu::util::new_instance_with_webgpu_detection(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::BROWSER_WEBGPU | wgpu::Backends::GL,
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        })
        .await;
        let surface = instance
            .create_surface(wgpu::SurfaceTarget::Canvas(canvas))
            .map_err(|error| format!("could not create a surface on the canvas: {error}"))?;
        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::LowPower,
                compatible_surface: Some(&surface),
                ..Default::default()
            })
            .await
            .map_err(|error| format!("no usable GPU adapter: {error}"))?;
        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("fabric-view"),
                // WebGL2 offers less than the default; ask for no more than
                // it has, so the viewer runs without WebGPU too.
                required_limits: wgpu::Limits::downlevel_webgl2_defaults()
                    .using_resolution(adapter.limits()),
                ..Default::default()
            })
            .await
            .map_err(|error| format!("could not open the GPU device: {error}"))?;

        let mut config = surface
            .get_default_config(&adapter, width.max(1), height.max(1))
            .ok_or_else(|| "the surface is not usable with this adapter".to_string())?;
        // Prefer an sRGB target: the palette is written in sRGB and
        // converted once, in `palette`.
        let capabilities = surface.get_capabilities(&adapter);
        if let Some(srgb) = capabilities
            .formats
            .iter()
            .copied()
            .find(wgpu::TextureFormat::is_srgb)
        {
            config.format = srgb;
        }
        let format = config.format;
        surface.configure(&device, &config);

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("fabric-view"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shader.wgsl").into()),
        });
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("camera"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("fabric-view"),
            bind_group_layouts: &[Some(&bind_group_layout)],
            immediate_size: 0,
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("fabric-view"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vertex_main"),
                compilation_options: Default::default(),
                buffers: &[Some(wgpu::VertexBufferLayout {
                    array_stride: size_of::<Instance>() as wgpu::BufferAddress,
                    step_mode: wgpu::VertexStepMode::Instance,
                    attributes: &wgpu::vertex_attr_array![0 => Float32x4, 1 => Float32x4],
                })],
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fragment_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleStrip,
                ..Default::default()
            },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });

        let instance_capacity = 4096;
        let instance_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("instances"),
            size: (instance_capacity * size_of::<Instance>()) as wgpu::BufferAddress,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        Ok(Renderer {
            surface,
            device,
            queue,
            config,
            pipeline,
            bind_group_layout,
            cameras: Vec::new(),
            instance_buffer,
            instance_capacity,
            background: wgpu::Color {
                r: 0.043,
                g: 0.047,
                b: 0.059,
                a: 1.0,
            },
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
    }

    pub fn size(&self) -> (u32, u32) {
        (self.config.width, self.config.height)
    }

    /// Makes sure there is a camera buffer and bind group for every pass.
    fn ensure_camera_slots(&mut self, count: usize) {
        while self.cameras.len() < count {
            let buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("camera"),
                size: size_of::<CameraUniform>() as wgpu::BufferAddress,
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
            let bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("camera"),
                layout: &self.bind_group_layout,
                entries: &[wgpu::BindGroupEntry {
                    binding: 0,
                    resource: buffer.as_entire_binding(),
                }],
            });
            self.cameras.push((buffer, bind_group));
        }
    }

    /// Draws the passes, in order, into one frame.
    ///
    /// Returns false when there was no frame to draw into and nothing was
    /// drawn, so the caller knows to ask again rather than leave the canvas
    /// showing whatever was there before.
    pub fn render(&mut self, passes: &[Pass<'_>]) -> Result<bool, String> {
        let total: usize = passes.iter().map(|pass| pass.instances.len()).sum();
        if total > self.instance_capacity {
            // Grow generously: the fabric buffer is written once and then
            // only the small overlays change.
            self.instance_capacity = total.next_power_of_two();
            self.instance_buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("instances"),
                size: (self.instance_capacity * size_of::<Instance>()) as wgpu::BufferAddress,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
        }

        let mut offsets = Vec::with_capacity(passes.len());
        let mut cursor = 0u64;
        for pass in passes {
            let bytes: &[u8] = bytemuck::cast_slice(pass.instances);
            if !bytes.is_empty() {
                self.queue
                    .write_buffer(&self.instance_buffer, cursor, bytes);
            }
            offsets.push((cursor, pass.instances.len() as u32));
            cursor += bytes.len() as u64;
        }
        self.ensure_camera_slots(passes.len());
        for (index, pass) in passes.iter().enumerate() {
            self.queue
                .write_buffer(&self.cameras[index].0, 0, bytemuck::bytes_of(&pass.camera));
        }

        // A frame can be unavailable for reasons that are not failures: the
        // canvas was resized out from under the surface, or the page is not
        // visible.  Reconfigure and skip rather than report an error the
        // page would show to the user.
        let frame = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame)
            | wgpu::CurrentSurfaceTexture::Suboptimal(frame) => frame,
            wgpu::CurrentSurfaceTexture::Outdated | wgpu::CurrentSurfaceTexture::Lost => {
                self.surface.configure(&self.device, &self.config);
                return Ok(false);
            }
            wgpu::CurrentSurfaceTexture::Timeout | wgpu::CurrentSurfaceTexture::Occluded => {
                return Ok(false);
            }
            wgpu::CurrentSurfaceTexture::Validation => {
                return Err("the surface rejected the frame".to_string());
            }
        };
        let view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("fabric-view"),
            });
        {
            let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("fabric-view"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(self.background),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            render_pass.set_pipeline(&self.pipeline);
            for (index, pass) in passes.iter().enumerate() {
                let (offset, count) = offsets[index];
                if count == 0 {
                    continue;
                }
                let viewport = pass.viewport;
                if viewport.width < 1.0 || viewport.height < 1.0 {
                    continue;
                }
                render_pass.set_viewport(
                    viewport.x,
                    viewport.y,
                    viewport.width,
                    viewport.height,
                    0.0,
                    1.0,
                );
                render_pass.set_bind_group(0, &self.cameras[index].1, &[]);
                let bytes = (count as u64) * size_of::<Instance>() as u64;
                render_pass
                    .set_vertex_buffer(0, self.instance_buffer.slice(offset..offset + bytes));
                render_pass.draw(0..4, 0..count);
            }
        }
        self.queue.submit(std::iter::once(encoder.finish()));
        self.queue.present(frame);
        Ok(true)
    }
}
