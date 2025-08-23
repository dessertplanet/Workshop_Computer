import path from 'node:path';
import { fsAsync as fs, encodePathSegments } from '../utils/fs.js';

export async function discoverDownloads(absReleaseDir, repoRelBase, makeRawUrl) {
  const downloads = [];
  let latestUf2 = null;

  async function walk(dir) {
    const ents = await fs.readdir(dir, { withFileTypes: true });
    for (const ent of ents) {
      const fullPath = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        await walk(fullPath);
      } else if (/\.(uf2|zip|bin|hex)$/i.test(ent.name)) {
        const relFromRelease = path.relative(absReleaseDir, fullPath);
        const relFromRepoRoot = path.join(repoRelBase, relFromRelease);
        let mtime = 0;
        try {
          mtime = (await fs.stat(fullPath)).mtimeMs;
        } catch {}
        const encoded = encodePathSegments(relFromRepoRoot);
        const url = makeRawUrl(encoded);
        const item = { name: ent.name, rel: relFromRepoRoot, url, mtime };
        downloads.push(item);
        if (/\.uf2$/i.test(ent.name)) {
          if (!latestUf2 || mtime > latestUf2.mtime) latestUf2 = item;
        }
      }
    }
  }

  await walk(absReleaseDir);
  return { downloads, latestUf2 };
}
