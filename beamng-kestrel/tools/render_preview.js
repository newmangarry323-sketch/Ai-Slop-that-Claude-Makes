// Renders pictures of the mod with tools/preview/preview.html in headless
// Chromium (Playwright). Needs three.js and its ColladaLoader; the copies in
// BeamNG's VS Code extension work:
//
//   git clone --depth 1 https://github.com/BeamNG/vscode-jbeam-editor /tmp/vscode-jbeam-editor
//   python3 tools/check_mod.py --export tools/preview/physics.json
//   node tools/render_preview.js /tmp/vscode-jbeam-editor/webview/libs
//
// Writes docs/kestrel_*.png and docs/thumbnail.jpg.

const http = require('http')
const fs = require('fs')
const path = require('path')
let chromium
try {
  chromium = require('playwright').chromium
} catch (e) {
  chromium = require(path.join(require('child_process').execSync('npm root -g').toString().trim(), 'playwright')).chromium
}

const libDir = path.resolve(process.argv[2] || '')
if (!fs.existsSync(path.join(libDir, 'three.min.js'))) {
  console.error('usage: node tools/render_preview.js <folder with three.min.js and ColladaLoader.js>')
  process.exit(2)
}
const root = path.resolve(__dirname, '..')
const docs = path.join(root, 'docs')
fs.mkdirSync(docs, { recursive: true })

const types = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.dae': 'model/vnd.collada+xml' }
const server = http.createServer((req, res) => {
  const url = decodeURIComponent(req.url.split('?')[0])
  const file = url.startsWith('/lib/') ? path.join(libDir, url.slice(5)) : path.join(root, url)
  if (!file.startsWith(root) && !file.startsWith(libDir)) { res.writeHead(403); return res.end() }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end() }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' })
    res.end(data)
  })
})

async function main () {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
  const port = server.address().port
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] })
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } })
  page.on('console', m => { if (m.type() === 'error') console.error('page:', m.text()) })
  for (const view of ['front', 'rear', 'side', 'physics']) {
    await page.goto(`http://127.0.0.1:${port}/tools/preview/preview.html?lib=/lib&view=${view}`)
    await page.waitForFunction(() => document.title.startsWith('ready') || document.title.startsWith('error'), null, { timeout: 120000 })
    const title = await page.title()
    if (title.startsWith('error')) throw new Error(title)
    const out = path.join(docs, `kestrel_${view}.png`)
    await page.screenshot({ path: out })
    console.log(out, title)
    if (view === 'front') {
      await page.screenshot({ path: path.join(docs, 'thumbnail.jpg'), type: 'jpeg', quality: 88 })
    }
  }
  await browser.close()
  server.close()
}

main().catch(e => { console.error(e); server.close(); process.exit(1) })
