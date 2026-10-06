"use strict";
// ============================================================================
// Démarrage : tous les fichiers sont chargés, on construit le monde puis on lance la boucle.
// Les enseignes en japonais attendent la police (2,5 s maximum).
// ============================================================================
async function start() {
  try { await Promise.race([document.fonts.load(`900 64px "Zen Kaku Gothic New"`, "ラーメン寿司居酒屋カラオケ百貨店"), new Promise((r) => setTimeout(r, 2500))]); } catch (e) { /* police de secours */ }
  buildTextures(); buildCity(); buildBackdrop(); setupNight(); setupClouds(); buildDragon(); setupNpcs(); setupAltars();
  setRigWeapon(model, P.weapon); buildSpellBar();
  resize();
  playBtn.disabled = false; playBtn.textContent = "Jouer";
  requestAnimationFrame((t) => { last = t; frame_(t); });
}
start();
