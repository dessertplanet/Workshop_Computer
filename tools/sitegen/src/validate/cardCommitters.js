// Look up the GitHub accounts that have committed to a card, via the REST API.
// Squash merges credit the PR author, and GitHub only links a commit to an
// account whose verified email it carries, so these logins are reliable.

/**
 * Logins of commit authors under releases/<card>/ reachable from `ref`.
 * Returns null on any API failure so callers fail closed.
 */
export async function fetchCardCommitters({ repo, card, ref, token, fetchImpl = fetch }) {
  const logins = new Set();
  const headers = { accept: 'application/vnd.github+json', 'x-github-api-version': '2022-11-28' };
  if (token) headers.authorization = `Bearer ${token}`;
  let url = `https://api.github.com/repos/${repo}/commits?${new URLSearchParams({
    sha: ref, path: `releases/${card}`, per_page: '100',
  })}`;
  try {
    while (url) {
      const response = await fetchImpl(url, { headers });
      if (!response.ok) return null;
      for (const commit of await response.json()) {
        if (commit.author?.login) logins.add(commit.author.login);
      }
      url = response.headers.get('link')?.match(/<([^>]+)>;\s*rel="next"/)?.[1] || null;
    }
  } catch {
    return null;
  }
  return [...logins].sort();
}
