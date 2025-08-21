export function debugLog(...args) {
  if (process.env.DEBUG_SITEGEN === '1') console.log('[sitegen]', ...args);
}
