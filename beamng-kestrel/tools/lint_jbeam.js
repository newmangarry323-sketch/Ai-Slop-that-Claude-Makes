// Checks every .jbeam, .pc and .json file of the mod with the JBeam parser
// and table processor from BeamNG's own VS Code extension
// (https://github.com/BeamNG/vscode-jbeam-editor).
//
//   git clone --depth 1 https://github.com/BeamNG/vscode-jbeam-editor /tmp/vscode-jbeam-editor
//   node tools/lint_jbeam.js /tmp/vscode-jbeam-editor
//
// The extension expects to run inside VS Code, so a tiny stand-in for the
// 'vscode' module is supplied here.

const fs = require('fs')
const path = require('path')
const Module = require('module')

const extDir = process.argv[2]
if (!extDir) {
  console.error('usage: node tools/lint_jbeam.js <path to vscode-jbeam-editor>')
  process.exit(2)
}

const originalLoad = Module._load
Module._load = function (request, ...rest) {
  if (request === 'vscode') {
    return {
      Position: class { constructor (line, character) { this.line = line; this.character = character } },
      Range: class { constructor (start, end) { this.start = start; this.end = end } },
      Diagnostic: class { constructor (range, message, severity) { this.range = range; this.message = message; this.severity = severity } },
      DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
      Uri: { file: (p) => ({ fsPath: p }) },
      workspace: { workspaceFolders: [] },
      window: {}
    }
  }
  return originalLoad.call(this, request, ...rest)
}

const sjson = require(path.join(extDir, 'src/json/sjsonParser'))
const tableSchema = require(path.join(extDir, 'src/json/tableSchema'))

const vehicleDir = path.join(__dirname, '..', 'mod', 'vehicles', 'slop_kestrel')
let problems = 0
let checked = 0
for (const name of fs.readdirSync(vehicleDir).sort()) {
  if (!/\.(jbeam|pc|json)$/.test(name)) continue
  const file = path.join(vehicleDir, name)
  const bundle = sjson.decodeWithMetaWithDiagnostics(fs.readFileSync(file, 'utf8'), file)
  const messages = bundle.diagnosticsList.map(d => `${d.severity === 0 ? 'error' : 'warning'}: ${d.message} (line ${d.range.start.line + 1})`)
  let parts = 0
  if (name.endsWith('.jbeam') && bundle.data) {
    const [processed, diagnostics] = tableSchema.processAllParts(bundle.data)
    for (const [level, message] of diagnostics) messages.push(`${level}: ${message}`)
    parts = Object.keys(processed).filter(k => k !== '__meta').length
  }
  checked++
  problems += messages.length
  console.log(`${messages.length ? 'FAIL' : 'ok  '} ${name}${parts ? ` (${parts} part${parts > 1 ? 's' : ''})` : ''}`)
  for (const m of messages) console.log('     ' + m)
}
console.log(`${checked} files checked, ${problems} problems`)
process.exit(problems ? 1 : 0)
