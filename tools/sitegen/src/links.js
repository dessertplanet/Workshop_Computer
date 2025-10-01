import { encodePathSegments } from './utils/fs.js';

export function makeRawUrl(repo, ref, relPathFromRepoRoot) {
  return `https://raw.githubusercontent.com/${repo}/${ref}/${encodePathSegments(relPathFromRepoRoot)}`;
}
