import createCoinCapture from './coin_capture.js';
import initRenderer, { Renderer } from './renderer.js';

const state = window.coinRenderWebGPU;
const status = document.querySelector('#status');
const stats = document.querySelector('#stats');
const canvas = document.querySelector('#render-canvas');
const fail = window.coinRenderWebGPUFail;
const phase = window.coinRenderWebGPUPhase;
const wait = window.coinRenderWebGPUWait;
function assertActive() {
  if (state.status === 'error') throw new Error(state.error);
}
let paused = false;
document.querySelector('#pause').addEventListener('click', event => {
  paused = !paused;
  event.target.textContent = paused ? 'Continuar movimento' : 'Pausar movimento';
});
function extent() {
  const rect = canvas.getBoundingClientRect();
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  return [Math.max(1, Math.min(8192, Math.round(rect.width * ratio))),
          Math.max(1, Math.min(8192, Math.round(rect.height * ratio)))];
}
// wgpu 24's public WebGPU AdapterInfo contains empty placeholders. Observe
// its actual request instead of probing a different adapter. The checked object
// is returned unchanged to wgpu; all device creation/rendering stays in Rust.
async function createVerifiedRenderer(canvas) {
  const gpu = navigator.gpu;
  const original = gpu.requestAdapter;
  const ownDescriptor = Object.getOwnPropertyDescriptor(gpu, 'requestAdapter');
  let requests = 0;
  let validationError = null;
  let nativeDevice = null;
  let deviceFailure = null;
  let restoreDeviceRequest = null;
  let deviceRequests = 0;
  async function checkedRequest(options) {
    assertActive();
    requests += 1;
    if (requests !== 1) {
      validationError = new Error('Inicialização requisitou mais de um adapter WebGPU.');
      throw validationError;
    }
    const adapter = await wait('adapter', 'seleção do adapter WebGPU', () => original.call(gpu, options));
    if (!adapter) return null;
    try {
      const info = adapter.info || (typeof adapter.requestAdapterInfo === 'function'
        ? await wait('adapter-info', 'informações do adapter WebGPU', () => adapter.requestAdapterInfo()) : null);
      const identity = {};
      for (const field of ['vendor', 'device', 'architecture', 'description']) {
        identity[field] = typeof info?.[field] === 'string' ? info[field] : '';
      }
      // WebGPU moved this property from GPUAdapter to GPUAdapterInfo. Support
      // both versions, and reject a contradictory true from either location.
      const fallbackFlags = [adapter.isFallbackAdapter, info?.isFallbackAdapter]
        .filter(value => typeof value === 'boolean');
      identity.isFallbackAdapter = fallbackFlags.includes(true) ? true
        : fallbackFlags.includes(false) ? false : null;
      state.adapter = identity;
      const hasIdentity = ['vendor', 'device', 'architecture', 'description']
        .some(field => identity[field].trim());
      const fields = Object.entries(identity).map(([key, value]) => `${key}: ${value === '' || value === null ? 'não informado' : value}`).join(' · ');
      const details = hasIdentity ? fields
        : `identidade não informada/oculta pelo navegador · ${fields}`;
      document.querySelector('#adapter-info').textContent = `Adapter efetivo: ${details}`;
      const software = /swiftshader|llvmpipe|lavapipe|softpipe|software|swrast|microsoft basic render|warp renderer/i
        .test(Object.values(identity).join(' '));
      if (identity.isFallbackAdapter === true || software) {
        throw new Error(`Adapter WebGPU fallback/software recusado: ${details}`);
      }
      // Privacy may hide the identity. A real explicit false flag is sufficient
      // to establish non-fallback status without inventing hardware identity.
      if (identity.isFallbackAdapter !== false) {
        throw new Error(`O navegador não forneceu informação suficiente para confirmar um adapter WebGPU sem fallback: ${details}`);
      }
      const requestDevice = adapter.requestDevice;
      const deviceDescriptor = Object.getOwnPropertyDescriptor(adapter, 'requestDevice');
      // Retain the very device returned to wgpu. Drawing and submission stay
      // in Rust; only the completion signal uses the browser's native queue,
      // because wgpu 24's web queue completion method is unimplemented.
      Object.defineProperty(adapter, 'requestDevice', {
        configurable: true, writable: true,
        value: async function(descriptor) {
          assertActive();
          deviceRequests += 1;
          if (deviceRequests !== 1) throw new Error('Mais de um device WebGPU foi requisitado.');
          const device = await wait('device', 'criação do device WebGPU', () => requestDevice.call(adapter, descriptor));
          if (typeof device.queue?.onSubmittedWorkDone !== 'function') {
            device.destroy();
            throw new Error('O device WebGPU não fornece confirmação de submissão.');
          }
          nativeDevice = device;
          device.lost.then(info => {
            deviceFailure = new Error(`WebGPU device perdido (${info.reason}): ${info.message}`);
            fail(deviceFailure);
          }).catch(error => { deviceFailure = error; fail(error); });
          device.addEventListener('uncapturederror', event => {
            deviceFailure = new Error(`Erro WebGPU: ${event.error?.message || 'erro de device não capturado'}`);
            fail(deviceFailure);
          });
          phase('pipeline', 'pipeline e configuração do canvas');
          return device;
        },
      });
      restoreDeviceRequest = () => {
        if (deviceDescriptor) Object.defineProperty(adapter, 'requestDevice', deviceDescriptor);
        else delete adapter.requestDevice;
      };
      phase('device', 'criação do device WebGPU');
      return adapter;
    } catch (error) {
      validationError = error;
      throw error;
    }
  }
  Object.defineProperty(gpu, 'requestAdapter', {value: checkedRequest, configurable: true, writable: true});
  try {
    const renderer = await wait('adapter', 'seleção do adapter WebGPU', () => Renderer.create(canvas));
    if (validationError) throw validationError;
    if (requests !== 1 || !state.adapter) throw new Error('O adapter efetivo do wgpu não pôde ser verificado.');
    if (deviceRequests !== 1 || !nativeDevice) throw new Error('O device efetivo do wgpu não pôde ser verificado.');
    if (deviceFailure) throw deviceFailure;
    return {
      async render(...args) {
        assertActive();
        if (deviceFailure) throw deviceFailure;
        await wait('submission', 'submissão e validação do frame wgpu', () => renderer.render(...args));
        await wait('completion', 'confirmação de conclusão da GPU', () => nativeDevice.queue.onSubmittedWorkDone());
        if (deviceFailure) throw deviceFailure;
        // Device loss can resolve queue completion; never publish such a frame.
        if (state.status === 'error') throw new Error(state.error);
      },
    };
  } catch (error) {
    throw validationError || error;
  } finally {
    if (restoreDeviceRequest) restoreDeviceRequest();
    if (ownDescriptor) Object.defineProperty(gpu, 'requestAdapter', ownDescriptor);
    else delete gpu.requestAdapter;
  }
}
async function start() {
  assertActive();
  phase('environment', 'verificação de contexto seguro e WebGPU');
  if (!window.isSecureContext) throw new Error('WebGPU exige contexto seguro: use HTTPS com certificado confiável ou localhost.');
  if (!navigator.gpu) throw new Error('navigator.gpu indisponível: este navegador/dispositivo não oferece WebGPU.');
  const coin = await wait('coin-wasm', 'módulo Coin WebAssembly', () => createCoinCapture());
  await wait('renderer-wasm', 'módulo renderer WebAssembly', () => initRenderer());
  [canvas.width, canvas.height] = extent();
  const renderer = await createVerifiedRenderer(canvas);
  let angle = 0;
  let previous = performance.now();
  async function frame(now) {
    if (state.status === 'error') return;
    try {
      const delta = Math.min((now - previous) / 1000, 0.1); previous = now;
      if (!paused) angle += delta * 0.65;
      const [width, height] = extent();
      if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
      if (!coin._coin_capture(angle, width, height)) throw new Error(coin.UTF8ToString(coin._coin_error()));
      const vertices = coin._coin_vertex_count();
      const indices = coin._coin_index_count();
      // Explicit inter-module bridge: copies detach this frame from Emscripten's
      // growable memory before the asynchronous Rust/wgpu submission.
      const vp = coin._coin_vertices() >>> 2;
      const ip = coin._coin_indices() >>> 2;
      const vertexData = coin.HEAPF32.slice(vp, vp + vertices * 11);
      const indexData = coin.HEAPU32.slice(ip, ip + indices);
      await renderer.render(vertexData, indexData, width, height);
      if (state.status === 'error') return;
      state.stats.vertices = vertices; state.stats.indices = indices; state.stats.frames += 1;
      state.backend = 'webgpu'; state.status = 'rendering';
      phase('rendering', 'Rust wgpu / WebGPU · frame confirmado');
      status.textContent = 'Renderizando · Rust wgpu / WebGPU'; status.dataset.state = 'rendering';
      stats.textContent = `${vertices} vértices · ${indices} índices · ${state.stats.frames} frames confirmados · ${width} × ${height}`;
      requestAnimationFrame(frame);
    } catch (error) { fail(error); }
  }
  phase('first-frame', 'primeiro frame e captura da cena Coin');
  requestAnimationFrame(frame);
}
start().catch(fail);
