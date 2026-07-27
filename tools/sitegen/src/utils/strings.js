/** Lowercase info.yaml keys; strip spaces and hyphens for consistent lookup (demo-link → demolink). */
export function normalizeYamlKey(k) {
  return String(k || '').toLowerCase().replace(/[\s-]+/g, '');
}

export function slugify(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/(^-|-$)+/g, '');
}
