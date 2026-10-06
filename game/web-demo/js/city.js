"use strict";
// ============================================================================
// Bâtiments
// ============================================================================
const WALL_COLORS = [0xa3a0aa, 0x8e8e9a, 0xb5a8a3, 0x7c7e8a, 0xadacb2, 0x9d9298, 0xb9b1a6, 0x98a0ab];
function wallMats(color) {
  const key = "W" + color;
  if (!MATS[key]) MATS[key] = [nightMat(texMat(TEX.window, { color, emissive: 0xffffff, emissiveMap: TEX.windowLit }), 0, 1.0), lambert(new THREE.Color(color).multiplyScalar(0.7).getHex())];
  return MATS[key];
}
function bodyBox(x0, x1, z0, z1, h, color) {
  const sx = x1 - x0, sz = z1 - z0, geo = new THREE.BoxGeometry(sx, h, sz), uv = geo.attributes.uv;
  const faceW = [sz, sz, 0, 0, sx, sx], rows = Math.max(1, Math.round(h / 3.2));
  for (let f = 0; f < 6; f++) for (let v = 0; v < 4; v++) { const i = f * 4 + v; if (faceW[f]) uv.setXY(i, (uv.getX(i) * Math.max(1, Math.round(faceW[f] / 2.8))) / 4, (uv.getY(i) * rows) / 4); }
  geo.clearGroups(); geo.addGroup(0, 12, 0); geo.addGroup(12, 12, 1); geo.addGroup(24, 12, 0);
  const [wall, roof] = wallMats(color);
  addGrouped(geo, [wall, roof], new THREE.Matrix4().makeTranslation((x0 + x1) / 2, h / 2, (z0 + z1) / 2));
}
function roofDetails(x0, x1, z0, z1, h) {
  const sx = x1 - x0, sz = z1 - z0, cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
  const parapet = lambert(0x6b6a75), equip = lambert(0xb8b8bd), dark = lambert(0x2a2830);
  boxW(cx, h + 0.3, z1 - 0.12, sx, 0.6, 0.25, parapet); boxW(cx, h + 0.3, z0 + 0.12, sx, 0.6, 0.25, parapet);
  boxW(x1 - 0.12, h + 0.3, cz, 0.25, 0.6, sz, parapet); boxW(x0 + 0.12, h + 0.3, cz, 0.25, 0.6, sz, parapet);
  const ix = sx / 2 - 1.5, iz = sz / 2 - 1.5;
  if (ix < 0.5 || iz < 0.5) return;
  if (rand() < 0.5) { const hh = range(2.2, 3.2), w = range(2.5, 4), d = range(2.5, 4), x = cx + range(-ix, ix) / 2, z = cz + range(-iz, iz) / 2; boxW(x, h + hh / 2, z, w, hh, d, parapet); collider(x - w / 2, h, z - d / 2, x + w / 2, h + hh, z + d / 2); }
  const n = 1 + Math.floor(rand() * 4);
  for (let i = 0; i < n; i++) boxW(cx + range(-ix, ix), h + 0.35, cz + range(-iz, iz), 1, 0.7, 0.8, equip);
  if (rand() < 0.3) { const ax = cx + range(-ix, ix), az = cz + range(-iz, iz); boxW(ax, h + 0.3, az, 1.6, 0.6, 1.6, dark); addGeo(new THREE.CylinderGeometry(1, 1, 1.7, 14), equip, new THREE.Matrix4().makeTranslation(ax, h + 1.45, az)); }
  if (rand() < 0.6) { const ax = cx + range(-ix, ix), az = cz + range(-iz, iz), hh = range(3, 8); addGeo(new THREE.CylinderGeometry(0.06, 0.06, hh, 6), dark, new THREE.Matrix4().makeTranslation(ax, h + hh / 2, az)); addGeo(new THREE.SphereGeometry(0.16, 8, 6), lambert(0xe5503f, { emissive: 0x9a2a1e }), new THREE.Matrix4().makeTranslation(ax, h + hh, az)); }
}
const FRONT_NORMALS = { "+x": new THREE.Vector3(1, 0, 0), "-x": new THREE.Vector3(-1, 0, 0), "+z": new THREE.Vector3(0, 0, 1), "-z": new THREE.Vector3(0, 0, -1) };
function facadeFrame(rect, front) {
  const [x0, x1, z0, z1] = rect, cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
  const origin = { "+x": [x1, cz], "-x": [x0, cz], "+z": [cx, z1], "-z": [cx, z0] }[front];
  return { F: frame(new THREE.Vector3(origin[0], 0, origin[1]), FRONT_NORMALS[front]), W: front[1] === "x" ? z1 - z0 : x1 - x0 };
}

const SHOPS = {
  ramen: { noren: ["らーめん", "#27315e"], blade: ["ラーメン一番", "#c8322a", "#fff6ea"], sign: ["麺屋 ツイスト", "#f2e6cf", "#2a1d17"], lanterns: 2, menu: ["本日のおすすめ", ["醤油ラーメン 850¥", "味噌ラーメン 900¥", "餃子 400¥", "ビール 500¥"]] },
  sushi: { noren: ["寿司処", "#1d2a3f"], blade: ["すし 波", "#efe2c4", "#1d2a3f"], sign: ["寿司 波乗り", "#d9c09a", "#2a1d17"], wood: true, menu: ["おまかせ", ["にぎり 8貫 2200¥", "ちらし 1600¥", "味噌汁 200¥"]] },
  izakaya: { noren: ["酒", "#5b3626"], blade: ["居酒屋", "#b8261f", "#fff3dc"], sign: ["赤ちょうちん", "#30201a", "#ffcf7a"], lanterns: 6, menu: ["今夜の一品", ["焼き鳥 180¥", "枝豆 350¥", "ハイボール 450¥", "唐揚げ 580¥"]] },
  konbini: { sign: ["TWIST MART\nツイストマート", "#ffffff", "#1f8a52"], stripes: ["#1f8a52", "#f08a24"], vending: true },
  cafe: { sign: ["CAFÉ 喫茶ノイズ", "#3a2a22", "#f4e7d2"], awning: ["#2f5f4e", "#efe6d6"], menu: ["CAFÉ", ["ブレンド 450¥", "抹茶ラテ 520¥", "ケーキ 480¥"]] },
  boutique: { sign: ["FADE ファッション", "#16141a", "#f4f0ea"], plants: true },
  bakery: { sign: ["パン工房 BAKERY", "#f6e7cf", "#8a4a22"], awning: ["#d9772f", "#f6e7cf"], menu: ["焼きたて", ["メロンパン 180¥", "あんぱん 160¥", "クロワッサン 220¥"]] },
  karaoke: { sign: ["カラオケ 歌広場", "#3b1f5e", "#ffd76a"], blade: ["カラオケ", "#6a2fb0", "#ffe57a"] },
  dept: { sign: ["TWISTED 百貨店", "#1c1a22", "#f2e3c8"], banners: [["SALE 50%", "#c8322a", "#ffffff"], ["百貨店", "#f2e3c8", "#1c1a22"]] },
};
const UPPER_SIGNS = [["歯科", "#2f6fc0", "#ffffff"], ["整体", "#2f9e5b", "#ffffff"], ["BAR", "#16141a", "#ff7ab0"], ["英会話", "#f2b631", "#1c1a22"], ["麻雀", "#1d6b4a", "#ffffff"], ["美容室", "#f4f0ea", "#c8322a"]];

function shopFront(F, W, kind) {
  const frameMat = lambert(kind === "boutique" ? 0x1c1a22 : 0x3a3640);
  fplane(F, 0, 1.75, 0.05, W - 0.9, 2.9, MATS["int_" + kind] || (MATS["int_" + kind] = litMat(shopInteriorTex(kind))));
  fbox(F, -W / 2 + 0.22, 1.8, 0.15, 0.44, 3.6, 0.3, frameMat); fbox(F, W / 2 - 0.22, 1.8, 0.15, 0.44, 3.6, 0.3, frameMat);
  fbox(F, 0, 3.45, 0.15, W, 0.32, 0.3, frameMat);
}
const signMat = (tex) => nightMat(texMat(tex, { emissive: 0xffffff, emissiveMap: tex }), 0, 0.95);
function decorateShop(F, W, h, kind) {
  const s = SHOPS[kind];
  shopFront(F, W, kind);
  if (s.wood) { for (let u = -W / 2 + 0.6; u < W / 2 - 0.4; u += 0.35) fbox(F, u, 3.85, 0.12, 0.08, 0.5, 0.1, lambert(0x8a6448), false); }
  if (s.awning) { const t = stripeTex(s.awning[0], s.awning[1]); t.repeat.set(W / 2, 1); fbox(F, 0, 3.95, 0.85, W - 0.4, 0.08, 1.7, texMat(t), true, 0.28); }
  if (s.stripes) { fbox(F, 0, 3.75, 0.08, W, 0.22, 0.06, lambert(new THREE.Color(s.stripes[0]).getHex())); fbox(F, 0, 3.97, 0.08, W, 0.22, 0.06, lambert(new THREE.Color(s.stripes[1]).getHex())); }
  const signY = s.awning ? 4.75 : 4.15, signH = kind === "konbini" ? 1.0 : 0.85;
  if (s.sign && h > 4.4) fplane(F, 0, signY, 0.1, Math.min(W * 0.75, 9), signH, MATS["sign_" + kind] || (MATS["sign_" + kind] = signMat(signTex(s.sign[0], { bg: s.sign[1], fg: s.sign[2], border: s.sign[2], w: 512, h: kind === "konbini" ? 140 : 110 }))));
  else if (s.sign) fplane(F, 0, 3.9, 0.32, Math.min(W * 0.75, 9), 0.7, signMat(signTex(s.sign[0], { bg: s.sign[1], fg: s.sign[2], border: s.sign[2], w: 512, h: 110 })));
  const door = -W / 4;
  if (s.noren) fplane(F, door, 2.85, 0.34, 2.1, 1.05, new THREE.MeshLambertMaterial({ map: norenTex(s.noren[0], s.noren[1]), alphaTest: 0.5, side: THREE.DoubleSide }));
  const red = lambert(0xd8402f, { emissive: 0xff3a20 });
  if (!red.userData.night) { red.userData.night = true; nightMat(red, 0.3, 1.1); }
  if (s.lanterns === 2) for (const du of [-1.4, 1.4]) { fcyl(F, door + du, 2.3, 0.5, 0.2, 0.2, 0.6, red); fbox(F, door + du, 2.95, 0.5, 0.3, 0.06, 0.3, lambert(0x1c1a22), false); }
  if (s.lanterns === 6) for (let i = 0; i < 6; i++) fcyl(F, -W / 2 + 0.9 + (i * (W - 1.8)) / 5, 2.75, 0.45, 0.17, 0.17, 0.5, red);
  if (s.blade && h > 6) bladeSign(F, W / 2 - 0.5, Math.min(h - 2.2, 6.6), 0.75, 0.9, 3.4, signTex(s.blade[0], { bg: s.blade[1], fg: s.blade[2], border: s.blade[2], vertical: true, w: 128, h: 480 }));
  if (s.menu) menuBoard(F, door + 2.2, 1.4, s.menu);
  if (s.vending) for (const du of [W / 2 - 0.9, W / 2 - 1.9]) vendingMachine(F, du, 0.1);
  if (s.plants) for (const du of [-W / 2 + 0.8, W / 2 - 0.8]) pottedPlant(F.p(du, 0, 0.5));
  if (s.banners) s.banners.forEach((b, i) => fplane(F, (i ? 1 : -1) * W / 3, 11, 0.06, 2.6, 13, texMat(signTex(b[0], { bg: b[1], fg: b[2], vertical: true, w: 128, h: 640 }))));
  if (h >= 8 && rand() < 0.7) { const sg = pick(UPPER_SIGNS); bladeSign(F, -W / 2 + 0.5, range(5.5, Math.min(h - 1.5, 9)), 0.6, 0.7, 1.6, signTex(sg[0], { bg: sg[1], fg: sg[2], border: sg[2], vertical: true, w: 96, h: 220 })); }
}
function decorateApartment(F, W, h) {
  fplane(F, 0, 1.3, 0.04, 1.6, 2.4, lambert(0x3d3a44));
  fplane(F, 0, 2.85, 0.06, 2.4, 0.5, texMat(signTex("コーポ " + pick(["森", "桜", "月光", "青葉", "ひばり"]), { bg: "#efe9de", fg: "#3a3640", w: 256, h: 64 })));
  const floors = Math.floor(h / 3.2), bays = Math.max(1, Math.round(W / 4)), bw = W / bays;
  const slab = lambert(0xc9c5c0), rail = lambert(0xe2e0dc), cloth = [0xe9473f, 0x3d7fd9, 0xf2f0ea, 0xf2b631, 0x8ac6a8, 0xf3a6c0];
  for (let f = 1; f < floors; f++) for (let i = 0; i < bays; i++) {
    const u = -W / 2 + bw * (i + 0.5), y = f * 3.2;
    fbox(F, u, y, 0.6, bw - 0.3, 0.14, 1.2, slab);
    fbox(F, u, y + 0.5, 1.17, bw - 0.3, 0.86, 0.06, rail);
    if (rand() < 0.35) for (let k = 0; k < 3; k++) fplane(F, u - 0.7 + k * 0.6, y + 1.45, 0.95, 0.45, 0.6, lambert(pick(cloth), { side: THREE.DoubleSide }));
    if (rand() < 0.4) fbox(F, u + bw / 2 - 0.65, y + 0.38, 0.45, 0.75, 0.6, 0.45, lambert(0xd7d7da));
  }
}
function building(rect, h, front, kind, opts = {}) {
  const [x0, x1, z0, z1] = rect;
  bodyBox(x0, x1, z0, z1, h, opts.color ?? pick(WALL_COLORS));
  collider(x0, 0, z0, x1, h, z1, { chain: opts.chain });
  roofDetails(x0, x1, z0, z1, h);
  if (!front) return;
  const { F, W } = facadeFrame(rect, front);
  if (SHOPS[kind]) decorateShop(F, W, h, kind);
  else if (kind === "apartment") decorateApartment(F, W, h);
}

// Maison : en retrait derrière un muret, toit à deux pentes
function gableRoof(width, depth, height) {
  const a = width / 2, b = depth / 2, v = [];
  const quad = (p, q, r, s) => v.push(...p, ...q, ...r, ...p, ...r, ...s);
  quad([-a, 0, b], [a, 0, b], [a, height, 0], [-a, height, 0]);
  quad([a, 0, -b], [-a, 0, -b], [-a, height, 0], [a, height, 0]);
  v.push(-a, 0, -b, -a, 0, b, -a, height, 0, a, 0, b, a, 0, -b, a, height, 0);
  const geo = new THREE.BufferGeometry(); geo.setAttribute("position", new THREE.Float32BufferAttribute(v, 3));
  geo.setAttribute("uv", new THREE.Float32BufferAttribute(new Array((v.length / 3) * 2).fill(0), 2));
  geo.computeVertexNormals(); return geo;
}
function house(rect, front) {
  const { F, W } = facadeFrame(rect, front), [x0, x1, z0, z1] = rect;
  const depthLot = front[1] === "x" ? x1 - x0 : z1 - z0, setback = 2.6, depth = Math.min(8, depthLot - setback - 1), bw = W - 2;
  const wall = lambert(pick([0xe9e1d2, 0xcfd8e0, 0xd9c7b0, 0xb89c82, 0xe4d9e6])), roofMat = lambert(pick([0x4b4f5e, 0x8a4a3a, 0x5a6b7a, 0x3e3a3f]));
  const wc = -(setback + depth / 2);
  fbox(F, 0, 2.8, wc, bw, 5.6, depth, wall);
  fcollider(F, 0, 3.4, wc, bw, 6.8, depth);
  addGeo(gableRoof(bw + 1.0, depth + 1.0, 2.2), roofMat, F.m(0, 5.6, wc));
  const win = MATS.houseWin || (MATS.houseWin = texMat(TEX.houseWindow));
  for (const v of [1.6, 4.3]) for (const u of [-bw / 4, bw / 4]) if (!(v < 2 && u < 0)) fplane(F, u, v, -setback + 0.02, 1.3, 1.2, win);
  fplane(F, -bw / 4, 1.1, -setback + 0.02, 1.0, 2.2, lambert(0x6b4a32));
  ground(Math.min(x0, x1), Math.max(x0, x1), Math.min(z0, z1), Math.max(z0, z1), 0.13, rand() < 0.5 ? "gravel" : "grass", rand() < 0.5 ? TEX.gravel : TEX.grass, 3);
  const low = lambert(0x9c958c);
  for (const s of [-1, 1]) { const len = W / 2 - 1.0; fbox(F, s * (0.9 + len / 2), 0.6, -0.15, len, 1.2, 0.2, low); fcollider(F, s * (0.9 + len / 2), 0.6, -0.15, len, 1.2, 0.2); }
  for (let i = 0; i < 2; i++) pottedPlant(F.p(range(-W / 2 + 1.5, W / 2 - 1.5), 0.13, -range(0.8, 2)));
  if (rand() < 0.5) tree(F.p(W / 2 - 1.4, 0, -setback / 2 - 0.4), rand() < 0.5 ? "sakura" : "ginkgo", 0.7);
}

// Remplit l'intérieur d'un pâté de maisons (vu des toits et des trouées)
function fillBlock(x0, x1, z0, z1) {
  const nx = Math.max(1, Math.round((x1 - x0) / 16)), nz = Math.max(1, Math.round((z1 - z0) / 16));
  for (let i = 0; i < nx; i++) for (let j = 0; j < nz; j++) {
    const a = x0 + ((x1 - x0) * i) / nx, b = x0 + ((x1 - x0) * (i + 1)) / nx, c = z0 + ((z1 - z0) * j) / nz, d = z0 + ((z1 - z0) * (j + 1)) / nz;
    building([a, b, c, d], range(8, 20), null, "filler");
  }
}

// ============================================================================
// Objets de rue
// ============================================================================
const LAMP_POS = []; // têtes de lampadaires : lumières et halos de nuit
function streetLamp(x, z, armDir) {
  const pole = lambert(0x383640), F = frame(new THREE.Vector3(x, 0.12, z), armDir);
  fcyl(F, 0, 0, 0, 0.07, 0.09, 5.2, pole);
  fbox(F, 0, 5.15, 0.55, 0.1, 0.1, 1.2, pole);
  const headMat = lambert(0xf2e8d6, { emissive: 0xffe2b0 });
  if (!headMat.userData.night) { headMat.userData.night = true; nightMat(headMat, 0.12, 3.0); }
  fbox(F, 0, 5.05, 1.1, 0.26, 0.14, 0.5, headMat, false);
  LAMP_POS.push(F.p(0, 4.9, 1.1));
  collider(x - 0.12, 0, z - 0.12, x + 0.12, 5.2, z + 0.12, { noCam: true });
}
// Petite voiture japonaise garée (kei car carrée, ou taxi noir). Phares allumés la nuit.
let CAR_MATS = null;
function parkedCar(x, z, ang, color, taxi = false) {
  if (!CAR_MATS) {
    CAR_MATS = { glass: new THREE.MeshStandardMaterial({ color: 0x1d2633, roughness: 0.15, metalness: 0.6 }), tire: lambert(0x18171c), trim: lambert(0x2a2830), chrome: new THREE.MeshStandardMaterial({ color: 0xc9ced8, roughness: 0.25, metalness: 0.9 }),
      head: nightMat(new THREE.MeshLambertMaterial({ color: 0xfff4dc, emissive: 0xfff0c8 }), 0.05, 1.6), tail: nightMat(new THREE.MeshLambertMaterial({ color: 0xc8322a, emissive: 0xff2a1a }), 0.1, 1.4), sign: nightMat(new THREE.MeshLambertMaterial({ color: 0xf2b631, emissive: 0xffd27a }), 0.1, 1.3) };
  }
  const paint = new THREE.MeshStandardMaterial({ color, roughness: 0.35, metalness: 0.45 }), M = CAR_MATS;
  const F = frame(new THREE.Vector3(x, 0, z), dirVec(ang));
  fbox(F, 0, 0.58, 0, 1.58, 0.62, 3.5, paint);
  fbox(F, 0, 1.2, -0.15, 1.5, 0.66, 2.3, M.glass);
  fbox(F, 0, 1.56, -0.15, 1.52, 0.07, 2.32, paint);
  for (const u of [-0.74, 0.74]) for (const w of [0.85, -0.25, -1.25]) fbox(F, u, 1.2, w, 0.06, 0.66, 0.08, paint);
  fbox(F, 0, 0.36, 1.78, 1.6, 0.2, 0.1, M.trim); fbox(F, 0, 0.36, -1.78, 1.6, 0.2, 0.1, M.trim);
  for (const u of [-0.55, 0.55]) { fbox(F, u, 0.72, 1.76, 0.32, 0.14, 0.04, M.head, false); fbox(F, u, 0.72, -1.76, 0.28, 0.12, 0.04, M.tail, false); }
  fbox(F, 0, 0.62, 1.76, 0.5, 0.18, 0.03, M.chrome, false);
  for (const u of [-0.74, 0.74]) for (const w of [1.1, -1.1]) addGeo(new THREE.CylinderGeometry(0.31, 0.31, 0.22, 16), M.tire, F.m(u, 0.31, w).multiply(new THREE.Matrix4().makeRotationZ(Math.PI / 2)));
  if (taxi) { fbox(F, 0, 1.68, 0.1, 0.5, 0.18, 0.22, M.sign, false); fbox(F, 0, 0.62, 0, 1.6, 0.06, 3.52, M.sign, false); }
  fcollider(F, 0, 0.85, 0, 1.6, 1.7, 3.55, { noCam: true });
}
const wirePoints = [];
function powerPole(x, z) {
  const conc = lambert(0x9a958f), F = frame(new THREE.Vector3(x, 0.12, z), new THREE.Vector3(0, 0, 1));
  fcyl(F, 0, 0, 0, 0.13, 0.18, 9.2, conc);
  fbox(F, 0, 8.2, 0, 2.2, 0.12, 0.12, lambert(0x4a4650));
  fbox(F, 0.0, 6.6, 0.22, 0.5, 0.7, 0.4, lambert(0x8f9399));
  if (rand() < 0.5) fplane(F, 0, 3.0, 0.2, 0.28, 1.0, texMat(signTex(pick(["中央1丁目", "桜町2-4", "電柱 17"]), { bg: "#f2f0ea", fg: "#2a2830", vertical: true, w: 64, h: 220 })));
  collider(x - 0.18, 0, z - 0.18, x + 0.18, 9.2, z + 0.18, { noCam: true });
  return [-0.9, 0, 0.9].map((dx) => new THREE.Vector3(x + dx, 8.32, z));
}
function wiresBetween(polesTops) {
  for (let i = 0; i + 1 < polesTops.length; i++) for (let k = 0; k < 3; k++) {
    const a = polesTops[i][k], b = polesTops[i + 1][k];
    for (let s = 0; s < 12; s++) {
      const t0 = s / 12, t1 = (s + 1) / 12, sag = (t) => -Math.sin(t * Math.PI) * 0.9;
      wirePoints.push(a.clone().lerp(b, t0).add(new THREE.Vector3(0, sag(t0), 0)), a.clone().lerp(b, t1).add(new THREE.Vector3(0, sag(t1), 0)));
    }
  }
}
function vendingMachine(F, u, w) {
  const V = frame(F.p(u, 0, w + 0.45), F.n);
  fbox(V, 0, 0.95, -0.4, 0.95, 1.9, 0.8, lambert(0xdfe5ee));
  fplane(V, 0, 0.98, 0.01, 0.9, 1.8, MATS.vend || (MATS.vend = litMat(TEX.vending)));
  fcollider(V, 0, 0.95, -0.4, 0.95, 1.9, 0.8);
}
function menuBoard(F, u, w, [title, lines]) {
  const tex = menuTex(title, lines), mat = texMat(tex), M = frame(F.p(u, 0.12, w), F.n);
  fbox(M, 0, 0.55, 0.12, 0.62, 1.0, 0.04, mat, true, -0.22);
  fbox(M, 0, 0.55, -0.12, 0.62, 1.0, 0.04, lambert(0x6b4a32), true, 0.22);
}
function pottedPlant(p) {
  addGeo(new THREE.CylinderGeometry(0.28, 0.22, 0.5, 10), lambert(0x8a5a3c), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.25, p.z));
  addGeo(new THREE.IcosahedronGeometry(0.42, 0), lambert(0x4f7f45), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.8, p.z));
}
function tree(p, kind, scale = 1) {
  const trunk = lambert(0x5d4434), leaf = kind === "sakura" ? lambert(0xf2b7c8) : lambert(0xa9c25a);
  const h = 3.2 * scale;
  addGeo(new THREE.CylinderGeometry(0.12 * scale, 0.2 * scale, h, 7), trunk, new THREE.Matrix4().makeTranslation(p.x, p.y + h / 2, p.z));
  const blobs = kind === "sakura" ? 4 : 3;
  for (let i = 0; i < blobs; i++) {
    const r = range(1.1, 1.6) * scale, g = new THREE.IcosahedronGeometry(r, 0);
    const m = new THREE.Matrix4().makeTranslation(p.x + range(-0.9, 0.9) * scale, p.y + h + range(-0.2, 0.9) * scale, p.z + range(-0.9, 0.9) * scale);
    addGeo(g, leaf, m);
  }
  collider(p.x - 0.25, p.y, p.z - 0.25, p.x + 0.25, p.y + h, p.z + 0.25, { noCam: true });
}
function bench(p, angle) {
  const F = frame(p, dirVec(angle)), wood = lambert(0x8a6448), metal = lambert(0x3a3640);
  fbox(F, 0, 0.45, 0, 1.8, 0.08, 0.5, wood); fbox(F, 0, 0.8, -0.24, 1.8, 0.4, 0.06, wood);
  for (const u of [-0.75, 0.75]) fbox(F, u, 0.22, 0, 0.08, 0.45, 0.45, metal);
  fcollider(F, 0, 0.25, 0, 1.8, 0.5, 0.5);
}
function bin(p) { addGeo(new THREE.CylinderGeometry(0.28, 0.25, 0.9, 10), lambert(0x4a6f8a), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.45, p.z)); collider(p.x - 0.3, p.y, p.z - 0.3, p.x + 0.3, p.y + 0.9, p.z + 0.3); }
function bike(p, angle) {
  const F = frame(p, dirVec(angle)), metal = lambert(pick([0xc8322a, 0x2f6fc0, 0xf2f0ea, 0x2f9e5b]));
  for (const u of [-0.55, 0.55]) addGeo(new THREE.TorusGeometry(0.33, 0.035, 6, 18), lambert(0x1c1a22), F.m(u, 0.36, 0, 0, Math.PI / 2).multiply(new THREE.Matrix4().makeRotationY(Math.PI / 2)));
  fbox(F, 0, 0.55, 0, 1.0, 0.05, 0.05, metal, true, 0); fbox(F, 0.15, 0.7, 0, 0.05, 0.4, 0.05, metal); fbox(F, -0.45, 0.85, 0, 0.05, 0.1, 0.45, lambert(0x1c1a22));
}
function cone(p) { addGeo(new THREE.ConeGeometry(0.22, 0.7, 10), lambert(0xf07a2a), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.35, p.z)); addGeo(new THREE.CylinderGeometry(0.15, 0.17, 0.1, 10), lambert(0xf4f0ea), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.38, p.z)); }
function guardRail(x0, z0, x1, z1) {
  const len = Math.hypot(x1 - x0, z1 - z0), F = frame(new THREE.Vector3((x0 + x1) / 2, 0.12, (z0 + z1) / 2), new THREE.Vector3(-(z1 - z0), 0, x1 - x0).normalize());
  const white = lambert(0xeeeeec);
  fbox(F, 0, 0.85, 0, len, 0.08, 0.08, white); fbox(F, 0, 0.45, 0, len, 0.08, 0.08, white);
  for (let u = -len / 2; u <= len / 2 + 0.01; u += len / Math.max(1, Math.round(len / 2))) fbox(F, u, 0.45, 0, 0.08, 0.9, 0.08, white);
  fcollider(F, 0, 0.45, 0, len, 0.9, 0.12);
}
function workBarrier(p, angle) {
  const F = frame(p, dirVec(angle)), t = stripeTex("#f2c531", "#1c1a22"); t.repeat.set(3, 1);
  fbox(F, 0, 0.8, 0, 1.8, 0.25, 0.06, texMat(t)); for (const u of [-0.8, 0.8]) fbox(F, u, 0.45, 0, 0.06, 0.9, 0.5, lambert(0x3a3640));
  fcollider(F, 0, 0.45, 0, 1.8, 0.9, 0.3);
}
function mailbox(p) { addGeo(new THREE.BoxGeometry(0.5, 1.1, 0.5), lambert(0xd8402f), new THREE.Matrix4().makeTranslation(p.x, p.y + 0.55, p.z)); addGeo(new THREE.CylinderGeometry(0.25, 0.25, 0.5, 12, 1, false, 0, Math.PI), lambert(0xd8402f), new THREE.Matrix4().makeTranslation(p.x, p.y + 1.1, p.z).multiply(new THREE.Matrix4().makeRotationZ(Math.PI / 2))); collider(p.x - 0.25, 0, p.z - 0.25, p.x + 0.25, 1.35, p.z + 0.25); }
function torii(p, angle) {
  const F = frame(p, dirVec(angle)), red = lambert(0xc8322a), black = lambert(0x1c1a22);
  for (const u of [-1.7, 1.7]) { fcyl(F, u, 0, 0, 0.2, 0.24, 4.4, red); fcollider(F, u, 2.2, 0, 0.45, 4.4, 0.45); }
  fbox(F, 0, 4.55, 0, 5.2, 0.35, 0.45, black); fbox(F, 0, 4.3, 0, 4.8, 0.22, 0.35, red); fbox(F, 0, 3.55, 0, 4.0, 0.22, 0.28, red);
}
const trafficLamps = [];
function trafficLight(x, z, armDir) {
  const F = frame(new THREE.Vector3(x, 0.12, z), armDir), grey = lambert(0x6b6a75);
  fcyl(F, 0, 0, 0, 0.1, 0.12, 5.6, grey); fbox(F, 0, 5.5, 1.4, 0.12, 0.12, 2.8, grey);
  fbox(F, 0, 5.2, 2.6, 1.3, 0.42, 0.32, lambert(0x2a2830));
  collider(x - 0.14, 0, z - 0.14, x + 0.14, 5.6, z + 0.14, { noCam: true });
  const lamps = [0x2fd06a, 0xf2b631, 0xe9473f].map((c, i) => { const m = new THREE.Mesh(new THREE.CircleGeometry(0.13, 14), new THREE.MeshBasicMaterial({ color: c })); m.position.copy(F.p(-0.4 + i * 0.4, 5.2, 2.6)).addScaledVector(armDir.clone().cross(UP).normalize().cross(UP).negate(), 0); scene.add(m); return m; });
  // les feux regardent vers la rue d'en face
  const look = F.p(0, 5.2, 2.6).add(F.t.clone().multiplyScalar(-10));
  lamps.forEach((m, i) => { m.position.copy(F.p(-0.4 + i * 0.4, 5.2, 2.6)).addScaledVector(F.t, -0.17); m.lookAt(look); });
  trafficLamps.push(lamps);
}

// ============================================================================
// La carte : un carrefour (rue commerçante N-S, rue résidentielle E-O, parc)
// ============================================================================
const MAIN_HALF = 5, CROSS_HALF = 4, WALK = 3, LIMIT = 77;
const SW = 0.12; // hauteur des trottoirs
const SECRET_POS = new THREE.Vector3(15, 12.9, -35);
const ALLEY = { x0: 8, x1: 22, z0: -30, z1: -27 };

function buildCity() {
  // --- Chaussées ---
  ground(-MAIN_HALF, MAIN_HALF, -LIMIT, LIMIT, 0.002, "asphalt", TEX.asphalt, 7);
  ground(-LIMIT, -MAIN_HALF, -CROSS_HALF, CROSS_HALF, 0.002, "asphalt", TEX.asphalt, 7);
  ground(MAIN_HALF, LIMIT, -CROSS_HALF, CROSS_HALF, 0.002, "asphalt", TEX.asphalt, 7);
  const paint = lambert(0xece6d8);
  for (let z = -LIMIT + 2; z < LIMIT; z += 5) if (Math.abs(z) > 10) boxW(0, 0.006, z, 0.15, 0.01, 2.4, paint, false);
  for (let x = -LIMIT + 2; x < LIMIT; x += 5) if (Math.abs(x) > 11) boxW(x, 0.006, 0, 2.4, 0.01, 0.15, paint, false);
  for (const s of [-1, 1]) {
    for (let x = -4.5; x <= 4.6; x += 1) boxW(x, 0.006, s * 5.6, 0.5, 0.01, 3, paint, false);
    for (let z = -3.5; z <= 3.6; z += 1) boxW(s * 6.6, 0.006, z, 3, 0.01, 0.5, paint, false);
    boxW(s * 2.5, 0.006, s * 7.6, 4.8, 0.01, 0.3, paint, false);
    boxW(s * 8.6, 0.006, -s * 2, 0.3, 0.01, 3.8, paint, false);
  }
  const manholeMat = texMat(TEX.manhole, { transparent: true });
  for (const [x, z] of [[2.2, -24], [-2.4, 18], [-30, 1.8], [36, -1.6], [1.8, 52], [-2, -58]]) {
    const g = new THREE.CircleGeometry(0.7, 20); g.rotateX(-Math.PI / 2); addGeo(g, manholeMat, new THREE.Matrix4().makeTranslation(x, 0.01, z), false);
  }

  // --- Trottoirs (12 cm, on monte dessus sans sauter) ---
  const curb = lambert(0xbab7bb);
  for (const [x0, x1, z0, z1] of [[-LIMIT, -MAIN_HALF, -LIMIT, -CROSS_HALF], [MAIN_HALF, LIMIT, -LIMIT, -CROSS_HALF], [-LIMIT, -MAIN_HALF, CROSS_HALF, LIMIT], [MAIN_HALF, LIMIT, CROSS_HALF, LIMIT]]) {
    ground(x0, x1, z0, z1, SW, "tiles", TEX.tiles, 2);
    collider(x0, -1, z0, x1, SW, z1, { noCam: true });
    const xe = x0 < 0 ? x1 : x0, ze = z0 < 0 ? z1 : z0;
    boxW(xe, SW / 2, (z0 + z1) / 2, 0.16, SW + 0.01, z1 - z0, curb, false);
    boxW((x0 + x1) / 2, SW / 2, ze, x1 - x0, SW + 0.01, 0.16, curb, false);
  }
  // pavés de la rue commerçante
  for (const s of [-1, 1]) ground(s < 0 ? -8 : 5, s < 0 ? -5 : 8, -LIMIT, -CROSS_HALF, SW + 0.004, "cobble", TEX.cobble, 1.6);

  // --- Rue commerçante, côté ouest (façades vers +x) ---
  building([-22, -8, -19, -7], 4.8, "+x", "konbini");
  building([-22, -8, -30, -19], 9, "+x", "izakaya");
  building([-22, -8, -41, -30], 12, "+x", "boutique");
  building([-22, -8, -51, -41], 8, "+x", "sushi");
  building([-22, -8, -62, -51], 10.5, "+x", "ramen");
  building([-22, -8, -LIMIT, -62], 18, "+x", "apartment");
  // côté est (façades vers -x), avec la ruelle taguée
  building([8, 24, -27, -7], 24, "-x", "dept", { chain: true, color: 0xb5a8a3 });
  building([8, 22, -40, -30], 12, "-x", "bakery", { chain: true });
  building([22, 24, -30, -27], 12, null, "filler", { chain: true });
  building([8, 22, -52, -40], 15, "-x", "karaoke");
  building([8, 22, -64, -52], 10, "-x", "boutique");
  building([8, 22, -LIMIT, -64], 16, "-x", "apartment");
  ground(ALLEY.x0, ALLEY.x1, ALLEY.z0, ALLEY.z1, SW + 0.004, "metal", TEX.metal, 1.5);
  const tagMat = new THREE.MeshLambertMaterial({ map: TEX.tag, transparent: true, alphaTest: 0.1 });
  const fenceMat = new THREE.MeshLambertMaterial({ map: TEX.fence, transparent: true, alphaTest: 0.3, side: THREE.DoubleSide });
  for (const [z, n] of [[-27, -1], [-30, 1]]) {
    const F = frame(new THREE.Vector3(15, 0, z), new THREE.Vector3(0, 0, n));
    for (const v of [2.2, 6.4, 10.4]) fplane(F, range(-3, 3), v, 0.03, 6, 3, tagMat);
    const fence = TEX.fence.clone(); fence.needsUpdate = true; fence.repeat.set(14, 12);
    fplane(F, 0, 6, 0.05, 14, 12, new THREE.MeshLambertMaterial({ map: fence, transparent: true, alphaTest: 0.3, side: THREE.DoubleSide }));
  }
  workBarrier(new THREE.Vector3(6.6, SW, -31.2), Math.PI / 2);
  cone(new THREE.Vector3(6.4, SW, -25.8)); cone(new THREE.Vector3(7.2, SW, -25.4));

  // --- Sud-ouest : parc, place, café avec terrasse ---
  ground(-40, -8, 7, 40, SW + 0.004, "grass", TEX.grass, 3);
  ground(-24, -8, 7, 24, SW + 0.008, "plaza", TEX.plaza, 4);
  ground(-40, -24, 13.5, 17, SW + 0.008, "gravel", TEX.gravel, 2.5);
  building([-22, -8, 40, 52], 7, "+x", "cafe");
  building([-22, -8, 52, LIMIT], 15, "+x", "apartment");
  // terrasse en bois surélevée (30 cm, on y monte en marchant)
  const deckH = 0.38;
  addGeo(new THREE.BoxGeometry(12, deckH - SW, 6.5), lambert(0x5a4030), new THREE.Matrix4().makeTranslation(-16, SW + (deckH - SW) / 2, 36.75));
  ground(-22, -10, 33.5, 40, deckH + 0.002, "wood", TEX.wood, 2.5);
  collider(-22, 0, 33.5, -10, deckH, 40, { noCam: true });
  for (const [x, z] of [[-19, 36], [-15, 37.5], [-12, 35.5]]) {
    const F = frame(new THREE.Vector3(x, deckH, z), new THREE.Vector3(0, 0, 1));
    fcyl(F, 0, 0, 0, 0.06, 0.06, 0.75, lambert(0x2a2830)); fcyl(F, 0, 0.75, 0, 0.5, 0.5, 0.05, lambert(0xefe6d6));
    fcyl(F, 0, 0.8, 0, 0.03, 0.03, 1.5, lambert(0x2a2830)); addGeo(new THREE.ConeGeometry(1.4, 0.5, 8), lambert(0x2f5f4e), F.m(0, 2.45, 0));
    fcollider(F, 0, 0.4, 0, 1.0, 0.8, 1.0);
  }
  for (const [x, z] of [[-30, 22], [-35, 33], [-26, 10], [-37.5, 9.5], [-29, 30]]) tree(new THREE.Vector3(x, SW, z), "sakura", range(0.9, 1.15));
  for (const [x, z, a] of [[-20, 8.6, 0], [-12, 8.6, 0], [-23.2, 19, Math.PI / 2], [-33, 18.2, Math.PI]]) bench(new THREE.Vector3(x, SW, z), a);
  torii(new THREE.Vector3(-27, SW + 0.008, 15.25), Math.PI / 2);
  bin(new THREE.Vector3(-9.2, SW, 23)); bin(new THREE.Vector3(-23.4, SW, 22.6));
  vendingMachine(frame(new THREE.Vector3(-9.0, SW, 26.5), new THREE.Vector3(1, 0, 0)), 0, -0.45);
  streetLamp(-16, 24.4, new THREE.Vector3(0, 0, -1)); streetLamp(-32, 12.6, new THREE.Vector3(0, 0, 1));

  // --- Sud-est : résidentiel ---
  building([8, 22, 7, 22], 15, "-x", "apartment");
  building([8, 22, 22, 38], 12, "-x", "apartment");
  for (const [a, b] of [[38, 51], [51, 64], [64, LIMIT]]) house([8, 22, a, b], "-x");
  for (const [a, b] of [[22, 33], [33, 44]]) house([a, b, 7, 19], "-z");
  building([44, 58, 7, 19], 12, "-z", "apartment");
  for (const [a, b] of [[58, 68], [68, LIMIT]]) house([a, b, 7, 19], "-z");
  mailbox(new THREE.Vector3(7.4, SW, 21.5));
  // --- Nord : rue résidentielle des deux côtés du carrefour ---
  building([-34, -22, -19, -7], 12, "+z", "apartment");
  for (const [a, b] of [[-46, -34], [-58, -46]]) house([a, b, -19, -7], "+z");
  building([-LIMIT, -58, -19, -7], 15, "+z", "apartment");
  for (const [a, b] of [[24, 36], [36, 48]]) house([a, b, -19, -7], "+z");
  building([48, 62, -19, -7], 13, "+z", "apartment");
  house([62, LIMIT, -19, -7], "+z");
  // --- Sud-ouest le long de la rue résidentielle ---
  for (const [a, b] of [[-52, -40], [-64, -52], [-LIMIT, -64]]) house([a, b, 7, 19], "-z");

  // intérieurs des pâtés de maisons
  fillBlock(-LIMIT, -22, -LIMIT, -19); fillBlock(24, LIMIT, -LIMIT, -19);
  fillBlock(-LIMIT, -40, 19, LIMIT); fillBlock(-40, -22, 40, LIMIT); fillBlock(22, LIMIT, 19, LIMIT);
  // bouts de rue
  building([-14, 14, -92, -LIMIT], 22, "+z", "apartment"); building([-14, 14, LIMIT, 92], 14, "-z", "apartment");
  building([-92, -LIMIT, -12, 12], 14, "+x", "apartment"); building([LIMIT, 92, -12, 12], 16, "-x", "apartment");
  for (const [x0, z0, x1, z1] of [[-LIMIT - 1, -LIMIT, -LIMIT, LIMIT], [LIMIT, -LIMIT, LIMIT + 1, LIMIT], [-LIMIT, -LIMIT - 1, LIMIT, -LIMIT], [-LIMIT, LIMIT, LIMIT, LIMIT + 1]]) collider(x0, -1, z0, x1, 80, z1, { noCam: true });

  // --- Mobilier ---
  for (let z = -70; z <= -12; z += 12) { streetLamp(-5.6, z, new THREE.Vector3(1, 0, 0)); streetLamp(5.6, z + 6, new THREE.Vector3(-1, 0, 0)); }
  for (let z = 14; z <= 70; z += 14) streetLamp(-5.6, z, new THREE.Vector3(1, 0, 0));
  const north = [], south = [];
  for (let x = -70; x <= 70; x += 17) if (Math.abs(x) > 12) north.push(powerPole(x, -4.6));
  wiresBetween(north.filter((p) => p[0].x < 0)); wiresBetween(north.filter((p) => p[0].x > 0));
  for (let z = 14; z <= 70; z += 16) south.push(powerPole(5.7, z));
  wiresBetween(south);
  for (const x of [-66, -45, -31, 30, 52, 66]) tree(new THREE.Vector3(x, SW, 5.7), "ginkgo", range(0.85, 1.05));
  for (const z of [30, 60]) tree(new THREE.Vector3(-6.0, SW, z), "ginkgo", 0.9);
  for (const [x, z, a] of [[-8.6, -14, Math.PI / 2], [-8.8, -12.5, Math.PI / 2], [6.6, 12, 0], [7.0, 13.2, 0], [-6.4, 60, Math.PI / 2]]) bike(new THREE.Vector3(x, SW, z), a);
  guardRail(-20, -4.35, -11, -4.35); guardRail(11, 4.35, 20, 4.35); guardRail(-5.35, 11, -5.35, 20); guardRail(5.35, -20, 5.35, -12);
  for (const z of [-21, -45]) bin(new THREE.Vector3(-5.6, SW, z));
  vendingMachine(frame(new THREE.Vector3(30, SW, 6.1), new THREE.Vector3(0, 0, -1)), 0, -0.45);
  trafficLight(-6.2, -5.4, new THREE.Vector3(1, 0, 0)); trafficLight(6.2, 5.4, new THREE.Vector3(-1, 0, 0));
  // voitures garées le long des trottoirs
  for (const [x, z, a, c, taxi] of [[-62, -2.9, -Math.PI / 2, 0xe8e4dc], [-38, -2.9, -Math.PI / 2, 0x2f5f6e], [28, -2.9, -Math.PI / 2, 0x1d1c22, true], [50, -2.9, -Math.PI / 2, 0xc8402f],
    [-52, 2.9, Math.PI / 2, 0x7a8a9a], [40, 2.9, Math.PI / 2, 0xf2e6c8], [64, 2.9, Math.PI / 2, 0x4f6b4a], [3.5, 30, 0, 0xd9cfc0], [-3.5, 50, Math.PI, 0x8a3b3b], [3.5, -50, 0, 0x1d1c22, true], [-3.5, -22, Math.PI, 0xb5b8c0]])
    parkedCar(x, z, a, c, taxi);
  trafficLight(6.2, -5.4, new THREE.Vector3(0, 0, 1)); trafficLight(-6.2, 5.4, new THREE.Vector3(0, 0, -1));

  flushBatches();
  const wires = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(wirePoints), new THREE.LineBasicMaterial({ color: 0x2a2830 }));
  scene.add(wires);
  // sol lointain, bien en dessous de la ville pour ne jamais passer à travers la chaussée
  const far = new THREE.Mesh(new THREE.PlaneGeometry(5000, 5000, 40, 40), new THREE.MeshLambertMaterial({ color: 0x6d6c78, polygonOffset: true, polygonOffsetFactor: 4, polygonOffsetUnits: 4 }));
  far.rotation.x = -Math.PI / 2; far.position.y = -0.8; scene.add(far);
}

// ============================================================================
// Décor lointain : ville, montagnes, tour, oiseaux
// ============================================================================
const TOWER_POS = new THREE.Vector3(70, 0, -720);
const rings = [], birds = [];
let TOWER_BEACON = null;
function buildBackdrop() {
  const geo = new THREE.BoxGeometry(1, 1, 1); geo.translate(0, 0.5, 0);
  const uv = geo.attributes.uv; for (let i = 0; i < uv.count; i++) if (i < 8 || i >= 16) uv.setXY(i, uv.getX(i) * 1.25, uv.getY(i) * 2);
  const COUNT = 500, mesh = new THREE.InstancedMesh(geo, nightMat(texMat(TEX.window, { emissive: 0xffffff, emissiveMap: TEX.windowLit }), 0, 1.1), COUNT);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), col = new THREE.Color();
  let n = 0, tries = 0;
  while (n < COUNT && tries++ < 8000) {
    const a = rand() * Math.PI * 2, r = Math.sqrt(range(110 * 110, 650 * 650)), x = Math.cos(a) * r, z = Math.sin(a) * r;
    const fx = range(8, 24), fz = range(8, 24);
    if (Math.abs(x) < 95 + fx && Math.abs(z) < 95 + fz) continue;
    const toTower = Math.hypot(x - TOWER_POS.x, z - TOWER_POS.z); if (toTower < 70) continue;
    const tall = Math.min(1, Math.max(0.2, 1 - toTower / 900));
    q.setFromAxisAngle(UP, rand() < 0.5 ? 0 : Math.PI / 2);
    m.compose(new THREE.Vector3(x, 0, z), q, new THREE.Vector3(fx, range(8, 20 + 40 * tall), fz)); mesh.setMatrixAt(n, m);
    col.setHSL(0.7 + rand() * 0.08, 0.06, 0.55 + rand() * 0.12); mesh.setColorAt(n, col); n++;
  }
  mesh.count = n; scene.add(mesh);

  const mount = new THREE.MeshStandardMaterial({ color: 0x9aa0b4, roughness: 1, flatShading: true });
  for (let i = 0; i < 26; i++) {
    const a = (Math.PI * 2 * i) / 26 + range(-0.08, 0.08), r = range(1150, 1500), h = range(220, 480);
    const c = new THREE.Mesh(new THREE.ConeGeometry(h * range(1.2, 1.7), h, 4, 1), mount); c.position.set(Math.cos(a) * r, h / 2, Math.sin(a) * r); c.rotation.y = rand() * Math.PI; scene.add(c);
  }

  const group = new THREE.Group(); group.position.copy(TOWER_POS); scene.add(group);
  const RED = new THREE.Color(0xc8402f), WHITE = new THREE.Color(0xeeeae6), LAT = 250, TOP = 333, LEVELS = 16;
  const corners = (h) => { const w = 2.2 + 31.8 * Math.pow(1 - h / LAT, 2.2); return [[w, w], [-w, w], [-w, -w], [w, -w]].map(([x, z]) => new THREE.Vector3(x, h, z)); };
  const beams = [];
  for (let i = 0; i < LEVELS; i++) {
    const c0 = corners((LAT * i) / LEVELS), c1 = corners((LAT * (i + 1)) / LEVELS), band = Math.floor(i / 2) % 2 === 0 ? RED : WHITE, leg = 1.8 - 1.2 * (i / LEVELS);
    for (let k = 0; k < 4; k++) { const nk = (k + 1) % 4; beams.push([c0[k], c1[k], leg, band], [c1[k], c1[nk], 0.6, band], [c0[k], c1[nk], 0.4, band], [c0[nk], c1[k], 0.4, band]); }
  }
  for (let i = 0; i < 8; i++) beams.push([new THREE.Vector3(0, LAT + ((TOP - LAT) * i) / 8, 0), new THREE.Vector3(0, LAT + ((TOP - LAT) * (i + 1)) / 8, 0), 2.2 - (1.7 * i) / 8, i % 2 ? WHITE : RED]);
  const lattice = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshLambertMaterial(), beams.length);
  beams.forEach(([a, b, t, c], i) => { const d = b.clone().sub(a), len = d.length(); q.setFromUnitVectors(UP, d.divideScalar(len)); m.compose(a.clone().add(b).multiplyScalar(0.5), q, new THREE.Vector3(t, len, t)); lattice.setMatrixAt(i, m); lattice.setColorAt(i, c); });
  group.add(lattice);
  for (const [y, s, h] of [[125, 22, 9], [223, 10, 4]]) { const d = new THREE.Mesh(new THREE.BoxGeometry(s, h, s), lambert(0xeeeae6)); d.position.y = y; group.add(d); }
  const glow = (c, o = 1) => new THREE.MeshBasicMaterial({ color: c, fog: false, transparent: o < 1, opacity: o });
  const beacon = new THREE.Mesh(new THREE.SphereGeometry(1.8, 12, 8), glow(0xfff4ec)); beacon.position.y = TOP; group.add(beacon); TOWER_BEACON = beacon;
  for (const [r, h] of [[22, 318], [36, 305], [52, 292]]) { const ring = new THREE.Mesh(new THREE.TorusGeometry(r, 0.55, 6, 120), glow(0xf2f7ff, 0.95)); ring.rotation.x = Math.PI / 2; ring.position.y = h; group.add(ring); rings.push(ring); }
  const sphere = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.IcosahedronGeometry(78, 2)), new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.22, fog: false }));
  sphere.position.y = 300; group.add(sphere); rings.push(sphere);

  const wingGeo = new THREE.BufferGeometry(); wingGeo.setAttribute("position", new THREE.Float32BufferAttribute([0, 0, -0.15, 0, 0, 0.25, 1, 0, 0], 3));
  const bm = new THREE.MeshBasicMaterial({ color: 0x4a4550, side: THREE.DoubleSide });
  for (let i = 0; i < 24; i++) {
    const b = new THREE.Group(), l = new THREE.Mesh(wingGeo, bm), r = new THREE.Mesh(wingGeo, bm); r.scale.x = -1; b.add(l, r);
    const near = i < 10;
    b.userData = { l, r, center: near ? new THREE.Vector3(range(-30, 30), 0, range(-40, 40)) : TOWER_POS.clone(), radius: near ? range(20, 50) : range(60, 180), height: near ? range(30, 50) : range(120, 280), speed: range(0.08, 0.18) * (rand() < 0.5 ? 1 : -1), phase: rand() * 10 };
    b.scale.setScalar(near ? 0.9 : 3); scene.add(b); birds.push(b);
  }
}

