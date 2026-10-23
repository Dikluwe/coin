// Public-contract probe, also usable with browser evaluate after loading the demo.
globalThis.coinRenderWebGPUProbe = async function ({waitMs = 15000} = {}) {
  const until = performance.now() + waitMs;
  while (performance.now() < until) {
    const value = window.coinRenderWebGPU;
    if (value && (value.status === 'rendering' || value.status === 'error')) {
      return JSON.parse(JSON.stringify(value));
    }
    await new Promise(resolve => setTimeout(resolve, 25));
  }
  throw new Error('CoinRender did not reach rendering or error within deadline');
};
