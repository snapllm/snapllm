import { execFileSync } from 'node:child_process';

const npm = process.env.npm_execpath
  ? process.execPath
  : (process.platform === 'win32' ? 'npm.cmd' : 'npm');
const npmArgs = process.env.npm_execpath
  ? [process.env.npm_execpath, 'audit', '--json']
  : ['audit', '--json'];
let output;
try {
  output = execFileSync(npm, npmArgs, {
    encoding: 'utf8',
    ...(process.platform === 'win32' && !process.env.npm_execpath ? { shell: true } : {}),
  });
} catch (error) {
  output = error.stdout ?? '';
}
if (!output) {
  console.error('npm audit did not return JSON output');
  process.exit(1);
}
const report = JSON.parse(output);
const findings = [];

for (const [name, vulnerability] of Object.entries(report.vulnerabilities ?? {})) {
  const advisories = (vulnerability.via ?? []).map((advisory) =>
    typeof advisory === 'object'
      ? (advisory.title ?? advisory.url ?? 'unknown advisory')
      : String(advisory),
  );
  if (advisories.length > 0) {
    findings.push(`${name}: ${advisories.join('; ')}`);
  }
}

if (findings.length) {
  console.error(findings.join('\n'));
  process.exit(1);
}

console.log('npm audit: passed (no advisories)');
