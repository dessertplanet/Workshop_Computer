// Render a base-vs-current validation comparison for GitHub Actions.
// Usage: node compareReportsCli.js CURRENT.json BASELINE.json [SUMMARY.md]

import fs from 'node:fs';
import {
  classifyDiagnostics,
  reportComparisonText,
  reportGithubComparison,
} from './diagnosticComparison.js';

const [currentFile, baselineFile, summaryFile] = process.argv.slice(2);
if (!currentFile || !baselineFile) {
  console.error('Usage: node compareReportsCli.js CURRENT.json BASELINE.json [SUMMARY.md]');
  process.exit(2);
}

function resultsFrom(file) {
  const parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
  return Array.isArray(parsed) ? parsed : parsed.results || [];
}

const compared = classifyDiagnostics(resultsFrom(currentFile), resultsFrom(baselineFile));
const text = reportComparisonText(compared);
console.log(reportGithubComparison(compared));
console.log(text);

if (summaryFile) {
  fs.appendFileSync(summaryFile, [
    '## Complete info.yaml validation',
    '',
    'Every current diagnostic in each changed file is listed. **NEW** diagnostics were not present on the PR base branch; **existing** diagnostics were already present in that file.',
    '',
    '```text',
    text,
    '```',
    '',
  ].join('\n'));
}
