use super::CoinWgpuUniforms;

// Dynamic offsets let draws share texture/sampler descriptors without sharing
// uniform contents. Chunking respects both the device buffer limit and the
// u32 dynamic-offset range; the binding itself covers exactly one record.
pub(super) struct UniformArena {
    stride: usize,
    record_size: usize,
    records_per_chunk: usize,
    remaining: usize,
    chunks: Vec<Chunk>,
}

struct Chunk {
    buffer: wgpu::Buffer,
    bytes: Vec<u8>,
    capacity: usize,
}

impl UniformArena {
    pub(super) fn new(device: &wgpu::Device, records: usize, record_size: usize) -> Self {
        let limits = device.limits();
        let size = record_size;
        let alignment = limits.min_uniform_buffer_offset_alignment as usize;
        let stride = size.div_ceil(alignment) * alignment;
        let chunk_bytes = limits.max_buffer_size.min(64 * 1024 * 1024)
            .min(u64::from(u32::MAX)) as usize;
        Self { stride, record_size, records_per_chunk: chunk_bytes / stride,
            remaining: records, chunks: Vec::new() }
    }

    pub(super) fn push(&mut self, device: &wgpu::Device,
        uniforms: &CoinWgpuUniforms) -> (usize, u32) {
        if self.chunks.last().map_or(true, |chunk| chunk.bytes.len() == chunk.capacity) {
            let records = self.remaining.min(self.records_per_chunk).max(1);
            let capacity = records * self.stride;
            self.remaining = self.remaining.saturating_sub(records);
            self.chunks.push(Chunk {
                buffer: device.create_buffer(&wgpu::BufferDescriptor {
                    label: Some("Coin draw uniform arena"), size: capacity as u64,
                    usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                    mapped_at_creation: false,
                }),
                bytes: Vec::with_capacity(capacity), capacity,
            });
        }
        let chunk_index = self.chunks.len() - 1;
        let chunk = &mut self.chunks[chunk_index];
        let offset = chunk.bytes.len();
        chunk.bytes.resize(offset + self.stride, 0);
        let bytes = &bytemuck::bytes_of(uniforms)[..self.record_size];
        chunk.bytes[offset..offset + bytes.len()].copy_from_slice(bytes);
        (chunk_index, offset as u32)
    }

    pub(super) fn buffer(&self, chunk: usize) -> &wgpu::Buffer {
        &self.chunks[chunk].buffer
    }

    pub(super) fn upload(&self, queue: &wgpu::Queue) {
        for chunk in &self.chunks {
            queue.write_buffer(&chunk.buffer, 0, &chunk.bytes);
        }
    }

    pub(super) fn chunk_count(&self) -> usize { self.chunks.len() }
}
