// Le bonhomme d'ABRASION, mascotte de la marque (point de départ : game/reference/bonhomme_abrasion.png).
// Silhouette : grosse tête ronde de chrome, flottante, séparée du corps ; torse en pilule large ; bras
// et jambes en tubes épais aux bouts ronds. Chrome par matcap (même rendu quelle que soit la lumière).
// Signes distinctifs (ce qui le rend unique) :
//   - une visière orange qui affiche ses yeux en pixels et ses expressions (pas de bouche) ;
//   - le logo ABRASION sur la poitrine (carré noir, carré orange) qui pulse sur le rythme ;
//   - un anneau orange lumineux dans l'espace entre la tête et le corps ;
//   - une queue en câble jack, fiche orange au bout, qui traîne derrière lui.
//   const b = makeBonhomme();  scene.add(b.root);  b.pose(P) à chaque image (voir POSE plus bas)
import * as THREE from 'three';

function matcapTex() {
    const c = document.createElement('canvas');
    c.width = c.height = 256;
    const g = c.getContext('2d');
    g.beginPath(); g.arc(128, 128, 128, 0, Math.PI * 2); g.clip();
    const base = g.createRadialGradient(118, 112, 0, 128, 128, 128);
    base.addColorStop(0, '#8c8f97'); base.addColorStop(0.55, '#4a4c53'); base.addColorStop(0.82, '#7d818c'); base.addColorStop(1, '#eef0f5');
    g.fillStyle = base; g.fillRect(0, 0, 256, 256);
    const sky = g.createLinearGradient(0, 20, 0, 128);
    sky.addColorStop(0, 'rgba(215,222,236,0.6)'); sky.addColorStop(1, 'rgba(215,222,236,0)');
    g.fillStyle = sky; g.fillRect(0, 0, 256, 128);
    const glow = g.createRadialGradient(128, 262, 0, 128, 262, 70);  // reflet orange du sol, au bord seulement
    glow.addColorStop(0, 'rgba(255,92,20,0.5)'); glow.addColorStop(1, 'rgba(255,92,20,0)');
    g.fillStyle = glow; g.fillRect(0, 128, 256, 128);
    const hi = g.createRadialGradient(92, 80, 0, 92, 80, 70);
    hi.addColorStop(0, 'rgba(255,255,255,0.97)'); hi.addColorStop(0.25, 'rgba(255,255,255,0.6)'); hi.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = hi; g.fillRect(0, 0, 256, 256);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    return t;
}

const V = (x = 0, y = 0, z = 0) => new THREE.Vector3(x, y, z);

// Proportions (unités ≈ 1 m) : hauteur totale ≈ 2,3
const R = {
    hipY: 0.74, torsoY: 1.18, torsoR: 0.36, torsoLen: 0.4, torsoSX: 1.22, torsoSZ: 0.86,
    headY: 2.24, headR: 0.39, shoulder: V(0.36, 1.46, 0), armR: 0.155, handR: 0.165,
    hip: V(0.2, 0.74, 0), legR: 0.15, footR: 0.165,
};

export function makeBonhomme() {
    const mc = matcapTex();
    const headMat = new THREE.MeshMatcapMaterial({ matcap: mc, color: 0xffffff });
    const bodyMat = new THREE.MeshMatcapMaterial({ matcap: mc, color: 0xc4c8d0 });
    const root = new THREE.Group();      // position et cap
    const body = new THREE.Group();      // écrasement / étirement, penché
    root.add(body);
    const torso = new THREE.Mesh(new THREE.CapsuleGeometry(R.torsoR, R.torsoLen, 12, 32), bodyMat);
    torso.scale.set(R.torsoSX, 1, R.torsoSZ);
    torso.position.y = R.torsoY;
    body.add(torso);
    const head = new THREE.Mesh(new THREE.SphereGeometry(R.headR, 48, 32), headMat);
    body.add(head);
    const hands = [0, 1].map(() => { const m = new THREE.Mesh(new THREE.SphereGeometry(R.handR, 24, 16), bodyMat); m.scale.set(1, 1.08, 0.92); body.add(m); return m; });
    const feet = [0, 1].map(() => { const m = new THREE.Mesh(new THREE.SphereGeometry(R.footR, 24, 16), bodyMat); m.scale.set(1, 0.72, 1.45); body.add(m); return m; });
    const limbs = [0, 1, 2, 3].map(() => { const m = new THREE.Mesh(new THREE.BufferGeometry(), bodyMat); body.add(m); return m; });
    // visière : bandeau sombre sur l'avant de la tête, yeux en pixels orange (canvas, voir FACES)
    const fc = document.createElement('canvas'); fc.width = 256; fc.height = 72;
    const fg = fc.getContext('2d');
    const faceTex = new THREE.CanvasTexture(fc); faceTex.colorSpace = THREE.SRGBColorSpace;
    faceTex.magFilter = THREE.NearestFilter; faceTex.minFilter = THREE.LinearFilter;
    const VL = 1.95, VT = 1.4, VH = 0.5;   // largeur (rad), centre et hauteur (rad) du bandeau
    const visor = new THREE.Mesh(new THREE.SphereGeometry(R.headR * 1.012, 48, 12, Math.PI / 2 - VL / 2, VL, VT - VH / 2, VH),
        new THREE.MeshBasicMaterial({ map: faceTex, color: new THREE.Color(1.7, 1.7, 1.7), toneMapped: false }));
    const glass = new THREE.Mesh(new THREE.SphereGeometry(R.headR * 1.022, 48, 12, Math.PI / 2 - VL / 2 - 0.03, VL + 0.06, VT - VH / 2 - 0.03, VH + 0.06),
        new THREE.MeshMatcapMaterial({ matcap: mc, color: 0x9aa0aa, transparent: true, opacity: 0.28, depthWrite: false }));
    head.add(visor, glass);
    // logo ABRASION sur la poitrine
    const plate = new THREE.Group();
    const pl = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 0.05), new THREE.MeshMatcapMaterial({ matcap: mc, color: 0x1a1a1c }));
    const dot = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.11, 0.02), new THREE.MeshBasicMaterial({ color: new THREE.Color(3.2, 0.95, 0.06), toneMapped: false }));
    dot.position.z = 0.03;
    plate.add(pl, dot);
    plate.position.set(0, R.torsoY + 0.12, R.torsoR * R.torsoSZ + 0.005);
    body.add(plate);
    // anneau orange entre la tête et le corps
    const ring = new THREE.Mesh(new THREE.TorusGeometry(0.2, 0.026, 12, 48), new THREE.MeshBasicMaterial({ color: new THREE.Color(3.2, 0.95, 0.06), toneMapped: false }));
    ring.rotation.x = Math.PI / 2;
    ring.position.y = R.torsoY + R.torsoLen / 2 + R.torsoR + 0.05;
    body.add(ring);
    // queue en câble jack (dans le repère du sol, pour que la fiche reste par terre pendant les sauts)
    const cableMat = new THREE.MeshMatcapMaterial({ matcap: mc, color: 0x2a2b30 });
    const cable = new THREE.Mesh(new THREE.BufferGeometry(), cableMat);
    const plug = new THREE.Group();
    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.075, 0.24, 20), bodyMat);
    const tip = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.045, 0.2, 16), new THREE.MeshBasicMaterial({ color: new THREE.Color(2.6, 0.78, 0.05), toneMapped: false }));
    tip.position.y = 0.22;
    plug.add(barrel, tip);
    root.add(cable, plug);
    // ombre douce au sol
    const sc = document.createElement('canvas'); sc.width = sc.height = 64;
    const sg = sc.getContext('2d'); const gr = sg.createRadialGradient(32, 32, 0, 32, 32, 32);
    gr.addColorStop(0, 'rgba(0,0,0,0.65)'); gr.addColorStop(1, 'rgba(0,0,0,0)'); sg.fillStyle = gr; sg.fillRect(0, 0, 64, 64);
    const shadow = new THREE.Mesh(new THREE.PlaneGeometry(1.6, 1.6), new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(sc), transparent: true, depthWrite: false }));
    shadow.rotation.x = -Math.PI / 2;
    shadow.position.y = 0.01;
    root.add(shadow);

    let lastFace = '';
    function drawFace(name, talk = 0) {
        const key = name + Math.round(talk * 4);
        if (key === lastFace) return;
        lastFace = key;
        fg.fillStyle = '#060606'; fg.fillRect(0, 0, 256, 72);
        FACES[name in FACES ? name : 'neutre'](fg, 1 - 0.35 * talk);
        faceTex.needsUpdate = true;
    }
    function tube(mesh, a, ctrl, b, r) {
        const curve = new THREE.QuadraticBezierCurve3(a, ctrl, b);
        mesh.geometry.dispose();
        mesh.geometry = new THREE.TubeGeometry(curve, 18, r, 12, false);
    }
    const tmp = V();
    // P : { pos, rotY, lean, squash, air (hauteur au-dessus du sol, pour l'ombre), head: Vector3 (décalage),
    //       hands: [Vector3, Vector3] (droite, gauche ; repère du corps), elbows: [Vector3, Vector3] (courbure),
    //       feet: [Vector3, Vector3], knees: [Vector3, Vector3], show: { head, body, limbs } }
    function pose(P) {
        root.position.copy(P.pos);
        root.rotation.y = P.rotY || 0;
        const sq = P.squash || 1;
        body.scale.set(1 / Math.sqrt(sq), sq, 1 / Math.sqrt(sq));
        body.rotation.set(P.lean || 0, 0, P.tilt || 0);
        body.position.y = P.air || 0;
        head.position.set(0, R.headY, 0).add(P.head || V());
        const show = P.show || {};
        head.visible = show.head !== false;
        const bodyOn = show.body !== false, limbsOn = show.limbs !== false;
        torso.visible = bodyOn;
        for (let s = 0; s < 2; ++s) {
            const side = s === 0 ? 1 : -1;
            const sh = V(R.shoulder.x * side, R.shoulder.y, 0);
            const hand = P.hands[s];
            const mid = sh.clone().add(hand).multiplyScalar(0.5).add(P.elbows ? P.elbows[s] : V());
            tube(limbs[s], sh, mid, hand.clone().add(tmp.copy(hand).sub(mid).setLength(-0.02)), R.armR);
            hands[s].position.copy(hand);
            const hp = V(R.hip.x * side, R.hip.y, 0);
            const foot = P.feet[s];
            const kn = hp.clone().add(foot).multiplyScalar(0.5).add(P.knees ? P.knees[s] : V(0, 0, 0.06));
            tube(limbs[2 + s], hp, kn, foot.clone().add(V(0, 0.05, 0)), R.legR);
            feet[s].position.copy(foot).add(V(0, 0.02, 0.06));
            limbs[s].visible = limbs[2 + s].visible = hands[s].visible = feet[s].visible = limbsOn;
        }
        drawFace(P.face || 'neutre', P.talk || 0);
        dot.material.color.setRGB(3.2, 0.95, 0.06).multiplyScalar(0.35 + 0.9 * (P.pulse ?? 0.6));
        ring.material.color.setRGB(3.2, 0.95, 0.06).multiplyScalar(0.3 + 0.6 * (P.pulse ?? 0.6));
        plate.visible = ring.visible = bodyOn;
        // câble : part du bas du dos, ondule et finit posé au sol derrière lui
        const airB = P.air || 0, sw = Math.sin((P.time || 0) * 2.3) * 0.18, sw2 = Math.sin((P.time || 0) * 1.7 + 1) * 0.3;
        const lean = P.lean || 0;
        const c0 = V(0, 0.88 + airB, -R.torsoR * R.torsoSZ * 0.85 - lean * 0.3);
        const c1 = V(sw * 0.4, 0.45 + airB * 0.6, -0.7);
        const c2 = V(sw, 0.07, -1.3);
        const c3 = V(sw2, 0.07, -1.95 - (P.cableLen || 0));
        cable.geometry.dispose();
        cable.geometry = new THREE.TubeGeometry(new THREE.CatmullRomCurve3([c0, c1, c2, c3]), 32, 0.045, 10, false);
        plug.position.copy(c3);
        const dir = c3.clone().sub(c2).normalize();
        plug.quaternion.setFromUnitVectors(V(0, 1, 0), dir);
        cable.visible = plug.visible = show.cable !== false && bodyOn;
        const air = P.air || 0;
        shadow.scale.setScalar(Math.max(0.3, 1 - air * 0.18));
        shadow.material.opacity = Math.max(0.15, 0.9 - air * 0.2) * (P.shadow ?? 1);
    }
    return { root, pose, R };
}

// ---------------------------------------------------------------- poses de base (repère du corps, unités)
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
// expressions sur la visière : yeux en blocs de pixels (8 px), k = hauteur relative (parole)
const PX = 8;
function blk(g, x, y, w, h) { g.fillStyle = '#FF5A10'; g.fillRect(x * PX, y * PX, w * PX, h * PX); }
export const FACES = {
    neutre(g, k) { const h = Math.max(1, Math.round(4 * k)); blk(g, 10, 5 - h / 2, 2, h); blk(g, 20, 5 - h / 2, 2, h); },
    cligne(g) { blk(g, 9, 5, 4, 1); blk(g, 19, 5, 4, 1); },
    clin(g, k) { const h = Math.max(1, Math.round(4 * k)); blk(g, 10, 5 - h / 2, 2, h); blk(g, 19, 5, 4, 1); },
    content(g) { for (const x of [9, 19]) { blk(g, x, 5, 1, 1); blk(g, x + 1, 4, 2, 1); blk(g, x + 3, 5, 1, 1); } },
    surpris(g) { for (const x of [9, 19]) { blk(g, x, 3, 4, 1); blk(g, x, 6, 4, 1); blk(g, x, 4, 1, 2); blk(g, x + 3, 4, 1, 2); } },
    concentre(g) { blk(g, 9, 4, 1, 1); blk(g, 10, 5, 3, 1); blk(g, 19, 5, 3, 1); blk(g, 22, 4, 1, 1); },
    aie(g) { for (const [x, s] of [[10, 1], [21, -1]]) { blk(g, x - s, 3, 1, 1); blk(g, x, 4, 1, 1); blk(g, x + s, 5, 1, 1); blk(g, x, 6, 1, 1); blk(g, x - s, 7, 1, 1); } },
    malin(g) { blk(g, 9, 5, 4, 1); blk(g, 10, 4, 2, 1); blk(g, 20, 3, 2, 4); },
    etoiles(g) { for (const x of [10, 21]) { blk(g, x, 3, 1, 5); blk(g, x - 2, 5, 5, 1); blk(g, x - 1, 4, 3, 3); } },
};

export const POSE = {
    // debout, bras le long du corps, respiration
    idle(t = 0) {
        const br = Math.sin(t * 2.4) * 0.02;
        return {
            pos: V(), squash: 1 + br, head: V(0, br * 2, 0),
            hands: [V(0.6, 0.86, 0.05), V(-0.6, 0.86, 0.05)], elbows: [V(0.1, 0, 0), V(-0.1, 0, 0)],
            feet: [V(0.22, 0.1, 0), V(-0.22, 0.1, 0)],
        };
    },
    // coucou (comme sur l'image) : main gauche levée qui se balance ; s = côté (1 droite, -1 gauche)
    wave(t = 0, amount = 1) {
        const P = POSE.idle(t);
        const w = Math.sin(t * 9) * 0.22 * amount;
        // comme sur l'image : le bras part à l'horizontale puis remonte, la main tout en haut
        P.hands[1] = V(-0.95 + w * 0.5, 1.46 + 0.95 * amount, 0.1);
        P.elbows[1] = V(-0.42 * amount, -0.42 * amount, 0.05);
        P.head = V(0.05 * amount, 0, 0);
        P.tilt = -0.05 * amount;
        return P;
    },
    // marche : cycle de pas (phase en tours)
    walk(phase, speed = 1) {
        const a = phase * Math.PI * 2;
        const P = POSE.idle(0);
        const step = 0.32 * speed;
        P.feet = [V(0.2, 0.08 + Math.max(0, Math.sin(a)) * 0.22, Math.cos(a) * step), V(-0.2, 0.08 + Math.max(0, -Math.sin(a)) * 0.22, -Math.cos(a) * step)];
        P.knees = [V(0, 0.06, 0.16), V(0, 0.06, 0.16)];
        P.hands = [V(0.5, 0.95, -Math.cos(a) * 0.3 * speed), V(-0.5, 0.95, Math.cos(a) * 0.3 * speed)];
        P.air = Math.abs(Math.sin(a)) * 0.07;
        P.squash = 1 - Math.abs(Math.cos(a)) * 0.04;
        P.head = V(0, -Math.abs(Math.cos(a)) * 0.05, 0.02);
        P.lean = 0.08 * speed;
        return P;
    },
    // saut : u de 0 (accroupi) à 1 (réception) ; h hauteur
    jump(u, h = 2.5) {
        const P = POSE.idle(0);
        const crouch = u < 0.15 ? Math.sin((u / 0.15) * Math.PI) : 0;
        const land = u > 0.85 ? Math.sin(((u - 0.85) / 0.15) * Math.PI) : 0;
        const fly = clamp((u - 0.12) / 0.76);
        P.air = Math.sin(fly * Math.PI) * h;
        P.squash = 1 - 0.22 * (crouch + land) + (fly > 0 && fly < 1 ? 0.1 * Math.sin(fly * Math.PI * 2) : 0);
        const up = fly > 0 && fly < 1 ? 1 : 0;
        P.hands = [V(0.6, 0.95 + 0.9 * up, 0.05), V(-0.6, 0.95 + 0.9 * up, 0.05)];
        P.feet = [V(0.22, 0.08 + 0.18 * up, -0.1 * up), V(-0.22, 0.08 + 0.18 * up, 0.1 * up)];
        P.head = V(0, -0.12 * (crouch + land) + 0.1 * (fly > 0 && fly < 0.5 ? 1 : -0.4) * up, 0);
        return P;
    },
    // frappe vers le bas avec le poing droit (u 0 → 1, impact à 0,7)
    stomp(u) {
        const P = POSE.idle(0);
        const raise = clamp(u / 0.55), hit = clamp((u - 0.55) / 0.15);
        P.hands[0] = V(0.55, 1.4 + 1.0 * raise - 1.6 * hit, 0.2 + 0.3 * hit);
        P.elbows[0] = V(0.2, 0.1, -0.1);
        P.squash = 1 + 0.08 * raise - 0.2 * hit * (1 - clamp((u - 0.75) / 0.25));
        P.lean = 0.25 * hit;
        P.head = V(0, 0.06 * raise - 0.1 * hit, 0.04 * hit);
        return P;
    },
    // surf : jambes fléchies, bras écartés
    surf(t) {
        const P = POSE.idle(t);
        const b = Math.sin(t * 3.1) * 0.1;
        P.feet = [V(0.42, 0.08, 0.05), V(-0.42, 0.08, -0.05)];
        P.knees = [V(0.12, 0, 0.12), V(-0.12, 0, 0.12)];
        P.hands = [V(1.0, 1.35 + b, 0.05), V(-1.0, 1.25 - b, 0.05)];
        P.elbows = [V(0, 0.15, 0), V(0, 0.15, 0)];
        P.squash = 0.9;
        P.tilt = b * 0.6;
        return P;
    },
    // pousse un objet devant lui (deux mains en avant)
    push(t) {
        const P = POSE.walk(t * 1.2, 0.7);
        P.hands = [V(0.32, 1.35, 0.6), V(-0.32, 1.35, 0.6)];
        P.elbows = [V(0.1, -0.1, 0), V(-0.1, -0.1, 0)];
        P.lean = 0.32;
        return P;
    },
    // assis (sur le bord d'une surface), jambes qui pendent et se balancent
    sit(t) {
        const P = POSE.idle(t);
        const sw = Math.sin(t * 3) * 0.15;
        P.feet = [V(0.2, -0.05, 0.55 + sw), V(-0.2, -0.05, 0.55 - sw)];
        P.knees = [V(0, 0.25, 0.1), V(0, 0.25, 0.1)];
        P.air = -0.55;
        P.hands = [V(0.55, 0.78, -0.1), V(-0.55, 0.78, -0.1)];
        return P;
    },
};
