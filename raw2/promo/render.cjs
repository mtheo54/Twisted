// Rend une vidéo promo image par image (Chromium sans fenêtre + three.js) et la monte avec ffmpeg.
//   node promo/render.cjs <lancement|le-son> <largeur> <hauteur> <sortie.mp4>
//   node promo/render.cjs <lancement|le-son> <largeur> <hauteur> --apercu <dossier> <t1,t2,...>
// Le serveur local sert le dossier raw2/ (polices, scène, build/promo-assets).
const { chromium } = require('playwright');
const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const [ad, w, h, out, ...rest] = process.argv.slice(2);
if (!ad || !w || !h || !out) {
    console.error('usage: node promo/render.cjs <lancement|le-son> <largeur> <hauteur> <sortie.mp4 | --apercu dossier t1,t2>');
    process.exit(1);
}
const W = +w, H = +h, FPS = 30;
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png', '.ttf': 'font/ttf', '.txt': 'text/plain', '.wav': 'audio/wav' };

const server = http.createServer((req, res) => {
    const rel = decodeURIComponent(req.url.split('?')[0]);
    const file = path.normalize(path.join(ROOT, rel));
    if (!file.startsWith(ROOT)) { res.writeHead(403); res.end(); return; }
    fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404); res.end(); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
        res.end(data);
    });
});

(async () => {
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    const port = server.address().port;
    const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--disable-gpu-vsync'] });
    const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
    page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') console.log('[page]', m.text()); });
    page.on('pageerror', e => { console.error('[page error]', e.message); });
    await page.goto(`http://127.0.0.1:${port}/promo/scene.html?ad=${ad}&w=${W}&h=${H}&fps=${FPS}`);
    await page.waitForFunction('window.READY === true', null, { timeout: 120000 });
    const duration = await page.evaluate('window.DURATION');

    if (out === '--apercu') {
        const dir = rest[0], times = (rest[1] || '0').split(',').map(Number);
        fs.mkdirSync(dir, { recursive: true });
        for (const t of times) {
            await page.evaluate(t => window.renderFrame(t), t);
            await page.screenshot({ path: path.join(dir, `${ad}-${W}x${H}-${t.toFixed(2)}.png`) });
        }
        await browser.close();
        server.close();
        return;
    }

    const wav = path.join(ROOT, 'build', 'promo-assets', `${ad}.wav`);
    const ff = spawn('ffmpeg', ['-v', 'error', '-y', '-f', 'image2pipe', '-c:v', 'mjpeg', '-framerate', String(FPS), '-i', '-',
        '-i', wav, '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-preset', 'slow', '-crf', '17', '-pix_fmt', 'yuv420p',
        '-profile:v', 'high', '-movflags', '+faststart', '-c:a', 'aac', '-b:a', '256k', '-shortest', out], { stdio: ['pipe', 'inherit', 'inherit'] });
    const frames = Math.round(duration * FPS);
    const t0 = Date.now();
    for (let f = 0; f < frames; ++f) {
        await page.evaluate(t => window.renderFrame(t), f / FPS);
        const buf = await page.screenshot({ type: 'jpeg', quality: 95 });
        if (!ff.stdin.write(buf)) await new Promise(r => ff.stdin.once('drain', r));
        if (f % 60 === 0) process.stdout.write(`${ad} ${W}x${H} : image ${f}/${frames} (${((Date.now() - t0) / 1000).toFixed(0)} s)\n`);
    }
    ff.stdin.end();
    await new Promise((r, j) => ff.on('close', c => (c === 0 ? r() : j(new Error('ffmpeg ' + c)))));
    await browser.close();
    server.close();
    console.log(`${out} écrit (${frames} images, ${((Date.now() - t0) / 1000).toFixed(0)} s)`);
})().catch(e => { console.error(e); process.exit(1); });
