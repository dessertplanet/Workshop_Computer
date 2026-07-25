// Render one current validation report for GitHub Actions.
// Usage: node githubReportCli.js REPORT.json [SUMMARY.md]

import fs from 'node:fs';
import { reportGithub, reportMarkdown, reportText } from './reporters/index.js';

const [reportFile, summaryFile] = process.argv.slice(2);
if (!reportFile) {
  console.error('Usage: node githubReportCli.js REPORT.json [SUMMARY.md]');
  process.exit(2);
}

const parsed = JSON.parse(fs.readFileSync(reportFile, 'utf8'));
const results = Array.isArray(parsed) ? parsed : parsed.results || [];
console.log(reportGithub(results));
console.log(reportText(results));
if (summaryFile) fs.writeFileSync(summaryFile, reportMarkdown(results));