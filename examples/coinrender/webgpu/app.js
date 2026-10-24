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
    // Explicit selection on the very request made by wgpu, never an extra
    // probe or automatic retry with a different WebGPU feature level.
    const requestedOptions = {...options, featureLevel: state.profile, forceFallbackAdapter: false};
    const adapter = await wait('adapter', 'seleção do adapter WebGPU · perfil ' + state.profile,
      () => original.call(gpu, requestedOptions));
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
      identity.maxBufferSize = adapter.limits?.maxBufferSize ?? null;
      const hasIdentity = ['vendor', 'device', 'architecture', 'description']
        .some(field => identity[field].trim());
      const fields = Object.entries(identity).map(([key, value]) => `${key}: ${value === '' || value === null ? 'não informado' : value}`).join(' · ');
      const details = hasIdentity ? fields
        : `identidade não informada/oculta pelo navegador · ${fields}`;
      document.querySelector('#adapter-info').textContent = `Perfil WebGPU solicitado: ${state.profile} · Adapter efetivo: ${details}`;
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
    async function submit(method, args) {
      assertActive();
      if (deviceFailure) throw deviceFailure;
      const started = performance.now();
      const value = await wait('submission', 'submissão e validação do frame wgpu', () => method(...args));
      const submitted = performance.now();
      await wait('completion', 'confirmação de conclusão da GPU', () => nativeDevice.queue.onSubmittedWorkDone());
      const completed = performance.now();
      if (deviceFailure) throw deviceFailure;
      assertActive();
      return {value, renderMs: submitted-started, completionMs: completed-submitted};
    }
    return {
      render: (...args) => submit((...values) => renderer.render(...values), args),
      city_render: (...args) => submit((...values) => renderer.city_render(...values), args),
      city_begin: (...args) => wait('city-allocation', 'buffers e pipeline instanciado', () => renderer.city_begin(...args)),
      city_upload: (...args) => wait('city-upload',
        `upload de instâncias ${args[0]+1}–${args[0]+args[1].length/20} / ${state.scene.buildings+state.scene.ground}`,
        () => renderer.city_upload(...args)),
      city_uploaded: () => renderer.city_uploaded(),
      max_buffer_size: () => renderer.max_buffer_size(),
      city_upload_complete: () => wait('city-upload-completion', 'conclusão do upload da cidade', () => nativeDevice.queue.onSubmittedWorkDone()),
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
  if (state.profile !== 'core' && state.profile !== 'compatibility') {
    throw new Error('Perfil WebGPU não validado antes da inicialização.');
  }
  phase('environment', 'verificação de contexto seguro e WebGPU · perfil ' + state.profile);
  if (!window.isSecureContext) throw new Error('WebGPU exige contexto seguro: use HTTPS com certificado confiável ou localhost.');
  if (!navigator.gpu) throw new Error('navigator.gpu indisponível: este navegador/dispositivo não oferece WebGPU.');
  const coin = await wait('coin-wasm', 'módulo Coin WebAssembly', () => createCoinCapture());
  await wait('renderer-wasm', 'módulo renderer WebAssembly', () => initRenderer());
  [canvas.width, canvas.height] = extent();
  const renderer = await createVerifiedRenderer(canvas);
  if (state.scene?.name !== 'cube') {
    await startCity(coin, renderer);
    return;
  }
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
      state.stats.width = width; state.stats.height = height;
      state.scene.submittedInstances = 1;
      state.backend = 'webgpu'; state.status = 'rendering';
      phase('rendering', 'Rust wgpu / WebGPU · frame confirmado');
      status.textContent = 'Renderizando · Rust wgpu / WebGPU · perfil ' + state.profile; status.dataset.state = 'rendering';
      stats.textContent = `${vertices} vértices · ${indices} índices · ${state.stats.frames} frames confirmados · ${width} × ${height}`;
      requestAnimationFrame(frame);
    } catch (error) { fail(error); }
  }
  phase('first-frame', 'primeiro frame e captura da cena Coin');
  requestAnimationFrame(frame);
}
start().catch(fail);


// Binary layout: 56-byte LE header (magic, six u32 fields, six bound floats),
// palette RGB f32 triples, then translation XYZ/scale XYZ/paletteIndex records.
export function parseCityAsset(buffer, expectedBuildings) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < 56) throw new Error('Ativo da cidade truncado');
  const data = new DataView(buffer);
  if (String.fromCharCode(...new Uint8Array(buffer,0,8)) !== 'CRCTY001') throw new Error('Formato de cidade inválido');
  const grid=data.getUint32(8,true), seed=data.getUint32(12,true), buildings=data.getUint32(16,true);
  const count=data.getUint32(20,true), stride=data.getUint32(24,true), colors=data.getUint32(28,true);
  if (![200,1000].includes(grid) || seed!==136 || buildings!==grid*grid || buildings!==expectedBuildings ||
      count!==buildings+1 || stride!==28 || colors!==9) throw new Error('Metadados da cidade não correspondem à cena solicitada');
  const offset=56+colors*12;
  if (buffer.byteLength!==offset+count*stride) throw new Error('Comprimento do ativo de cidade inválido');
  const bounds=Array.from({length:6},(_,i)=>data.getFloat32(32+i*4,true));
  if (!bounds.every(Number.isFinite) || [0,1,2].some(i=>bounds[i]>=bounds[i+3])) throw new Error('Limites da cidade inválidos');
  const palette=Array.from({length:colors*3},(_,i)=>data.getFloat32(56+i*4,true));
  if (!palette.every(v=>Number.isFinite(v)&&v>=0&&v<=1)) throw new Error('Paleta da cidade inválida');
  // A successfully parsed asset is fully valid, even when used independently
  // of the chunked capture/upload path. Inspect in place without expanding it.
  for (let index=0;index<count;index++) {
    const position=offset+index*stride;
    if (data.getUint32(position+24,true)!==(index===0?8:(index-1)%8))
      throw new Error('Índice de material da cidade inválido');
    for (let component=0;component<6;component++) {
      const value=data.getFloat32(position+component*4,true);
      if (!Number.isFinite(value)||(component>=3&&value<=0))
        throw new Error('Transformação de cidade inválida');
    }
  }
  return {data,offset,count,stride,bounds,palette};
}

export function benchmarkGroup(samples, elapsedMs) {
  if (samples.length!==10 || !Number.isFinite(elapsedMs) || elapsedMs<=0 ||
      samples.some(s=>![s.renderMs,s.completionMs].every(v=>Number.isFinite(v)&&v>=0)))
    throw new Error('Amostras de benchmark inválidas');
  const renderMs=samples.reduce((sum,s)=>sum+s.renderMs,0)/10;
  const completionMs=samples.reduce((sum,s)=>sum+s.completionMs,0)/10;
  const frameMs=elapsedMs/10;
  if (renderMs+completionMs>frameMs+0.01) throw new Error('Tempos do benchmark excedem o intervalo medido');
  return {fpsWall:1000/frameMs,renderMs,completionMs,frameMs};
}

async function prepareCity(coin, renderer) {
  const startup = state.benchmark.startup;
  let started=performance.now();
  const filename=state.scene.name==='million'?'city-million.bin':'city-40000.bin';
  const response=await wait('city-load','download dos registros da cidade',()=>fetch(filename));
  if (!response.ok) throw new Error(`Ativo de cidade HTTP ${response.status}`);
  const buffer=await wait('city-load','leitura dos registros da cidade',()=>response.arrayBuffer());
  const asset=parseCityAsset(buffer,state.scene.buildings);
  startup.loadMs=performance.now()-started;
  const total=asset.count, required=total*80;
  const max=state.adapter.maxBufferSize;
  if (!Number.isSafeInteger(max)||max<=0||required>max||required>renderer.max_buffer_size())
    throw new Error(`Buffer de instâncias ${required} bytes excede maxBufferSize ${max}`);
  const [width,height]=extent();
  canvas.width=width;canvas.height=height;
  coin.HEAPF32.set(asset.bounds,coin._coin_city_bounds()>>>2);
  started=performance.now();
  if (!coin._coin_city_begin(width,height)) throw new Error(coin.UTF8ToString(coin._coin_error()));
  startup.captureMs=performance.now()-started;
  const vertexCount=coin._coin_city_vertex_count(),indexCount=coin._coin_city_index_count();
  const vp=coin._coin_city_vertices()>>>2,ip=coin._coin_city_indices()>>>2;
  started=performance.now();
  await renderer.city_begin(coin.HEAPF32.slice(vp,vp+vertexCount*6),coin.HEAPU32.slice(ip,ip+indexCount),total);
  startup.uploadMs=performance.now()-started;
  const capacity=coin._coin_city_capacity();
  if (!Number.isInteger(capacity)||capacity<1||capacity>1024) throw new Error('Capacidade de captura inválida');
  state.progress={processed:0,total:state.scene.buildings};
  for (let first=0;first<total;first+=capacity) {
    assertActive();
    const count=Math.min(capacity,total-first),input=new Float32Array(count*10);
    for (let i=0;i<count;i++) {
      const position=asset.offset+(first+i)*asset.stride;
      const color=asset.data.getUint32(position+24,true);
      if (color!==(first+i===0?8:(first+i-1)%8)) throw new Error('Índice de material da cidade inválido');
      for (let k=0;k<6;k++) {
        const value=asset.data.getFloat32(position+k*4,true);
        if (!Number.isFinite(value)||(k>=3&&value<=0)) throw new Error('Transformação de cidade inválida');
        input[i*10+k]=value;
      }
      for(let k=0;k<3;k++)input[i*10+6+k]=asset.palette[color*3+k];
      input[i*10+9]=1;
    }
    phase('city-capture',`captura CoinRender de instâncias ${first+1}–${first+count} / ${total}`);
    coin.HEAPF32.set(input,coin._coin_city_input()>>>2);
    started=performance.now();
    const captured=coin._coin_city_capture(count,width,height);
    startup.captureMs+=performance.now()-started;
    if(captured!==count)throw new Error(coin.UTF8ToString(coin._coin_error())||'Contagem de captura da cidade divergente');
    const ptr=coin._coin_city_instances()>>>2;
    started=performance.now();
    await renderer.city_upload(first,coin.HEAPF32.slice(ptr,ptr+captured*20));
    startup.uploadMs+=performance.now()-started;
    state.progress.processed=Math.max(0,first+count-1);
    await wait('city-progress',`CoinRender: ${state.progress.processed} / ${state.scene.buildings} edifícios`,
      ()=>new Promise(resolve=>setTimeout(resolve,0)));
  }
  if(renderer.city_uploaded()!==total)throw new Error('Upload incompleto; desenho parcial recusado');
  started=performance.now();await renderer.city_upload_complete();startup.uploadMs+=performance.now()-started;
  return {vertexCount,indexCount,width,height};
}

let reporting=false;
async function reportBenchmark() {
  if (new URLSearchParams(location.search).get('report')!=='1'||reporting)return;
  reporting=true;
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),30000);
  try {
    const {scene,stats,benchmark,adapter,profile,backend,fallback,status}=state;
    const body=JSON.stringify({scene,stats,benchmark,adapter,profile,backend,fallback,status});
    if(new TextEncoder().encode(body).length>65536)throw new Error('Snapshot de benchmark excede 64KiB');
    const response=await fetch('/benchmark-report',{method:'POST',headers:{'Content-Type':'application/json'},body,signal:controller.signal});
    if(!response.ok)throw new Error(`Exportação local HTTP ${response.status}`);
    state.reportError='';
  }catch(error){state.reportError=String(error);}
  finally{clearTimeout(timer);reporting=false;}
}

async function startCity(coin,renderer) {
  const profileNote=document.querySelector('.profile');
  if(profileNote)profileNote.textContent=`Cena solicitada: ${state.scene.buildings.toLocaleString()} edifícios + 1 chão. C++ CoinRender captura malha, transformações e materiais em lotes; Rust wgpu desenha todas as instâncias via WebGPU. Câmera e cena estáticas durante a medição. Sem fallback.`;
  state.benchmark={warmupFrames:10,groupSize:10,groups:[],completedWarmup:0,startup:{loadMs:0,captureMs:0,uploadMs:0}};
  const prepared=await prepareCity(coin,renderer);
  state.benchmark.startup.totalMs=performance.now();
  let width=prepared.width,height=prepared.height;
  let camera=coin.HEAPF32.slice(coin._coin_city_uniform()>>>2,(coin._coin_city_uniform()>>>2)+40);
  let samples=[],groupStart=null;
  document.querySelector('#pause').textContent='Cena estática · medição contínua';
  document.querySelector('#pause').disabled=true;
  async function frame() {
    if(state.status==='error')return;
    try{
      const next=extent();
      if(next[0]!==width||next[1]!==height){
        [width,height]=next;canvas.width=width;canvas.height=height;
        if(!coin._coin_city_camera(width,height))throw new Error(coin.UTF8ToString(coin._coin_error()));
        const ptr=coin._coin_city_uniform()>>>2;camera=coin.HEAPF32.slice(ptr,ptr+40);
        // Different extents are separate measurements, never mixed in one group.
        samples=[];groupStart=null;
        state.benchmark.groups=[];state.benchmark.completedWarmup=0;
        delete state.benchmark.fpsP50;delete state.benchmark.fpsP95;
      }
      const measured=await renderer.city_render(camera,width,height);
      if(state.status==='error')return;
      if(measured.value!==state.scene.buildings+state.scene.ground)throw new Error('Renderer não submeteu todas as instâncias');
      const now=performance.now();
      state.scene.submittedInstances=measured.value;
      state.stats.vertices=prepared.vertexCount;state.stats.indices=prepared.indexCount;state.stats.frames++;
      state.stats.width=width;state.stats.height=height;
      state.backend='webgpu';state.status='rendering';
      phase('rendering','cidade completa · frame GPU confirmado');status.dataset.state='rendering';
      if(state.benchmark.completedWarmup<10){
        state.benchmark.completedWarmup++;
        if(state.benchmark.completedWarmup===10)groupStart=now;
      }else{
        samples.push(measured);
        if(samples.length===10){
          const group=benchmarkGroup(samples,now-groupStart);
          group.width=width;group.height=height;
          state.benchmark.groups.push(group);
          if(state.benchmark.groups.length>120)state.benchmark.groups.shift();
          const fps=state.benchmark.groups.map(g=>g.fpsWall).sort((a,b)=>a-b);
          state.benchmark.fpsP50=fps[Math.floor((fps.length-1)*.50)];
          state.benchmark.fpsP95=fps[Math.floor((fps.length-1)*.95)];
          samples=[];groupStart=now;
          void reportBenchmark();
        }
      }
      const last=state.benchmark.groups.at(-1);
      stats.textContent=`${state.scene.buildings.toLocaleString()} edifícios + ${state.scene.ground} chão · ${state.scene.logicalTriangles.toLocaleString()} triângulos · ${state.scene.submittedInstances.toLocaleString()} instâncias submetidas · malha ${prepared.vertexCount}/${prepared.indexCount} · ${state.stats.frames} frames GPU · ${width}×${height} · ${last?last.fpsWall.toFixed(2)+' FPS wall / '+last.frameMs.toFixed(2)+' ms':'aquecimento '+state.benchmark.completedWarmup+'/10'}`;
      requestAnimationFrame(frame);
    }catch(error){fail(error);}
  }
  phase('first-frame','primeiro desenho completo da cidade');requestAnimationFrame(frame);
}
