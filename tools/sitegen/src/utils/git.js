import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { debugLog } from './logger.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../..');

export function detectRepoFromGit() {
  try {
    const url = execSync('git config --get remote.origin.url', { cwd: ROOT, encoding: 'utf8' }).trim();
    const m = url.match(/github\.com[:\/]([^\s]+?)(?:\.git)?$/i);
    if (m && m[1]) {
      return m[1].replace(/\.git$/i, '');
    }
  } catch (e) { debugLog('detectRepoFromGit failed:', e?.message || e); }
  return null;
}

export function detectRefFromGit() {
  try {
    const sha = execSync('git rev-parse HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (sha) return sha;
  } catch (e) { debugLog('detectRefFromGit sha failed:', e?.message || e); }
  try {
    const branch = execSync('git rev-parse --abbrev-ref HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (branch && branch !== 'HEAD') return branch;
  } catch (e) { debugLog('detectRefFromGit branch failed:', e?.message || e); }
  return null;
}
