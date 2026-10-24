use std::sync::{Arc, Mutex};
use wasm_bindgen::prelude::*;
use wgpu::util::DeviceExt;

fn failure(message: impl AsRef<str>) -> JsValue { JsValue::from_str(message.as_ref()) }

// The only mesh ingress is CoinRender's captured stream, supplied by the host.
#[wasm_bindgen]
pub struct Renderer {
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pipeline: wgpu::RenderPipeline,
    fault: Arc<Mutex<Option<String>>>,
    city: Option<CityResources>,
}

#[wasm_bindgen]
impl Renderer {
    #[wasm_bindgen(js_name = create)]
    pub async fn create(canvas: web_sys::HtmlCanvasElement) -> Result<Renderer, JsValue> {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::BROWSER_WEBGPU,
            ..Default::default()
        });
        let surface = instance.create_surface(wgpu::SurfaceTarget::Canvas(canvas.clone()))
            .map_err(|e| failure(format!("WebGPU canvas surface: {e}")))?;
        let adapter = instance.request_adapter(&wgpu::RequestAdapterOptions {
            compatible_surface: Some(&surface),
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
        }).await.ok_or_else(|| failure("No WebGPU adapter available for this canvas"))?;
        if adapter.get_info().backend != wgpu::Backend::BrowserWebGpu {
            return Err(failure("Adapter is not the required browser WebGPU backend"));
        }
        let mut limits = wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
        limits.max_buffer_size = adapter.limits().max_buffer_size;
        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("CoinRender demonstration WebGPU device"),
            required_features: wgpu::Features::empty(),
            // This demonstration uses vertex/index buffers and one color/depth
            // attachment, with no compute or storage resources. This preset is
            // only a set of resource limits; the backend remains WebGPU-only.
            required_limits: limits,
            memory_hints: wgpu::MemoryHints::MemoryUsage,
        }, None).await.map_err(|e| failure(format!("WebGPU device request failed: {e}")))?;
        let fault = Arc::new(Mutex::new(None));
        let error_fault = fault.clone();
        device.on_uncaptured_error(Box::new(move |error| {
            *error_fault.lock().unwrap() = Some(format!("WebGPU device error: {error}"));
        }));
        let lost_fault = fault.clone();
        device.set_device_lost_callback(move |reason, message| {
            *lost_fault.lock().unwrap() = Some(format!("WebGPU device lost ({reason:?}): {message}"));
        });
        let caps = surface.get_capabilities(&adapter);
        let format = caps.formats.iter().copied().find(|f| f.is_srgb())
            .or_else(|| caps.formats.first().copied()).ok_or_else(|| failure("No WebGPU surface format"))?;
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, format,
            width: canvas.width().max(1), height: canvas.height().max(1),
            present_mode: wgpu::PresentMode::Fifo,
            desired_maximum_frame_latency: 2,
            alpha_mode: caps.alpha_modes[0], view_formats: vec![],
        };
        device.push_error_scope(wgpu::ErrorFilter::Validation);
        surface.configure(&device, &config);
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Captured position/material demonstration shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("scene.wgsl").into()),
        });
        let attributes = wgpu::vertex_attr_array![0 => Float32x4, 1 => Float32x3, 2 => Float32x4];
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("CoinRender captured triangles"), layout: None,
            vertex: wgpu::VertexState {
                module: &shader, entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[wgpu::VertexBufferLayout {
                    array_stride: 44, step_mode: wgpu::VertexStepMode::Vertex, attributes: &attributes,
                }],
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader, entry_point: Some("fs_main"), compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState { format, blend: None, write_mask: wgpu::ColorWrites::ALL })],
            }),
            primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: wgpu::TextureFormat::Depth24Plus, depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Less, stencil: Default::default(), bias: Default::default(),
            }),
            multisample: Default::default(), multiview: None, cache: None,
        });
        if let Some(error) = device.pop_error_scope().await {
            return Err(failure(format!("WebGPU pipeline/configuration failed: {error}")));
        }
        Ok(Renderer { surface, device, queue, config, pipeline, fault, city: None })
    }

    // Encode, submit, present and validate using wgpu. The host additionally
    // awaits onSubmittedWorkDone on this exact device's native WebGPU queue;
    // wgpu 24 does not implement Queue::on_submitted_work_done for the web.
    pub async fn render(&mut self, vertices: &[f32], indices: &[u32], width: u32, height: u32) -> Result<(), JsValue> {
        if let Some(error) = self.fault.lock().unwrap().clone() { return Err(failure(error)); }
        let vertex_count = vertices.len() / 11;
        if vertices.is_empty() || vertices.len() % 11 != 0 || !vertices.iter().all(|x| x.is_finite()) ||
           indices.is_empty() || indices.len() % 3 != 0 || indices.iter().any(|&i| i as usize >= vertex_count) {
            return Err(failure("Invalid CoinRender captured triangle stream"));
        }
        if width == 0 || height == 0 || width > 8192 || height > 8192 {
            return Err(failure("Invalid WebGPU canvas extent (1..8192)"));
        }
        self.device.push_error_scope(wgpu::ErrorFilter::Validation);
        if self.config.width != width || self.config.height != height {
            self.config.width = width; self.config.height = height;
            self.surface.configure(&self.device, &self.config);
        }
        let output = self.surface.get_current_texture().map_err(|e| failure(format!("WebGPU canvas acquisition failed: {e}")))?;
        let view = output.texture.create_view(&Default::default());
        let depth = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("CoinRender demonstration depth"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth24Plus,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
        });
        let depth_view = depth.create_view(&Default::default());
        let vertex_buffer = self.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("CoinRender WASM captured vertices"), contents: bytemuck::cast_slice(vertices), usage: wgpu::BufferUsages::VERTEX,
        });
        let index_buffer = self.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("CoinRender WASM captured indices"), contents: bytemuck::cast_slice(indices), usage: wgpu::BufferUsages::INDEX,
        });
        let mut encoder = self.device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("CoinRender WebGPU canvas frame"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.018, g: 0.028, b: 0.055, a: 1.0 }), store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &depth_view,
                    depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store }), stencil_ops: None,
                }), timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&self.pipeline);
            pass.set_vertex_buffer(0, vertex_buffer.slice(..));
            pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(0..indices.len() as u32, 0, 0..1);
        }
        self.queue.submit(Some(encoder.finish()));
        output.present();
        if let Some(error) = self.device.pop_error_scope().await { return Err(failure(format!("WebGPU frame validation failed: {error}"))); }
        if let Some(error) = self.fault.lock().unwrap().clone() { return Err(failure(error)); }
        Ok(())
    }
}

struct CityResources {
    vertices: wgpu::Buffer,
    indices: wgpu::Buffer,
    instances: wgpu::Buffer,
    uniform: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
    pipeline: wgpu::RenderPipeline,
    depth: Option<wgpu::Texture>,
    expected: u32,
    uploaded: u32,
    index_count: u32,
}

#[wasm_bindgen]
impl Renderer {
    pub fn max_buffer_size(&self) -> f64 { self.device.limits().max_buffer_size as f64 }

    pub async fn city_begin(&mut self, vertices: &[f32], indices: &[u32], instance_count: u32) -> Result<(), JsValue> {
        if self.city.is_some() { return Err(failure("City buffers already initialized")); }
        if vertices.len() != 24*6 || indices.len() != 36 || indices.iter().any(|&i| i >= 24) ||
            !vertices.iter().all(|v| v.is_finite()) || instance_count == 0 || instance_count > 1_000_001 {
            return Err(failure("Invalid canonical Coin mesh or city instance count"));
        }
        let instance_bytes = u64::from(instance_count).checked_mul(80).ok_or_else(|| failure("Instance buffer size overflow"))?;
        if instance_bytes > self.device.limits().max_buffer_size {
            return Err(failure(format!("Instance buffer {instance_bytes} bytes exceeds maxBufferSize {}", self.device.limits().max_buffer_size)));
        }
        self.device.push_error_scope(wgpu::ErrorFilter::Validation);
        let vertices = self.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("CoinRender canonical cube mesh"), contents: bytemuck::cast_slice(vertices), usage: wgpu::BufferUsages::VERTEX,
        });
        let index_count = indices.len() as u32;
        let indices = self.device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("CoinRender canonical cube indices"), contents: bytemuck::cast_slice(indices), usage: wgpu::BufferUsages::INDEX,
        });
        let instances = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("All captured city instances"), size: instance_bytes,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST, mapped_at_creation: false,
        });
        let uniform = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Coin captured city camera and lights"), size: 160,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST, mapped_at_creation: false,
        });
        let shader = self.device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Captured city instancing"), source: wgpu::ShaderSource::Wgsl(include_str!("city.wgsl").into()),
        });
        let mesh_attributes = wgpu::vertex_attr_array![0 => Float32x3, 1 => Float32x3];
        let instance_attributes = wgpu::vertex_attr_array![2 => Float32x4, 3 => Float32x4, 4 => Float32x4, 5 => Float32x4, 6 => Float32x4];
        let pipeline = self.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Coin captured city WebGPU pipeline"), layout: None,
            vertex: wgpu::VertexState { module: &shader, entry_point: Some("vs_main"), compilation_options: Default::default(), buffers: &[
                wgpu::VertexBufferLayout { array_stride: 24, step_mode: wgpu::VertexStepMode::Vertex, attributes: &mesh_attributes },
                wgpu::VertexBufferLayout { array_stride: 80, step_mode: wgpu::VertexStepMode::Instance, attributes: &instance_attributes },
            ] },
            fragment: Some(wgpu::FragmentState { module: &shader, entry_point: Some("fs_main"), compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState { format: self.config.format, blend: None, write_mask: wgpu::ColorWrites::ALL })] }),
            primitive: wgpu::PrimitiveState { cull_mode: Some(wgpu::Face::Back), ..Default::default() },
            depth_stencil: Some(wgpu::DepthStencilState { format: wgpu::TextureFormat::Depth24Plus, depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Less, stencil: Default::default(), bias: Default::default() }),
            multisample: Default::default(), multiview: None, cache: None,
        });
        let bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Coin city camera"), layout: &pipeline.get_bind_group_layout(0),
            entries: &[wgpu::BindGroupEntry { binding: 0, resource: uniform.as_entire_binding() }],
        });
        if let Some(error) = self.device.pop_error_scope().await { return Err(failure(format!("City allocation/pipeline: {error}"))); }
        self.city = Some(CityResources { vertices, indices, instances, uniform, bind_group, pipeline,
            depth: None, expected: instance_count, uploaded: 0, index_count });
        Ok(())
    }

    pub async fn city_upload(&mut self, first: u32, records: &[f32]) -> Result<(), JsValue> {
        let city = self.city.as_mut().ok_or_else(|| failure("City not initialized"))?;
        let count = records.len()/20;
        if first != city.uploaded || count == 0 || count > 1024 || records.len()%20 != 0 ||
           !records.iter().all(|v| v.is_finite()) || u64::from(first)+count as u64 > u64::from(city.expected) {
            return Err(failure("Invalid, out-of-order or oversized captured city chunk"));
        }
        self.device.push_error_scope(wgpu::ErrorFilter::Validation);
        self.queue.write_buffer(&city.instances, u64::from(first)*80, bytemuck::cast_slice(records));
        if let Some(error) = self.device.pop_error_scope().await { return Err(failure(format!("City chunk upload: {error}"))); }
        city.uploaded += count as u32;
        Ok(())
    }
    pub fn city_uploaded(&self) -> u32 { self.city.as_ref().map_or(0, |c| c.uploaded) }

    pub async fn city_render(&mut self, camera: &[f32], width: u32, height: u32) -> Result<u32, JsValue> {
        if let Some(error) = self.fault.lock().unwrap().clone() { return Err(failure(error)); }
        let city = self.city.as_mut().ok_or_else(|| failure("City not initialized"))?;
        if city.uploaded != city.expected { return Err(failure("City upload incomplete: refusing partial draw")); }
        if camera.len() != 40 || !camera.iter().all(|v| v.is_finite()) || width == 0 || height == 0 ||
            width > self.device.limits().max_texture_dimension_2d || height > self.device.limits().max_texture_dimension_2d {
            return Err(failure("Invalid captured city camera or canvas extent"));
        }
        self.device.push_error_scope(wgpu::ErrorFilter::Validation);
        if self.config.width != width || self.config.height != height {
            self.config.width=width; self.config.height=height;
            self.surface.configure(&self.device, &self.config); city.depth=None;
        }
        if city.depth.is_none() {
            city.depth = Some(self.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Persistent city depth"), size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
                mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth24Plus, usage: wgpu::TextureUsages::RENDER_ATTACHMENT, view_formats: &[],
            }));
        }
        self.queue.write_buffer(&city.uniform, 0, bytemuck::cast_slice(camera));
        let output = self.surface.get_current_texture().map_err(|e| failure(format!("City canvas: {e}")))?;
        let view = output.texture.create_view(&Default::default());
        let depth_view = city.depth.as_ref().unwrap().create_view(&Default::default());
        let mut encoder = self.device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Complete captured city"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment { view: &view, resolve_target: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color {r:0.018,g:0.028,b:0.055,a:1.0}), store: wgpu::StoreOp::Store } })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment { view: &depth_view,
                    depth_ops: Some(wgpu::Operations {load:wgpu::LoadOp::Clear(1.0),store:wgpu::StoreOp::Store}),stencil_ops:None }),
                timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&city.pipeline); pass.set_bind_group(0, &city.bind_group, &[]);
            pass.set_vertex_buffer(0, city.vertices.slice(..)); pass.set_vertex_buffer(1, city.instances.slice(..));
            pass.set_index_buffer(city.indices.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(0..city.index_count, 0, 0..city.expected);
        }
        self.queue.submit(Some(encoder.finish())); output.present();
        if let Some(error) = self.device.pop_error_scope().await { return Err(failure(format!("City frame validation: {error}"))); }
        if let Some(error) = self.fault.lock().unwrap().clone() { return Err(failure(error)); }
        Ok(city.expected)
    }
}
