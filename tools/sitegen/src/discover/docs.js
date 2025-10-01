import path from 'node:path';
import { fsAsync as fs, ensureDir, toPosix } from '../utils/fs.js';

export async function discoverDocs(absReleaseDir, outProgramDir, docsDirName = null) {
  const entries = await fs.readdir(absReleaseDir, { withFileTypes: true });
  const docsDir = docsDirName || entries.find(e => e.isDirectory() && /^(docs|documentation)$/i.test(e.name))?.name;
  const docs = [];
  if (!docsDir) return { docs, docsDir: null };

  const fullDocsDir = path.join(absReleaseDir, docsDir);
  const files = await fs.readdir(fullDocsDir, { withFileTypes: true });
  const outDocsDir = path.join(outProgramDir, docsDir);
  await ensureDir(outDocsDir);

  for (const f of files) {
    if (f.isFile() && f.name.toLowerCase().endsWith('.pdf')) {
      const srcPath = path.join(fullDocsDir, f.name);
      const destPath = path.join(outDocsDir, f.name);
      await fs.copyFile(srcPath, destPath);
      const relFromDetail = toPosix(`${docsDir}/${f.name}`);
      docs.push({ name: f.name, rel: relFromDetail, url: relFromDetail });
    }
  }
  return { docs, docsDir };
}
