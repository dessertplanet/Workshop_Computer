import { promises as fs } from 'node:fs';
import path from 'node:path';

export async function ensureDir(dir) {
  await fs.mkdir(dir, { recursive: true });
}

export async function writeFileEnsured(filePath, content) {
  await ensureDir(path.dirname(filePath));
  await fs.writeFile(filePath, content);
}

export async function listSubdirs(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  return entries.filter(e => e.isDirectory()).map(e => e.name);
}

export async function fileExists(file) {
  try {
    await fs.access(file);
    return true;
  } catch {
    return false;
  }
}

export function toPosix(p) {
  return p.replace(/\\/g, '/');
}

export function encodePathSegments(p) {
  return toPosix(p).split('/').map(encodeURIComponent).join('/');
}

export const fsAsync = fs;
