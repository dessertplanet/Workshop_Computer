export function slugify(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/(^-|-$)+/g, '');
}

export function parseDisplayFromFolder(folderName) {
  let number = '';
  let base = folderName;
  const m = folderName.match(/^(\d+)[-_\s]+(.+)$/);
  if (m) {
    number = m[1];
    base = m[2];
  }
  base = base.replace(/[_-]+/g, ' ').trim();
  base = base.split(/\s+/).map(w => w.charAt(0).toUpperCase() + w.slice(1).toLowerCase()).join(' ');
  return { number, title: base };
}
