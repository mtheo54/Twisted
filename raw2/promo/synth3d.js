// RAW 2 modélisé en 3D d'après la page SON du plugin (coordonnées de l'interface 1280 × 960 px,
// 1 px = 0,01 unité). Châssis, panneaux en relief, sérigraphie, potards, clavier, matrice de LED de la
// VIE, écrans d'oscilloscope, afficheurs et boutons. Les couleurs viennent d'un thème de themes.json.
//   const s = makeSynth(theme, { hot, light });  scene.add(s.group);  s.update({ t, ... }) à chaque image
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';

const U = 0.01;
const X = px => px * U - 6.4;
const Y = py => 4.8 - py * U;
const Z0 = 0.45;          // dessus du châssis
const ZC = Z0 + 0.07;     // dessus des panneaux
const CAP = 0.09;         // hauteur des boutons
const S = 2;              // échelle des textures (2 px de texture par px d'interface)

const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const lerp = (a, b, t) => a + (b - a) * t;

function rngf(seed) {
    return () => {
        seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
        let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

// petit jeu de la vie 37 × 18 (torique) pour la matrice de LED
function lifeGens(cols, rows, n, seed) {
    const r = rngf(seed);
    let cur = new Uint8Array(cols * rows);
    for (let i = 0; i < cur.length; ++i) cur[i] = r() < 0.3 ? 1 : 0;
    const out = [cur];
    for (let g = 1; g < n; ++g) {
        const nx = new Uint8Array(cols * rows);
        for (let y = 0; y < rows; ++y) for (let x = 0; x < cols; ++x) {
            let k = 0;
            for (let dy = -1; dy <= 1; ++dy) for (let dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                if (cur[((y + dy + rows) % rows) * cols + ((x + dx + cols) % cols)]) ++k;
            }
            const a = cur[y * cols + x];
            nx[y * cols + x] = a ? (k === 2 || k === 3 ? Math.min(a + 1, 99) : 0) : (k === 3 ? 1 : 0);
        }
        // ensemencement léger pour que la grille ne s'éteigne jamais
        if (g % 24 === 0) for (let i = 0; i < 18; ++i) nx[Math.floor(r() * nx.length)] = 1;
        out.push(nx);
        cur = nx;
    }
    return out;
}

// les surfaces éclairées de l'appareil ne dépassent jamais le blanc : seul ce qui émet de la lumière
// (LED, touches enfoncées, bande orange) nourrit le halo lumineux (bloom)
function noBloom(m) {
    m.onBeforeCompile = s => {
        s.fragmentShader = s.fragmentShader.replace('#include <dithering_fragment>',
            '#include <dithering_fragment>\n gl_FragColor.rgb = min(gl_FragColor.rgb - totalEmissiveRadiance, vec3(0.96)) + totalEmissiveRadiance;');
    };
    m.customProgramCacheKey = () => 'noBloom';
    return m;
}

function canvasTex(wpx, hpx, scale = S) {
    const c = document.createElement('canvas');
    c.width = Math.round(wpx * scale);
    c.height = Math.round(hpx * scale);
    const g = c.getContext('2d');
    g.scale(scale, scale);
    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 8;
    return { c, g, tex };
}

export function makeSynth(TH, { hot = new THREE.Color(3.2, 0.95, 0.06), light = false, seed = 7 } = {}) {
    const col = h => new THREE.Color(h);
    // couleur « qui brille » : l'accent du thème poussé au-delà du blanc (l'orange d'ALU garde sa teinte dorée)
    if (TH.acc.toUpperCase() !== '#FF4D00') { const c = col(TH.acc); hot = c.multiplyScalar(3.2 / Math.max(c.r, c.g, c.b, 1e-3)); }
    const group = new THREE.Group();
    const mats = new Map();
    const std = (hex, rough = 0.55, metal = 0.0) => {
        const k = `s${hex}${rough}${metal}`;
        if (!mats.has(k)) mats.set(k, noBloom(new THREE.MeshStandardMaterial({ color: col(hex), roughness: rough, metalness: metal, envMapIntensity: 0.6 })));
        return mats.get(k);
    };
    // pavé arrondi posé à la hauteur z0, défini en px d'interface
    function rbox(px, py, pw, ph, depth, z0, mat, radius = 0.03, seg = 3) {
        const r = Math.min(radius, (pw * U) / 2 - 0.001, (ph * U) / 2 - 0.001, depth / 2 - 0.001);
        const m = new THREE.Mesh(new RoundedBoxGeometry(pw * U, ph * U, depth, seg, Math.max(0.002, r)), mat);
        m.position.set(X(px + pw / 2), Y(py + ph / 2), z0 + depth / 2);
        group.add(m);
        return m;
    }
    // plan de texture (sérigraphie, afficheur, écran) aux coordonnées de l'interface
    function plane(px, py, pw, ph, z, map, { transparent = true, basic = true } = {}) {
        const mat = basic ? new THREE.MeshBasicMaterial({ map, transparent, depthWrite: !transparent, toneMapped: false })
                          : new THREE.MeshStandardMaterial({ map, transparent, depthWrite: !transparent, roughness: 0.7 });
        const m = new THREE.Mesh(new THREE.PlaneGeometry(pw * U, ph * U), mat);
        m.position.set(X(px + pw / 2), Y(py + ph / 2), z);
        group.add(m);
        return m;
    }
    const acc = TH.acc, ink = TH.ink, muted = TH.muted, card = TH.card;
    const darkPanel = TH.scr;
    const font = (f, s) => `${s}px ${f}`;
    function spaced(g, text, x, y, ls = 0, align = 'left') {
        g.letterSpacing = ls + 'px';
        g.textAlign = align;
        g.fillText(text, x, y);
        g.letterSpacing = '0px';
        g.textAlign = 'left';
    }

    // ---------------------------------------------------------------- châssis et panneaux
    const chassis = new THREE.Mesh(new RoundedBoxGeometry(13.5, 10.3, 0.9, 5, 0.32), std(TH.chassis, 0.42, 0.5));
    group.add(chassis);
    const panels = [[24, 72, 362, 500], [396, 72, 488, 500], [894, 72, 362, 500], [24, 583, 1232, 55], [24, 648, 814, 287]];
    for (const [x, y, w, h] of panels) rbox(x, y, w, h, 0.07, Z0, std(card, 0.6), 0.04);
    rbox(848, 648, 408, 287, 0.07, Z0, std(darkPanel, 0.45, 0.1), 0.04);   // PROPOSITION (panneau sombre)
    // vis des panneaux
    const screwGeo = new THREE.CylinderGeometry(0.045, 0.045, 0.025, 14);
    screwGeo.rotateX(Math.PI / 2);
    const screwMat = std(TH.muted, 0.35, 0.8);
    for (const [x, y, w, h] of [...panels.slice(0, 3), panels[4], [848, 648, 408, 287]]) {
        for (const [sx, sy] of [[x + 9, y + 9], [x + w - 9, y + 9], [x + 9, y + h - 9], [x + w - 9, y + h - 9]]) {
            const s = new THREE.Mesh(screwGeo, screwMat);
            s.position.set(X(sx), Y(sy), ZC + 0.0125);
            group.add(s);
        }
    }
    // bande lumineuse sous la plaque (accent)
    const strip = new THREE.Mesh(new THREE.BoxGeometry(11.6, 0.06, 0.06), new THREE.MeshBasicMaterial({ color: hot.clone() }));
    strip.position.set(0, -5.03, Z0 - 0.01);
    group.add(strip);

    // ---------------------------------------------------------------- sérigraphie : châssis, panneaux, dessus des boutons
    const P0 = canvasTex(1280, 960), P1 = canvasTex(1280, 960), P2 = canvasTex(1280, 960);
    {
        const g = P0.g;  // châssis : points, logo, en-tête, pied de plaque, coins
        g.fillStyle = TH.chDot || 'rgba(0,0,0,.1)';
        for (let y = 6; y < 960; y += 12) for (let x = 6; x < 1280; x += 12) g.fillRect(x - 0.6, y - 0.6, 1.2, 1.2);
        g.fillStyle = muted;
        g.font = font('Mono', 9);
        spaced(g, 'ABRASION', 76, 33, 3.2);
        g.font = font('PixB', 23);
        g.fillStyle = ink;
        g.fillText('RAW', 74, 58);
        g.fillStyle = acc;
        g.fillText('2', 74 + g.measureText('RAW').width + 1, 58);
        g.fillStyle = muted;
        g.font = font('Mono', 8.5);
        spaced(g, 'SPEC.NO: RAW-02 // PCB REV.C', 38, 951, 1.6);
        spaced(g, 'MIL-STD ● [TP1] ● [TP2]', 1242, 951, 1.6, 'right');
        g.font = font('Mono', 9);
        spaced(g, 'DISTRIBUÉ SUR', 600, 951, 4, 'right');
        g.font = font('MonoB', 9);
        g.fillStyle = ink;
        spaced(g, 'ONIRIQUE', 610, 951, 4);
        g.strokeStyle = muted;
        g.lineWidth = 1.5;
        for (const [x, y, dx, dy] of [[10, 10, 1, 1], [1270, 10, -1, 1], [10, 950, 1, -1], [1270, 950, -1, -1]]) {
            g.beginPath(); g.moveTo(x + dx * 22, y); g.lineTo(x, y); g.lineTo(x, y + dy * 22); g.stroke();
        }
    }
    const knobs = [];
    {
        const g = P1.g;  // panneaux
        g.fillStyle = ink;
        g.font = font('PixB', 16);
        for (const [tx, x] of [['OSC A', 40], ['VIE', 412], ['OSC B', 910]]) spaced(g, tx, x, 103, 3);
        g.font = font('PixB', 15);
        spaced(g, 'SORTIE', 40, 678, 3);
        g.fillStyle = TH.scrText || '#FFFFFF';
        g.font = font('PixB', 12.5);
        spaced(g, 'PROPOSITION', 868, 685, 1.4);
        // filets sous les titres
        g.strokeStyle = TH.rule || ink;
        g.lineWidth = 1;
        for (const [a, b, y] of [[40, 370, 117], [412, 868, 117], [910, 1240, 117], [40, 822, 696]]) { g.beginPath(); g.moveTo(a, y); g.lineTo(b, y); g.stroke(); }
        // petites cases d'en-tête des oscillateurs
        g.font = font('MonoM', 10);
        const boxT = (x, y, w, h, txt, stroke, fill) => {
            g.strokeStyle = stroke; g.lineWidth = 1.2; g.strokeRect(x, y, w, h);
            g.fillStyle = fill; g.textAlign = 'center'; g.fillText(txt, x + w / 2, y + h / 2 + 3.5); g.textAlign = 'left';
        };
        boxT(120, 86, 54, 20, '-2 OCT', ink, ink); boxT(174, 86, 31, 20, '0 ST', ink, ink);
        boxT(224, 86, 146, 20, '06 · 808 fumée  [ x ]', acc, acc);
        boxT(986, 86, 54, 20, '-1 OCT', ink, ink); boxT(1040, 86, 36, 20, '0 ST', ink, ink);
        boxT(1088, 86, 152, 20, '06 · 808 fumée…  [ x ]', acc, acc);
        // cadres pointillés
        const dashT = (x, y, w, h, txt) => {
            g.setLineDash([3, 3]); g.strokeStyle = ink; g.lineWidth = 1.2; g.strokeRect(x, y, w, h); g.setLineDash([]);
            g.fillStyle = ink; g.font = font('MonoM', 10.5); g.textAlign = 'center'; spaced(g, txt, x + w / 2, y + h / 2 + 4, 1.5, 'center');
        };
        dashT(40, 438, 160, 24, '[ SYNC : 90° ]'); dashT(208, 438, 162, 24, '[ WARP : CLASSIQUE ]');
        dashT(910, 438, 160, 24, '[ SYNC : 0° ]'); dashT(1078, 438, 162, 24, '[ WARP : CLASSIQUE ]');
        dashT(724, 362, 144, 28, 'CIBLE : FILTRE ▾');
        // légende de la VIE
        g.fillStyle = muted; g.font = font('Mono', 10.5);
        g.fillText('jeune', 411, 380);
        g.fillText('ancienne', 566, 380);
        const ages = [acc, '#D04000', '#A03300', '#702400', '#401A0A', ink];
        ages.forEach((c, i) => { g.fillStyle = c; g.beginPath(); g.arc(462 + i * 18, 376, 6, 0, Math.PI * 2); g.fill(); });
        // lit de la matrice de LED
        g.fillStyle = TH.lifeBg; g.strokeStyle = TH.lifeLine || muted; g.lineWidth = 1;
        g.beginPath(); g.roundRect(412, 132, 456, 216, 6); g.fill(); g.stroke();
        // bandeau des macros : séparateurs, voyants, textes, glissières
        const mac = [['HARMONIQUES', 'grille → timbre', '+25 %'], ['ESPACE', 'grille → position', '-15 %'], ['TEXTURE', 'grille → grains', '+10 %']];
        mac.forEach(([a, b, v], i) => {
            const x0 = 24 + i * 410.67;
            if (i) { g.strokeStyle = TH.line; g.beginPath(); g.moveTo(x0, 586); g.lineTo(x0, 635); g.stroke(); }
            g.fillStyle = acc; g.fillRect(x0 + 21, 601, 10, 10);
            g.fillStyle = ink; g.font = font('MonoM', 12); spaced(g, a, x0 + 43, 607, 1.8);
            g.fillStyle = muted; g.font = font('Mono', 11); g.fillText(b, x0 + 43, 623);
            g.fillStyle = ink; g.font = font('MonoB', 12); spaced(g, v, x0 + 390, 608, 0.6, 'right');
            g.strokeStyle = muted; g.lineWidth = 2; g.beginPath(); g.moveTo(x0 + 318, 622); g.lineTo(x0 + 390, 622); g.stroke();
        });
        // étiquettes et graduations des potards
        const kn = [
            [80, 495, 21, 'POSITION'], [163, 495, 21, 'WARP'], [246, 495, 21, 'PAN'], [330, 495, 21, 'NIVEAU'],
            [490, 470, 21, 'VITESSE'], [645, 470, 21, 'NAISSANCE'], [795, 470, 21, 'QUANTITÉ'],
            [950, 495, 21, 'POSITION'], [1033, 495, 21, 'WARP'], [1116, 495, 21, 'PAN'], [1200, 495, 21, 'NIVEAU'],
            [785, 785, 30, 'MASTER'],
        ];
        for (const [x, y, r, lab] of kn) {
            g.strokeStyle = TH.tickOff || muted; g.lineWidth = 1.3;
            for (let k = 0; k <= 24; ++k) {
                const a = (-135 + k * 11.25) * Math.PI / 180;
                const r1 = r + 5, r2 = r + (k % 6 === 0 ? 10 : 8);
                g.beginPath(); g.moveTo(x + Math.sin(a) * r1, y - Math.cos(a) * r1); g.lineTo(x + Math.sin(a) * r2, y - Math.cos(a) * r2); g.stroke();
            }
            g.fillStyle = muted; g.font = font('Mono', 9.5);
            spaced(g, lab, x, y + r + 16, 1.8, 'center');
            knobs.push({ x, y, r });
        }
        g.fillStyle = TH.scrMuted || muted; g.font = font('Mono', 10);
        g.fillText('rien ne change sans ton accord', 872, 910);
        g.setLineDash([3, 3]); g.strokeStyle = TH.scrMuted || muted; g.strokeRect(1008, 670, 142, 20); g.setLineDash([]);
        g.fillStyle = TH.scrText || '#fff'; g.font = font('MonoM', 9.5); spaced(g, '[ CIBLE : BASSE ▾ ]', 1079, 684, 1, 'center');
        g.strokeStyle = acc; g.strokeRect(1160, 670, 72, 20); g.fillStyle = acc; spaced(g, 'MIX · IA', 1196, 684, 1.5, 'center');
    }
    plane(0, 0, 1280, 960, Z0 + 0.001, P0.tex);
    plane(0, 0, 1280, 960, ZC + 0.001, P1.tex);

    // ---------------------------------------------------------------- boutons (touches à capuchon) avec leur texte
    const capMat = std(TH.btnBg, 0.5), capEdge = std(TH.btnEdge || TH.line, 0.5);
    const caps = [];
    function cap(px, py, pw, ph, z0, label, { mat = capMat, color = ink, size = 10.5, f = 'MonoM', led = false } = {}) {
        const m = rbox(px, py, pw, ph, CAP, z0, mat, 0.025);
        caps.push({ px, py, pw, ph, z0, label, color, size, f, led });
        return m;
    }
    // en-tête (sur le châssis)
    const logo = rbox(24, 22, 40, 40, 0.1, Z0, std(TH.logoBg, 0.4), 0.02);
    const logoDot = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.15, 0.03), new THREE.MeshBasicMaterial({ color: hot.clone().multiplyScalar(0.55) }));
    logoDot.position.set(X(44), Y(42), Z0 + 0.115);
    group.add(logoDot);
    cap(190, 24, 22, 38, Z0, '');
    const tabs = ['SON', 'JEU', 'SYNTH', 'VIE', 'MODULES', 'ENV / LFO', 'EFX', 'MIX IA', 'WAVETABLE'];
    {
        const ws = tabs.map(s => 20 + s.length * 6.6), tot = ws.reduce((a, b) => a + b, 0), k = (456 - 2 * (tabs.length - 1)) / tot;
        let x = 228;
        tabs.forEach((s, i) => { const w = ws[i] * k; cap(x, 23, w, 38, Z0, s, { size: 9.5, led: i === 0 }); x += w + 2; });
    }
    cap(836, 24, 62, 36, Z0, 'PANIC', { size: 11 });
    cap(920, 22, 36, 40, Z0, '<', { size: 13 });
    cap(1220, 22, 36, 40, Z0, '>', { size: 13 });
    // boutons des panneaux
    cap(411, 407, 152, 32, ZC, 'RESET', { size: 11.5 });
    cap(569, 407, 152, 32, ZC, 'GEL', { size: 11.5 });
    cap(727, 407, 142, 32, ZC, 'SEED ▾', { size: 11.5 });
    cap(459, 541, 32, 17, ZC, 'HZ', { mat: std(TH.tabBg, 0.5), color: TH.tabFg, size: 9 });
    cap(491, 541, 32, 17, ZC, 'BPM', { size: 9, color: muted });
    cap(579, 660, 167, 23, ZC, '→ GLISSER EN .WAV', { size: 10 });
    cap(757, 660, 66, 23, ZC, 'CLAVIER', { size: 10 });
    const accept = cap(872, 847, 175, 39, ZC, '[ ACCEPTER ]', { mat: new THREE.MeshBasicMaterial({ color: col(acc).multiplyScalar(0.9) }), color: TH.onAcc || ink, size: 12.5 });
    cap(1057, 847, 175, 39, ZC, '[ IGNORER ]', { mat: std(darkPanel, 0.45), color: TH.scrText || '#fff', size: 12.5 });
    {
        const g = P2.g;
        for (const c of caps) {
            g.fillStyle = c.color; g.font = font(c.f, c.size);
            if (c.label) spaced(g, c.label, c.px + c.pw / 2, c.py + c.ph / 2 + c.size * 0.36 + (c.led ? 5 : 0), 1.2, 'center');
        }
        // bouton de thème : petit carré
        g.fillStyle = acc; g.fillRect(195, 30, 12, 12);
    }
    // deux plans : dessus des boutons de l'en-tête et dessus des boutons des panneaux
    plane(0, 0, 1280, 960, Z0 + CAP + 0.001, P2.tex);
    plane(0, 0, 1280, 960, ZC + CAP + 0.001, P2.tex);
    const tabLed = new THREE.Mesh(new THREE.SphereGeometry(0.028, 12, 8), new THREE.MeshBasicMaterial({ color: hot.clone() }));
    tabLed.position.set(X(228 + 14), Y(31), Z0 + CAP + 0.01);
    group.add(tabLed);

    // ---------------------------------------------------------------- afficheurs (fond noir, texte lumineux)
    const displays = [];
    function display(px, py, pw, ph, z0, draw) {
        rbox(px, py, pw, ph, 0.03, z0, std(TH.scr, 0.3, 0.2), 0.01);
        const d = canvasTex(pw, ph, 3);
        const m = plane(px + 1, py + 1, pw - 2, ph - 2, z0 + 0.031, d.tex, { transparent: false });
        const o = { d, draw, last: '' };
        displays.push(o);
        return o;
    }
    const scrText = TH.scrText || '#FFFFFF', scrMuted = TH.scrMuted || '#7A7A75';
    function txt(g, s, x, y, c, f, sz, ls = 0.8, align = 'left') { g.fillStyle = c; g.font = font(f, sz); spaced(g, s, x, y, ls, align); }
    const dCpu = display(704, 22, 124, 40, Z0, (g, w, h, st) => {
        g.fillStyle = TH.scr; g.fillRect(0, 0, w, h);
        txt(g, `CPU: ${st.cpu.toFixed(1)}%`, 9, 16, scrText, 'MonoM', 10.5);
        for (let i = 0; i < 5; ++i) { g.fillStyle = i < Math.round(st.cpu * 1.3) ? acc : TH.scrGrid; g.fillRect(84 + i * 5, 8, 3, 9); }
        txt(g, '64 SAMPLES', 9, 31, scrMuted, 'Mono', 9.5);
    });
    const dPill = display(962, 22, 252, 40, Z0, (g, w, h, st) => {
        g.fillStyle = TH.scr; g.fillRect(0, 0, w, h);
        txt(g, st.preset[0], 36, 25, acc, 'MonoM', 12.5);
        txt(g, ' · ' + st.preset[1] + ' ▾', 36 + 18, 25, scrText, 'MonoM', 12.5);
    });
    const dVie = display(676, 86, 192, 20, ZC, (g, w, h, st) => {
        g.fillStyle = TH.scr; g.fillRect(0, 0, w, h);
        txt(g, `B3/S23 · GEN ${String(st.gen).padStart(4, '0')} · ${String(st.alive).padStart(3, '0')}`, 9, 14, scrText, 'MonoM', 9.5, 0.6);
        g.fillStyle = st.kick > 0.3 ? acc : scrText; g.beginPath(); g.arc(180, 10, 3.2, 0, 7); g.fill();
    });
    const valueBoxes = [
        [53, 541, 54, 18], [136, 541, 54, 18], [219, 541, 54, 18], [302, 541, 56, 18],
        [465, 516, 50, 18], [620, 516, 50, 18], [770, 516, 50, 18],
        [923, 541, 54, 18], [1006, 541, 54, 18], [1089, 541, 54, 18], [1171, 541, 58, 18],
        [757, 840, 56, 18],
    ].map(([x, y, w, h]) => display(x, y, w, h, ZC, (g, W, H, st, i) => {
        g.fillStyle = TH.scr; g.fillRect(0, 0, W, H);
        txt(g, st.values[i], W / 2, 13, scrText, 'MonoM', 10, 0.4, 'center');
    }));

    // ---------------------------------------------------------------- écrans des oscillateurs (verre + image animée)
    const screens = [];
    for (const [x0] of [[40], [910]]) {
        const y0 = 127, w = 330, h = 305;
        // cadre en relief autour de l'écran
        for (const [x, y, ww, hh] of [[x0 - 4, y0 - 4, w + 8, 4], [x0 - 4, y0 + h, w + 8, 4], [x0 - 4, y0, 4, h], [x0 + w, y0, 4, h]]) rbox(x, y, ww, hh, 0.03, ZC, std(TH.line, 0.4), 0.005);
        const d = canvasTex(w, h, 2);
        plane(x0, y0, w, h, ZC + 0.002, d.tex, { transparent: false });
        // verre : reflet discret de l'environnement
        const glass = new THREE.Mesh(new THREE.PlaneGeometry(w * U, h * U), noBloom(new THREE.MeshStandardMaterial({ color: 0xffffff, metalness: 1, roughness: 0.12, transparent: true, opacity: 0.1, depthWrite: false })));
        glass.position.set(X(x0 + w / 2), Y(y0 + h / 2), ZC + 0.028);
        group.add(glass);
        screens.push({ d, w, h });
    }
    // texte de la PROPOSITION (écran du panneau sombre)
    const propo = canvasTex(380, 120, 2);
    plane(868, 706, 380, 120, ZC + 0.002, propo.tex, { transparent: false });

    // ---------------------------------------------------------------- potards
    const knobMeshes = knobs.map(({ x, y, r }) => {
        const g = new THREE.Group();
        const R = r * U;
        const skirt = new THREE.Mesh(new THREE.CylinderGeometry(R * 1.08, R * 1.14, 0.035, 40), std(TH.knobLine, 0.5, 0.2));
        skirt.rotation.x = Math.PI / 2; skirt.position.z = 0.0175;
        const body = new THREE.Mesh(new THREE.CylinderGeometry(R * 0.94, R, 0.17, 40), std(TH.knobFace, 0.38, 0.25));
        body.rotation.x = Math.PI / 2; body.position.z = 0.035 + 0.085;
        const top = new THREE.Mesh(new THREE.CylinderGeometry(R * 0.8, R * 0.92, 0.02, 40), std(TH.knobFace, 0.25, 0.35));
        top.rotation.x = Math.PI / 2; top.position.z = 0.215;
        const ptr = new THREE.Mesh(new THREE.BoxGeometry(R * 0.11, R * 0.62, 0.012), new THREE.MeshBasicMaterial({ color: col(TH.ptr || acc).multiplyScalar(1.6) }));
        ptr.position.set(0, R * 0.42, 0.232);
        g.add(skirt, body, top, ptr);
        g.position.set(X(x), Y(y), ZC);
        group.add(g);
        return g;
    });

    // ---------------------------------------------------------------- glissières des macros
    const sliders = [0, 1, 2].map(i => {
        const m = new THREE.Mesh(new RoundedBoxGeometry(0.1, 0.16, 0.08, 2, 0.02), std(TH.ink, 0.4, 0.3));
        m.position.set(X(24 + i * 410.67 + 318), Y(622), ZC + 0.04);
        group.add(m);
        return { m, x0: 24 + i * 410.67 + 318 };
    });

    // ---------------------------------------------------------------- matrice de LED de la VIE (37 × 18)
    const LC = 37, LR = 18;
    const gens = lifeGens(LC, LR, 420, seed);
    const domeGeo = new THREE.SphereGeometry(0.046, 12, 6, 0, Math.PI * 2, 0, Math.PI / 2);
    domeGeo.rotateX(Math.PI / 2);
    const domes = new THREE.InstancedMesh(domeGeo, noBloom(new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.3, metalness: 0 })), LC * LR);
    const glowGeo = new THREE.SphereGeometry(0.05, 12, 6, 0, Math.PI * 2, 0, Math.PI / 2);
    glowGeo.rotateX(Math.PI / 2);
    const glows = new THREE.InstancedMesh(glowGeo, new THREE.MeshBasicMaterial({ color: 0xffffff }), LC * LR);
    group.add(domes, glows);
    const ledPos = [];
    for (let j = 0; j < LR; ++j) for (let i = 0; i < LC; ++i) ledPos.push([X(424 + i * 12.2), Y(142 + j * 11.6)]);
    const Mt = new THREE.Matrix4(), Qn = new THREE.Quaternion(), Sv = new THREE.Vector3(), Pv = new THREE.Vector3();
    const deadC = col(TH.dead), accC = col(acc);
    const ageC = [0.9, 0.42, 0.24, 0.13, 0.07, 0.035].map(k => hot.clone().multiplyScalar(k));  // jeune → ancienne

    // ---------------------------------------------------------------- clavier : rail, 14 touches blanches, 10 noires
    rbox(40, 706, 684, 192, 0.05, ZC, std('#' + col(TH.railBg || TH.chassis).lerp(col(TH.ink), 0.18).getHexString(), 0.6), 0.05);  // rail plus sombre que les touches
    const ZK = ZC + 0.05;
    const keyAtlas = canvasTex(14 * 44 + 10 * 32, 112, 2);
    const whites = [], blacks = [];
    const wNames = ['C3', 'D', 'E', 'F', 'G', 'A', 'B', 'C4', 'D', 'E', 'F', 'G', 'A', 'B'];
    const wPc = [0, 2, 4, 5, 7, 9, 11, 0, 2, 4, 5, 7, 9, 11];
    const bX = [97, 144, 238, 285, 332, 428, 475, 569, 616, 663];
    const bNames = ['C#', 'D#', 'F#', 'G#', 'A#', 'C#', 'D#', 'F#', 'G#', 'A#'];
    const bPc = [1, 3, 6, 8, 10, 1, 3, 6, 8, 10];
    {
        const g = keyAtlas.g;
        for (let i = 0; i < 14; ++i) {
            g.fillStyle = muted; g.font = font('MonoM', 10); g.textAlign = 'center';
            g.fillText(String(i + 1), i * 44 + 22, 16);
            g.fillStyle = ink; g.font = font('MonoM', 11);
            g.fillText(wNames[i], i * 44 + 22, 100);
        }
        for (let i = 0; i < 10; ++i) { g.fillStyle = '#FFFFFF'; g.font = font('MonoM', 9.5); g.fillText(bNames[i], 616 + i * 32 + 16, 44); }
        g.textAlign = 'left';
    }
    const atlasW = 14 * 44 + 10 * 32;
    function keyTop(w, h, u0, u1, v0, v1) {
        const geo = new THREE.PlaneGeometry(w, h);
        const uv = geo.attributes.uv;
        for (let i = 0; i < uv.count; ++i) uv.setXY(i, uv.getX(i) ? u1 : u0, uv.getY(i) ? v1 : v0);
        return new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ map: keyAtlas.tex, transparent: true, depthWrite: false }));
    }
    const keyNat = std(TH.keyNat || '#F4F4F0', 0.45), keySharp = std(TH.keySharp || '#1A1A1A', 0.4, 0.1);
    for (let i = 0; i < 14; ++i) {
        const x = 53 + i * 47.2, w = 42, y = 781, h = 109;
        const k = new THREE.Group();
        const mat = noBloom(keyNat.clone());
        const b = new THREE.Mesh(new RoundedBoxGeometry(w * U, h * U, 0.16, 3, 0.05), mat);
        b.position.z = 0.08;
        const lab = keyTop(w * U, h * U, (i * 44) / atlasW, (i * 44 + 44) / atlasW, 0, 1);
        lab.position.z = 0.161;
        k.add(b, lab);
        k.position.set(X(x + w / 2), Y(y + h / 2), ZK);
        group.add(k);
        whites.push({ k, mat, pc: wPc[i], oct: i < 7 ? 0 : 1 });
    }
    for (let i = 0; i < 10; ++i) {
        const w = 30, h = 54, x = bX[i] - w / 2, y = 716;
        const k = new THREE.Group();
        const b = new THREE.Mesh(new RoundedBoxGeometry(w * U, h * U, 0.15, 3, 0.04), keySharp);
        b.position.z = 0.075;
        const lab = keyTop(w * U, h * U * 0.6, (616 + i * 32) / atlasW, (616 + i * 32 + 32) / atlasW, 0.5, 0.95);
        lab.position.set(0, -0.06, 0.151);
        const led = new THREE.Mesh(new THREE.SphereGeometry(0.03, 10, 6), new THREE.MeshBasicMaterial({ color: hot.clone() }));
        led.position.set(0, 0.17, 0.155);
        k.add(b, lab, led);
        k.position.set(X(bX[i]), Y(y + h / 2), ZK);
        group.add(k);
        blacks.push({ k, led, pc: bPc[i], oct: i < 5 ? 0 : 1 });
    }

    // ---------------------------------------------------------------- dessin des écrans
    function drawScope(sc, data, colr, mode, st) {
        const { g, tex } = sc.d, w = sc.w, h = sc.h;
        g.fillStyle = TH.scr; g.fillRect(0, 0, w, h);
        g.strokeStyle = TH.scrGrid; g.lineWidth = 1;
        for (let i = 1; i < 8; ++i) { g.beginPath(); g.moveTo((w * i) / 8, 0); g.lineTo((w * i) / 8, h); g.stroke(); }
        for (let i = 1; i < 6; ++i) { g.beginPath(); g.moveTo(0, (h * i) / 6); g.lineTo(w, (h * i) / 6); g.stroke(); }
        g.strokeStyle = TH.scrMid || TH.scrGrid; g.beginPath(); g.moveTo(0, h / 2); g.lineTo(w, h / 2); g.stroke();
        g.fillStyle = scrMuted; g.font = font('Mono', 9);
        g.fillText('+6dB', 8, 14); g.fillText('0dB', 8, h / 2 - 4); g.fillText('-6dB', 8, h * 0.66);
        if (mode === 'wurli') {
            const env = st.wurli.env, wave = st.wurli.wave, n = env.length;
            g.fillStyle = acc;
            for (let i = 0; i < n; ++i) {
                const a = Math.pow(env[i], 1.2) * h * 0.42, x = 46 + (i / n) * (w - 60);
                g.fillRect(x, h / 2 - a, (w - 60) / n + 0.6, 2 * a);
            }
            const px = 46 + st.play * (w - 60);
            g.fillStyle = acc; g.fillRect(px, 18, 2, h - 30);
            txt(g, 'WURLITZER EP200', w - 10, 16, scrMuted, 'Mono', 9.5, 1, 'right');
            txt(g, 'ZONE C#4 (C4–D4) · 36 ZONES · 2.87 s', w / 2, h - 12, scrText, 'MonoM', 9.5, 0.6, 'center');
        } else if (mode === 'filter') {
            const cut = st.cut;
            g.strokeStyle = colr; g.lineWidth = 2.5; g.beginPath();
            for (let i = 0; i <= 200; ++i) {
                const x = i / 200, f = Math.pow(10, 1.3 + x * 3), fc = Math.pow(10, 1.3 + cut * 3);
                const r = f / fc, q = 2.2;
                const mag = 1 / Math.sqrt(Math.pow(1 - r * r, 2) + Math.pow(r / q, 2));
                const db = 20 * Math.log10(mag) - (r > 1 ? 24 * Math.log2(r) * 0.5 : 0);
                const y = h / 2 - clamp(db, -40, 14) * (h / 70);
                i ? g.lineTo(x * w, y) : g.moveTo(x * w, y);
            }
            g.stroke();
            txt(g, `LP 24 · CUTOFF ${(Math.pow(10, 1.3 + cut * 3) / 1000).toFixed(2)} kHz · RÉSO 30 %`, w / 2, h - 12, scrText, 'MonoM', 9.5, 0.6, 'center');
        } else {
            const d = data || [], n = d.length;
            g.strokeStyle = colr; g.lineWidth = 2.6; g.lineJoin = 'round'; g.beginPath();
            for (let i = 0; i < n; ++i) {
                const x = 8 + (i / (n - 1)) * (w - 16), y = h / 2 - clamp(d[i] * 1.6, -1, 1) * h * 0.38;
                i ? g.lineTo(x, y) : g.moveTo(x, y);
            }
            g.stroke();
            txt(g, st.scopeLabel, w - 10, 16, scrMuted, 'Mono', 9, 1, 'right');
        }
        tex.needsUpdate = true;
    }
    function drawPropo(st) {
        const { g, tex } = propo, w = 380, h = 120;
        g.fillStyle = darkPanel; g.fillRect(0, 0, w, h);
        txt(g, 'ROLE : BASSE // ANALYSE SPECTRE : 20Hz-250Hz', 4, 14, scrMuted, 'Mono', 9.5, 0.6);
        const full = ['> Réseau : BASSE 100 %. Aucune des 33', 'retouches essayées ne l\'en rapproche.', 'Relancer l\'analyse ?'];
        let left = Math.floor(st.typed), y = 42;
        full.forEach((ln, i) => {
            const s = ln.slice(0, Math.max(0, left));
            left -= ln.length;
            if (s) txt(g, s, 4, y, i === 2 ? acc : scrText, 'MonoM', 12, 0.3);
            y += 23;
        });
        if (Math.floor(st.t * 3) % 2 === 0) { g.fillStyle = acc; g.fillRect(4 + Math.min(1, st.typed / 110) * 160, 94, 8, 14); }
        tex.needsUpdate = true;
    }

    // ---------------------------------------------------------------- état par défaut
    const base = {
        t: 0, kick: 0, A: { rms: 0.5, low: 0.5, mid: 0.5, high: 0.5, flux: 0 }, chroma: null, scopeA: null, scopeB: null,
        mode: 'scope', play: 0, cut: 0.5, preset: ['06', '808 fumée'], wurli: null, scopeLabel: 'F0: 65.40 Hz // RMS: -4.7 dB',
    };
    const fmtPct = v => `${Math.round(v * 100)} %`;
    let drawnOnce = false;

    function update(state) {
        const st = Object.assign({}, base, state);
        const A = st.A, t = st.t;
        // potards : légère dérive + musique (valeurs affichées comme dans le plugin)
        const kv = [
            0.25 + 0.2 * A.mid, 0.25 + 0.25 * A.high, 0.43 + 0.1 * Math.sin(t * 0.9), 0.75 + 0.15 * A.rms,
            0.4 + 0.2 * Math.sin(t * 0.7), 0.18 + 0.3 * A.low, 0.35 + 0.2 * A.mid,
            0.4 + 0.25 * A.low, 0.25 + 0.3 * A.high, 0.6 + 0.1 * Math.sin(t * 1.1 + 1), 0.6 + 0.2 * A.rms,
            0.7 + 0.2 * A.rms,
        ];
        knobMeshes.forEach((k, i) => { k.rotation.z = -(-135 + 270 * clamp(kv[i])) * Math.PI / 180; });
        const db = v => `${(-24 + 24 * v).toFixed(1)} dB`;
        const pan = v => { const p = Math.round((v - 0.5) * 40); return p < 0 ? `L ${-p}` : p > 0 ? `R ${p}` : 'C'; };
        st.values = [fmtPct(kv[0]), fmtPct(kv[1]), pan(kv[2]), db(kv[3]), `${(0.2 + kv[4] * 3).toFixed(1)} Hz`, fmtPct(kv[5]), fmtPct(kv[6]),
                     fmtPct(kv[7]), fmtPct(kv[8]), pan(kv[9]), db(kv[10]), db(kv[11])];
        // glissières des macros
        sliders.forEach((s, i) => { s.m.position.x = X(s.x0 + 72 * clamp([0.62, 0.38, 0.55][i] + 0.25 * [A.mid, A.low, A.high][i] - 0.12)); });
        // matrice de LED
        const gi = Math.min(gens.length - 1, Math.floor(t * 8)), G = gens[gi];
        let alive = 0;
        for (let i = 0; i < LC * LR; ++i) {
            const a = G[i];
            const [x, y] = ledPos[i];
            Pv.set(x, y, ZC + 0.001); Sv.set(1, 1, 1); Mt.compose(Pv, Qn, Sv);
            domes.setMatrixAt(i, Mt);
            domes.setColorAt(i, a ? accC : deadC);
            Sv.setScalar(a ? 1 : 0); Mt.compose(Pv, Qn, Sv);
            glows.setMatrixAt(i, Mt);
            if (a) { ++alive; glows.setColorAt(i, ageC[Math.min(a - 1, 5)]); } else glows.setColorAt(i, deadC);
        }
        domes.instanceMatrix.needsUpdate = glows.instanceMatrix.needsUpdate = true;
        domes.instanceColor.needsUpdate = glows.instanceColor.needsUpdate = true;
        // clavier : les notes présentes dans la musique (chroma) enfoncent les touches
        const ch = st.chroma || new Array(12).fill(0);
        const order = [...ch.keys()].sort((a, b) => ch[b] - ch[a]);
        const press = pc => {
            const rank = order.indexOf(pc);
            return rank < 3 ? clamp((ch[pc] - 0.55) / 0.35) : 0;
        };
        for (const w of whites) {
            const p = press(w.pc) * (w.oct === (order[0] === w.pc ? 0 : 1) ? 1 : 0.0);
            w.k.position.z = ZK - 0.045 * p;
            w.mat.emissive.copy(hot).multiplyScalar(p * 0.5);
        }
        for (const b of blacks) {
            const p = press(b.pc) * (b.oct === (order[0] === b.pc ? 0 : 1) ? 1 : 0);
            b.k.position.z = ZK - 0.04 * p;
            b.led.material.color.copy(hot).multiplyScalar(0.25 + 0.9 * Math.max(p, ch[b.pc] * 0.4));
        }
        // voyants qui tapent sur la grosse caisse
        tabLed.material.color.copy(hot).multiplyScalar(0.5 + 1.0 * st.kick);
        logoDot.material.color.copy(hot).multiplyScalar(0.35 + 0.9 * st.kick);
        strip.material.color.copy(hot).multiplyScalar(0.45 + 1.2 * st.kick);
        if (light && drawnOnce) return;
        // écrans et afficheurs
        drawScope(screens[0], st.scopeA, TH.waveA, st.mode === 'wurli' ? 'wurli' : 'scope', st);
        drawScope(screens[1], st.scopeB, TH.waveB, st.mode === 'filter' ? 'filter' : 'scope', st);
        drawPropo(Object.assign(st, { typed: (t * 26) % 140 }));
        const dst = Object.assign(st, { cpu: 1.6 + 2.4 * A.rms + 0.4 * Math.sin(t * 13), gen: gi, alive });
        displays.forEach((o, i) => {
            const vi = valueBoxes.indexOf(o);
            o.d.g.save();
            o.draw(o.d.g, o.d.c.width / 3, o.d.c.height / 3, dst, vi);
            o.d.g.restore();
            o.d.tex.needsUpdate = true;
        });
        drawnOnce = true;
    }
    for (const p of [P0, P1, P2, keyAtlas]) p.tex.needsUpdate = true;
    update({});
    const uiPt = (px, py) => new THREE.Vector3(X(px), Y(py), ZC);
    return { group, update, uiPt, strip };
}
