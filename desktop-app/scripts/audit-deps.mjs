import { execFileSync } from 'node:child_process';

// Temporary, narrowly scoped exception approved by the maintainer:
// GHSA-qwww-vcr4-c8h2 only affects React Router's unstable RSC APIs, which
// SnapLLM does not enable. Remove this exception when react-router >=8.3.0
// is published and available in the lockfile.
const approved = new Set(['GHSA-qwww-vcr4-c8h2']);
const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
let output;
try {
  output = execFileSync(npm, ['audit', '--json'], {
    encoding: 'utf8',
    shell: process.platform === 'win32',
  });
} catch (error) {
  output = error.stdout;
}
const report = JSON.parse(output);
const findings = [];

for (const [name, vulnerability] of Object.entries(report.vulnerabilities ?? {})) {
  for (const advisory of vulnerability.via ?? []) {
    if (typeof advisory === 'object' && !approved.has(advisory.url?.split('/').pop())) {
      findings.push(`${name}: ${advisory.title ?? advisory.url ?? 'unknown advisory'}`);
    }
  }
}

if (findings.length) {
  console.error(findings.join('\n'));
  process.exit(1);
}

console.log('npm audit: passed (approved RSC-only exception: GHSA-qwww-vcr4-c8h2)');
