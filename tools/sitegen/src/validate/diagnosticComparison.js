// Compare complete validation reports for the PR version of changed info.yaml
// files and their base-branch versions. Matching deliberately ignores source
// line numbers so moving unchanged invalid metadata does not make it look new.

function canonicalFile(file) {
  const normalized = String(file || '').replaceAll('\\', '/');
  const releaseIndex = normalized.indexOf('releases/');
  return releaseIndex >= 0 ? normalized.slice(releaseIndex) : normalized;
}

function identity(diagnostic) {
  return [
    diagnostic.severity || 'error',
    diagnostic.ruleId || '',
    diagnostic.path || '',
    diagnostic.message || '',
  ].join('\u0000');
}

function baselineCounts(results) {
  const byFile = new Map();
  for (const result of results || []) {
    const counts = new Map();
    for (const diagnostic of result.diagnostics || []) {
      const key = identity(diagnostic);
      counts.set(key, (counts.get(key) || 0) + 1);
    }
    byFile.set(canonicalFile(result.file), counts);
  }
  return byFile;
}

/** Return current diagnostics classified as `new` or `existing`. */
export function classifyDiagnostics(currentResults, baselineResults) {
  const baseline = baselineCounts(baselineResults);
  return (currentResults || []).map(result => {
    const file = canonicalFile(result.file);
    const available = new Map(baseline.get(file) || []);
    const diagnostics = (result.diagnostics || []).map(diagnostic => {
      const key = identity(diagnostic);
      const remaining = available.get(key) || 0;
      if (remaining) available.set(key, remaining - 1);
      return { ...diagnostic, origin: remaining ? 'existing' : 'new' };
    });
    return { ...result, file, diagnostics };
  });
}

export function comparisonTotals(results) {
  const totals = {
    new: { errors: 0, warnings: 0 },
    existing: { errors: 0, warnings: 0 },
    files: (results || []).length,
  };
  for (const result of results || []) {
    for (const diagnostic of result.diagnostics || []) {
      const origin = diagnostic.origin === 'existing' ? 'existing' : 'new';
      const level = diagnostic.severity === 'error' ? 'errors' : 'warnings';
      totals[origin][level] += 1;
    }
  }
  return totals;
}

function escapeCommand(value) {
  return String(value).replace(/%/g, '%25').replace(/\r/g, '%0D').replace(/\n/g, '%0A');
}

/** GitHub workflow annotations, visibly labelled NEW or EXISTING. */
export function reportGithubComparison(results) {
  const lines = [];
  for (const result of results || []) {
    for (const diagnostic of result.diagnostics || []) {
      const level = diagnostic.severity === 'error' ? 'error' : 'warning';
      const origin = diagnostic.origin === 'existing' ? 'EXISTING' : 'NEW';
      const props = [`file=${result.file}`];
      if (diagnostic.line != null) props.push(`line=${diagnostic.line}`);
      if (diagnostic.col != null) props.push(`col=${diagnostic.col}`);
      props.push(`title=info.yaml ${origin} ${diagnostic.ruleId}`);
      const detail = diagnostic.path ? `${diagnostic.path}: ${diagnostic.message}` : diagnostic.message;
      lines.push(`::${level} ${props.join(',')}::${escapeCommand(detail)}`);
    }
  }
  const totals = comparisonTotals(results);
  lines.push(`::notice title=info.yaml validation::New: ${totals.new.errors} error(s), ${totals.new.warnings} warning(s). Existing: ${totals.existing.errors} error(s), ${totals.existing.warnings} warning(s).`);
  return lines.join('\n');
}

/** Complete human-readable report for the GitHub job summary. */
export function reportComparisonText(results) {
  const lines = [];
  for (const result of results || []) {
    lines.push(result.file);
    if (!result.diagnostics.length) {
      lines.push('  clean');
      continue;
    }
    for (const diagnostic of result.diagnostics) {
      const origin = diagnostic.origin === 'existing' ? 'existing' : 'NEW';
      const level = diagnostic.severity === 'error' ? 'error' : 'warning';
      const location = diagnostic.line == null ? '' : `:${diagnostic.line}${diagnostic.col == null ? '' : `:${diagnostic.col}`}`;
      const path = diagnostic.path ? ` [${diagnostic.path}]` : '';
      lines.push(`  ${origin} ${level}${location}${path} ${diagnostic.message} (${diagnostic.ruleId})`);
    }
  }
  const totals = comparisonTotals(results);
  lines.push('');
  lines.push(`New: ${totals.new.errors} error(s), ${totals.new.warnings} warning(s).`);
  lines.push(`Existing: ${totals.existing.errors} error(s), ${totals.existing.warnings} warning(s).`);
  return lines.join('\n');
}
