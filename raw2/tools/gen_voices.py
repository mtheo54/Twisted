#!/usr/bin/env python3
# Génère src/core/FactoryVoice.inc : enveloppes A/B, LFO 1/2, filtre, modulations et rack modulaire des 72 presets d'usine.
# Une ligne v(...) par preset (indice 0..71) ; relancer après modification : python tools/gen_voices.py
# Recettes de voix des 72 presets : enveloppes A/B (a d s r courbe), modes, LFO 1/2, filtre, matrice.
# LFO : (forme, vitesse, phase, fade, lissage, sync) ; formes 0 SINE 1 TRI 2 SAW 3 SQR 4 S&H ; sync 0 OFF 1 1/1 .. 5 1/16
SIN,TRI,SAW,SQR,SH=0,1,2,3,4
V = {}
def v(i, A, B, l1, l2, cut, reso, mx, modes=(0,0), link=0): V[i]=dict(A=A,B=B,l1=l1,l2=l2,cut=cut,reso=reso,mx=mx,modes=modes,link=link)
# ---- basses
v(2,  (.05,.60,.90,.35,.8), (.02,.40,.20,.30,1.), (SIN,.30,0,0,.2,0), (TRI,.20,0,0,0,0), .62,.08, "env>cutoff:25 lfo1>posB:15 vie>grain:8")
v(4,  (.08,.70,.95,.30,.8), (.10,.50,.60,.30,.8), (SIN,.20,0,0,0,0), (SIN,.30,.5,0,0,0), .58,.05, "env>cutoff:15 lfo1>posA:8")
v(5,  (.02,.78,.60,.50,.8), (.02,.35,0,.30,1.), (SIN,.50,0,0,0,0), (TRI,.20,0,0,0,0), .70,.10, "env>pitchA:12 env>cutoff:30")
v(6,  (.06,.62,.70,.40,.8), (.04,.50,.45,.35,.8), (SIN,.55,0,0,.2,0), (TRI,.30,0,0,0,0), .66,.18, "env>cutoff:45 lfo1>posA:10 lfo2>posB:15")
v(7,  (.03,.50,.85,.35,.8), (.03,.45,.70,.35,.8), (SAW,.50,0,0,.1,4), (SIN,.60,0,0,0,0), .70,.25, "lfo1>cutoff:25 env>cutoff:35 lfo2>pitchB:2")
v(8,  (.04,.66,.75,.40,.8), (.05,.58,.50,.38,.2), (TRI,.45,0,0,0,0), (SH,.50,0,0,.3,0), .64,.20, "lfo1>posA:22 env>cutoff:35 lfo2>reso:10")
v(9,  (.02,.55,.80,.30,.8), (.02,.50,.80,.30,.8), (SIN,.50,0,0,.15,4), (TRI,.35,0,0,0,0), .55,.30, "lfo1>cutoff:45 env>cutoff:20 lfo2>posB:20")
v(10, (.05,.65,.90,.40,.8), (.08,.50,.50,.40,.8), (SIN,.35,0,0,0,0), (SIN,.20,.25,0,0,0), .60,.05, "env>cutoff:20 lfo2>posA:10")
v(11, (.02,.45,.70,.25,1.), (.02,.30,.30,.20,1.), (SH,.50,0,0,0,5), (SQR,.70,0,0,0,0), .66,.30, "lfo1>posA:35 lfo1>cutoff:25 lfo2>pitchB:4 env>cutoff:30")
v(12, (.03,.70,.60,.45,.8), (.03,.40,.10,.35,1.), (TRI,.50,0,0,0,0), (SIN,.25,0,0,0,0), .62,.22, "env>cutoff:50 lfo1>posB:18 vie>grain:12")
v(13, (.10,.75,.80,.55,.8), (.20,.80,.60,.60,.5), (SIN,.15,0,0,.3,0), (TRI,.30,.5,0,0,0), .58,.20, "lfo1>cutoff:20 lfo2>posB:30 vie>grain:15")
v(14, (.02,.50,.90,.30,.8), (.02,.50,.90,.30,.8), (SIN,.62,0,0,.1,0), (SH,.55,0,0,0,0), .72,.30, "lfo1>pitchB:3 env>cutoff:30 lfo2>posA:15", link=1)
v(15, (.08,.60,.80,.40,.5), (.10,.45,.25,.35,.2), (TRI,.35,0,0,.5,0), (SH,.30,0,0,.5,0), .52,.12, "lfo1>pitchA:2 lfo2>posB:20 vie>grain:25")
v(16, (.02,.50,.55,.30,.8), (.02,.45,.40,.30,.8), (SAW,.50,0,0,0,5), (SIN,.40,0,0,0,0), .50,.60, "env>cutoff:60 lfo1>cutoff:15 lfo2>reso:15")
v(17, (.10,.70,.70,.55,.8), (.25,.75,.60,.60,.5), (SIN,.30,.25,0,.3,0), (TRI,.22,0,0,0,0), .66,.12, "lfo1>posA:20 lfo2>posB:25 env>cutoff:25 vie>grain:15")
v(18, (.04,.62,.85,.38,.8), (.06,.55,.70,.38,.5), (SIN,.40,0,0,0,0), (TRI,.25,0,0,0,0), .64,.10, "env>cutoff:30 lfo1>posA:10")
# ---- leads
v(0,  (.12,.55,.75,.50,.8), (.30,.60,.50,.60,.8), (SIN,.74,0,.12,.1,0), (TRI,.30,.25,0,.2,0), .85,.15, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posB:20 env>cutoff:30 vie>grain:15")
v(19, (.08,.50,.80,.45,.8), (.08,.50,.80,.45,.8), (SIN,.76,0,.10,0,0), (TRI,.60,0,0,0,0), .86,.20, "lfo1>pitchA:4 lfo1>pitchB:4 lfo2>posB:15 env>cutoff:25")
v(20, (.06,.48,.70,.40,.8), (.05,.40,.50,.40,.8), (SIN,.78,0,.15,0,0), (SQR,.50,0,0,.2,4), .84,.15, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posA:12 env>cutoff:20")
v(21, (.10,.55,.85,.50,.5), (.15,.60,.80,.50,.5), (TRI,.45,0,0,.2,0), (SIN,.74,0,.20,0,0), .82,.25, "lfo1>pitchB:8 lfo2>pitchA:3 env>cutoff:20", modes=(1,0))
v(22, (.04,.60,.60,.55,.8), (.08,.70,.40,.60,.8), (SIN,.72,0,.25,0,0), (TRI,.35,0,0,0,0), .88,.12, "lfo1>pitchA:2 lfo1>pitchB:2 lfo2>posA:20 env>cutoff:20")
v(23, (.10,.60,.75,.55,.8), (.02,.40,.30,.40,1.), (SIN,.75,0,.30,0,0), (SAW,.40,0,0,.3,0), .80,.30, "lfo1>pitchA:3 lfo2>posA:15 env>pitchB:8")
v(24, (.08,.55,.80,.50,.8), (.12,.60,.70,.50,.5), (SIN,.74,0,.20,0,0), (TRI,.30,0,0,0,0), .84,.18, "lfo1>pitchA:4 lfo1>pitchB:4 lfo2>posA:25 vie>grain:10")
v(25, (.10,.60,.90,.55,.8), (.10,.60,.90,.55,.8), (SIN,.78,0,.15,0,0), (TRI,.45,0,0,0,0), .86,.15, "lfo1>pitchA:2 lfo1>pitchB:2 lfo2>posB:10 env>cutoff:20", link=1)
v(26, (.14,.60,.80,.55,.5), (.20,.65,.70,.55,.5), (SIN,.73,0,.30,0,0), (SIN,.28,0,0,.2,0), .82,.20, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posA:30")
v(27, (.25,.60,.85,.60,.5), (.30,.55,.60,.60,.5), (SIN,.72,0,.35,0,0), (SH,.60,0,0,.7,0), .76,.10, "lfo1>pitchA:4 lfo1>master:4 lfo2>posB:10", modes=(1,1))
v(28, (.02,.45,.60,.40,.8), (.02,.30,.20,.35,1.), (SQR,.50,0,0,0,5), (SIN,.76,0,.10,0,0), .90,.20, "lfo1>posA:25 lfo2>pitchA:3 lfo2>pitchB:3 env>cutoff:35")
v(29, (.12,.55,.80,.50,.8), (.18,.60,.60,.50,.8), (SIN,.73,0,.25,0,0), (TRI,.32,0,0,0,0), .83,.15, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posB:20")
v(30, (0,.40,.80,.30,.5), (0,.35,.60,.30,.5), (SIN,.80,0,.20,0,0), (SQR,.50,0,0,0,4), 1.0,0, "lfo1>pitchA:4 lfo1>pitchB:4 lfo2>posA:30")
v(31, (.04,.50,.75,.45,.8), (.06,.45,.55,.45,.8), (SIN,.77,0,.15,0,0), (TRI,.40,0,0,0,3), .85,.20, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posB:25 env>cutoff:20")
v(32, (.20,.65,.70,.65,.5), (.30,.70,.60,.70,.5), (SIN,.71,0,.40,0,0), (SIN,.25,0,0,.2,0), .80,.12, "lfo1>pitchA:2 lfo1>pitchB:2 lfo2>posA:25 vie>grain:20", modes=(1,0))
v(33, (0,.62,.35,.55,1.), (0,.50,.15,.50,1.), (SIN,.72,0,.50,0,0), (TRI,.30,0,0,0,0), .90,.10, "lfo1>pitchA:2 lfo1>pitchB:2 lfo2>posA:15 env>cutoff:30")
v(34, (.06,.55,.75,.50,.8), (.08,.60,.70,.50,.2), (SAW,.55,0,0,.2,0), (SH,.50,0,0,0,5), .82,.30, "lfo1>posA:40 lfo2>pitchB:3 env>cutoff:20")
v(35, (.05,.55,.75,.50,.8), (.10,.60,.55,.55,.5), (SIN,.74,0,.20,0,0), (TRI,.40,0,0,0,0), .86,.18, "lfo1>pitchA:3 lfo1>pitchB:3 lfo2>posB:20 env>cutoff:25")
# ---- pads
v(1,  (.72,.70,.80,.78,.8), (.80,.75,.70,.82,.5), (SIN,.25,0,0,.3,0), (TRI,.18,.5,.40,.2,0), .72,.10, "lfo1>posA:25 lfo2>posB:30 vie>grain:30 lfo2>cutoff:12")
v(3,  (.78,.80,.70,.85,.5), (.85,.70,.60,.88,.2), (TRI,.20,.25,0,.4,0), (SH,.40,0,0,.6,0), .70,.20, "lfo1>posA:30 lfo2>posB:35 vie>grain:40 lfo2>reso:15")
v(36, (.70,.75,.80,.80,.5), (.80,.80,.70,.85,.5), (SIN,.22,0,0,.2,0), (TRI,.16,.5,.50,0,0), .74,.12, "lfo1>posA:25 lfo2>posB:30 vie>grain:30")
v(37, (.68,.70,.80,.78,.2), (.75,.60,.50,.80,.2), (TRI,.30,0,0,.5,0), (SH,.35,0,0,.7,0), .62,.10, "lfo1>pitchA:2 lfo2>cutoff:12 vie>grain:20")
v(38, (.74,.72,.85,.80,.5), (.70,.75,.70,.78,.8), (SIN,.28,0,0,0,0), (SIN,.20,.25,0,0,0), .70,.14, "lfo1>posA:20 lfo2>master:8 vie>grain:15")
v(39, (.76,.78,.80,.84,.5), (.82,.80,.70,.86,.5), (TRI,.24,0,0,.2,0), (SIN,.18,0,0,0,0), .66,.20, "lfo1>cutoff:18 lfo2>posA:20 vie>grain:35")
v(40, (.66,.70,.75,.80,.8), (.72,.75,.60,.82,.5), (SH,.40,0,0,.5,2), (SIN,.20,0,0,0,0), .64,.30, "lfo1>posB:30 lfo2>cutoff:20 vie>grain:20")
v(41, (.78,.76,.80,.84,.2), (.84,.80,.65,.86,.2), (SIN,.20,0,0,0,0), (TRI,.22,0,0,.2,0), .72,.10, "lfo1>pitchB:2 lfo2>posA:25 vie>grain:25")
v(42, (.65,.70,.80,.75,.5), (.70,.72,.75,.76,.5), (SIN,.30,0,0,0,0), (SIN,.26,.5,0,0,0), .76,.10, "lfo1>posA:15 lfo2>posB:15 env>cutoff:15")
v(43, (.82,.80,.85,.88,.5), (.88,.82,.70,.90,.5), (SIN,.12,0,0,0,0), (TRI,.15,0,0,.3,0), .70,.08, "lfo1>master:10 lfo2>posB:30 vie>grain:25")
v(44, (.62,.70,.80,.75,.8), (.70,.70,.65,.78,.8), (SIN,.40,0,0,.2,1), (TRI,.30,0,0,0,0), .72,.25, "lfo1>cutoff:25 lfo2>posB:20 vie>grain:15")
v(45, (.66,.70,.80,.76,.5), (.72,.70,.70,.78,.5), (TRI,.30,0,0,.2,0), (SIN,.20,0,0,0,0), .74,.12, "lfo1>posA:25 lfo2>pitchB:1 vie>grain:15")
v(46, (.80,.80,.80,.86,.2), (.86,.82,.75,.88,.2), (SIN,.18,0,0,0,0), (SIN,.14,.75,0,0,0), .76,.10, "lfo1>posA:20 lfo2>posB:25 vie>grain:30")
v(47, (.74,.70,.70,.80,.2), (.80,.74,.60,.82,.5), (SH,.30,0,0,.8,0), (SIN,.20,0,0,0,0), .58,.14, "lfo1>cutoff:15 lfo2>master:10 vie>grain:30")
# ---- plucks
v(48, (0,.55,0,.48,1.), (0,.42,0,.40,1.), (SIN,.40,0,0,0,0), (TRI,.30,0,0,0,0), .78,.20, "lfo1>posA:10 env>cutoff:45")
v(49, (0,.58,0,.50,.8), (0,.45,0,.45,1.), (TRI,.35,0,0,0,0), (SIN,.60,0,0,0,0), .80,.15, "lfo1>posB:15 env>cutoff:35")
v(50, (0,.60,0,.52,.8), (.02,.50,.10,.50,.8), (SIN,.70,0,.20,0,0), (TRI,.30,0,0,0,0), .76,.18, "lfo1>pitchA:2 lfo2>posA:10 env>cutoff:40")
v(51, (0,.66,0,.58,1.), (0,.55,0,.55,1.), (SIN,.80,0,0,0,0), (TRI,.25,0,0,0,0), .90,.10, "lfo1>pitchB:1 lfo2>posA:15 env>cutoff:20")
v(52, (0,.50,0,.45,1.), (0,.35,0,.35,1.), (SH,.50,0,0,0,5), (SH,.60,0,0,.3,0), .82,.30, "lfo1>posA:40 lfo2>pitchB:3 env>cutoff:30")
v(53, (0,.56,.10,.50,.8), (0,.48,0,.45,.8), (SIN,.35,0,0,0,0), (TRI,.25,0,0,0,0), .70,.25, "lfo1>posB:15 env>cutoff:55")
v(54, (0,.62,0,.55,.8), (0,.58,0,.52,1.), (SIN,.75,0,.30,0,0), (SIN,.15,0,0,0,0), .84,.10, "lfo1>pitchA:2 lfo2>master:6 env>cutoff:25")
v(55, (0,.42,0,.35,.5), (0,.30,0,.30,.5), (SQR,.50,0,0,0,3), (TRI,.30,0,0,0,0), .90,.10, "lfo1>posA:30 env>cutoff:25")
# ---- arps
v(56, (0,.50,.35,.40,.8), (.02,.45,.25,.40,.8), (TRI,.50,0,0,0,2), (SIN,.30,0,0,0,0), .74,.25, "lfo1>cutoff:20 lfo2>posB:15 env>cutoff:35")
v(57, (0,.48,.40,.38,.8), (0,.45,.30,.38,.8), (SIN,.50,0,0,0,3), (SH,.50,0,0,0,5), .78,.22, "lfo1>posA:25 lfo2>cutoff:15 env>cutoff:25")
v(58, (.02,.55,.30,.50,.5), (.05,.60,.25,.55,.5), (SIN,.28,0,0,0,0), (TRI,.40,0,0,0,2), .80,.15, "lfo1>posA:25 lfo2>posB:20 env>cutoff:25")
v(59, (0,.45,.45,.38,.8), (0,.40,.30,.38,.8), (SQR,.50,0,0,.1,4), (SIN,.30,0,0,0,0), .76,.20, "lfo1>master:15 lfo2>posA:15 env>cutoff:30")
v(60, (0,.42,.30,.35,1.), (0,.35,.20,.32,1.), (SH,.50,0,0,0,5), (SH,.50,0,0,0,4), .74,.35, "lfo1>cutoff:30 lfo2>posB:35 env>cutoff:20")
v(61, (.04,.60,.30,.60,.5), (.08,.65,.25,.65,.5), (SIN,.55,0,0,0,0), (SH,.45,0,0,.5,0), .80,.12, "lfo1>posB:20 lfo2>pitchB:1 vie>grain:25")
# ---- fx
v(62, (.80,.80,.70,.85,.2), (.85,.80,.60,.88,.2), (SH,.60,0,0,.5,0), (TRI,.20,0,0,0,0), .70,.30, "lfo1>cutoff:25 lfo2>posB:35 vie>grain:50")
v(63, (.02,.50,.60,.50,1.), (0,.35,.30,.40,1.), (SH,.50,0,0,0,5), (SQR,.65,0,0,0,0), .80,.35, "lfo1>posA:40 lfo2>master:20", modes=(1,1))
v(64, (.40,.70,.70,.70,.2), (.50,.70,.60,.70,.5), (SH,.50,0,0,.3,0), (SIN,.35,0,0,0,0), .62,.40, "lfo1>cutoff:30 lfo2>pitchB:3 vie>grain:30")
v(65, (.85,.85,.75,.90,.5), (.88,.85,.70,.90,.5), (SIN,.15,0,0,0,0), (TRI,.12,0,0,0,0), .60,.25, "lfo1>cutoff:30 lfo2>master:15 vie>grain:40")
v(66, (.30,.70,.80,.70,.8), (.40,.70,.70,.72,.8), (SAW,.50,0,0,.1,0), (SH,.50,0,0,0,5), .76,.35, "lfo1>pitchA:10 lfo2>cutoff:25 vie>grain:20")
v(67, (.60,.75,.70,.80,.2), (.70,.75,.60,.82,.2), (SIN,.40,0,0,0,0), (SH,.55,0,0,.7,0), .78,.20, "lfo1>posA:30 lfo2>posB:25 vie>grain:35")
v(68, (0,.40,.50,.40,1.), (0,.30,.30,.35,1.), (SH,.50,0,0,0,5), (SQR,.50,0,0,0,4), .80,.30, "lfo1>pitchB:8 lfo2>master:25 env>cutoff:20")
v(69, (.75,.80,.80,.85,.5), (.80,.80,.70,.88,.5), (SAW,.30,0,0,.2,0), (SIN,.45,0,0,0,0), .70,.45, "lfo1>pitchB:15 lfo2>cutoff:30 vie>grain:25")
v(70, (.82,.50,.90,.25,.2), (.85,.50,.85,.25,.2), (SAW,.20,0,0,0,0), (TRI,.30,0,0,0,0), .78,.15, "lfo1>master:10 lfo2>posA:20")
v(71, (.70,.75,.70,.80,.2), (.78,.75,.60,.82,.2), (SH,.40,0,0,.6,0), (TRI,.25,0,0,0,0), .64,.20, "lfo1>posB:30 lfo2>pitchA:2 vie>grain:40")

assert sorted(V)==list(range(72)), [i for i in range(72) if i not in V]

# ---- rack modulaire (MODULES / SIDE B) : LFO R1..R6, formes dessinées, routes libres vers n'importe
# quel potard. Jetons : r<n>:<forme>:<vitesse>:<sync>[:<phase>]  draw:<lfo>:<motif>  <lfo>><clé>:<dosage>
# (lfo : l1, l2, r1..r6 ; motifs : pluck swell breath wobble stairs gate bounce)
RK = {
    # basses : grain qui respire, saturation qui pompe sur les basses agressives
    2:  "r1:0:.22:0 r1>aWarp:10",
    4:  "r1:0:.18:0 r1>bLvl:8",
    5:  "draw:l1:pluck l1>aWarp:25",
    6:  "r1:1:.30:0 r1>aWarp:14 r2:0:.20:0:.5 r2>fx_dist_p1:10",
    7:  "r1:2:.5:3 r1>fx_dist_p0:18 r2:0:.25:0 r2>aPan:8",
    8:  "draw:l2:wobble r1:0:.3:0 r1>fx_dist_p0:15",
    9:  "r1:3:.5:4 r1>fx_dist_p1:12 r2:0:.2:0 r2>bWarp:15",
    10: "r1:0:.16:0 r1>aWarp:8",
    11: "draw:l2:wobble r1:4:.5:4 r1>fx_dist_p0:20",
    12: "r1:1:.28:0 r1>bWarp:18",
    13: "r1:0:.15:0 r1>fx_rev_mix:12 r2:1:.22:0 r2>aWarp:15",
    14: "r1:2:.5:2 r1>fx_dist_p0:15 r2:0:.24:0:.25 r2>bPan:12",
    15: "r1:0:.12:0 r1>fx_chor_mix:20 r2:4:.3:0 r2>aWarp:12",
    16: "draw:r1:pluck r1:2:.5:4 r1>fx_filt_p0:20 r1>fx_dist_p0:12",
    17: "r1:0:.2:0 r1>bWarp:15 r2:1:.16:0:.5 r2>fx_rev_mix:12",
    18: "r1:0:.22:0 r1>aWarp:10 r1>bWarp:-10",
    # leads : panoramique qui flotte, délai qui respire, souffle dessiné
    0:  "draw:l2:breath r1:0:.32:0:.25 r1>aPan:12 r1>bPan:-12",
    19: "r1:0:.35:0 r1>aPan:10 r2:1:.2:0 r2>fx_dly_mix:15",
    20: "r1:3:.5:3 r1>fx_chor_mix:18 r2:0:.3:0 r2>bPan:10",
    21: "r1:0:.3:0 r1>aWarp:12 r2:1:.18:0 r2>fx_phase_mix:15",
    22: "draw:l2:breath r1:0:.34:0 r1>aPan:12",
    23: "r1:2:.45:0 r1>bWarp:20 r2:0:.25:0 r2>fx_dly_mix:15",
    24: "r1:0:.3:0:.5 r1>bPan:14 r2:0:.22:0 r2>fx_rev_mix:10",
    25: "r1:0:.36:0 r1>aPan:8 r1>bPan:-8",
    26: "draw:l2:breath r1:1:.25:0 r1>fx_phase_p0:15",
    27: "r1:4:.4:0 r1>fx_dly_p2:15 r2:0:.2:0 r2>aPan:10",
    28: "r1:3:.5:3 r1>fx_flang_mix:18 r2:0:.3:0 r2>aWarp:12",
    29: "draw:l2:breath r1:0:.3:0 r1>bPan:12",
    30: "r1:0:.4:0 r1>aPan:10 r2:3:.5:4 r2>fx_dly_mix:15",
    31: "r1:1:.3:0 r1>aWarp:14 r2:0:.25:0 r2>fx_dly_mix:12",
    32: "r1:0:.2:0 r1>fx_rev_mix:15 r2:0:.28:0:.5 r2>aPan:12",
    33: "draw:r1:pluck r1:0:.5:4 r1>fx_dly_mix:18",
    34: "r1:4:.5:4 r1>bWarp:18 r2:0:.3:0 r2>aPan:10",
    35: "r1:0:.33:0 r1>aPan:10 r2:1:.22:0 r2>fx_hyper_mix:15",
    # pads : réverbération et image stéréo qui vivent lentement, montée dessinée
    1:  "r1:0:.14:0 r1>fx_rev_mix:15 r2:1:.18:0:.5 r2>bPan:22",
    3:  "r1:0:.12:0 r1>fx_rev_p1:15 r2:1:.2:0 r2>aWarp:18 r3:0:.16:0:.5 r3>bPan:20",
    36: "draw:l1:swell r1:0:.15:0 r1>fx_rev_mix:15 r2:1:.2:0:.5 r2>bPan:25",
    37: "r1:0:.12:0 r1>aWarp:18 r2:0:.18:0:.25 r2>aPan:20",
    38: "draw:l2:breath r1:1:.16:0 r1>fx_rev_mix:12 r2:0:.2:0:.5 r2>bPan:22",
    39: "r1:0:.14:0 r1>fx_chor_mix:15 r2:1:.18:0 r2>bWarp:20",
    40: "r1:4:.35:0 r1>aWarp:15 r2:0:.16:0 r2>fx_rev_mix:12",
    41: "draw:l1:swell r1:0:.13:0 r1>aPan:20 r1>bPan:-20",
    42: "r1:0:.18:0 r1>fx_rev_mix:12 r2:1:.2:0:.5 r2>aWarp:15",
    43: "draw:l2:breath r1:0:.1:0 r1>fx_rev_p1:15 r2:1:.14:0 r2>bPan:25",
    44: "r1:3:.5:2 r1>fx_phase_mix:15 r2:0:.2:0 r2>aPan:15",
    45: "r1:0:.16:0 r1>bWarp:18 r2:0:.22:0:.5 r2>fx_rev_mix:12",
    46: "draw:l1:swell r1:0:.12:0 r1>fx_rev_mix:15 r2:1:.16:0 r2>aPan:20",
    47: "r1:4:.3:0 r1>aWarp:20 r2:0:.14:0 r2>bPan:20",
    # plucks : chute dessinée, chorus et délai qui bougent
    48: "draw:l1:pluck r1:1:.3:0 r1>fx_dly_mix:15",
    49: "r1:0:.3:0 r1>fx_chor_mix:15 r2:0:.25:0:.5 r2>aPan:10",
    50: "r1:1:.28:0 r1>aWarp:12 r2:0:.3:0 r2>fx_phase_p0:15",
    51: "draw:l1:pluck r1:0:.35:0 r1>bPan:12",
    52: "r1:4:.5:4 r1>aWarp:18 r2:0:.3:0 r2>fx_dly_mix:12",
    53: "draw:l1:pluck r1:0:.28:0 r1>fx_dly_p2:12",
    54: "r1:0:.25:0 r1>fx_rev_mix:12 r2:1:.3:0:.5 r2>aPan:10",
    55: "r1:3:.5:3 r1>fx_dly_mix:15 r2:0:.3:0 r2>bWarp:12",
    # arps : gate et marches rythmiques calées sur le tempo
    56: "draw:r2:stairs r2:0:.5:2 r2>aPos:18 r1:0:.25:0 r1>aPan:10",
    57: "draw:r1:gate r1:3:.5:3 r1>master:-25",
    58: "draw:r2:stairs r2:0:.5:2 r2>bPos:20 r1:0:.3:0 r1>fx_dly_mix:12",
    59: "draw:r1:gate r1:3:.5:4 r1>master:-20 r2:0:.25:0 r2>bPan:12",
    60: "draw:r2:stairs r2:0:.5:3 r2>aWarp:20",
    61: "r1:0:.3:0 r1>fx_rev_mix:12 r2:1:.25:0:.5 r2>aPan:12",
    # fx : hasard, rebonds et marches sur les effets
    62: "r1:4:.5:4 r1>fx_filt_p0:20 r2:0:.1:0 r2>fx_rev_p1:20",
    63: "draw:r2:bounce r2:0:.45:0 r2>fx_dly_p0:12 r1:4:.5:4 r1>aWarp:20",
    64: "r1:4:.45:0 r1>fx_filt_p0:22 r2:1:.2:0 r2>bPan:25",
    65: "r1:0:.08:0 r1>fx_rev_p1:20 r2:0:.12:0:.5 r2>aPan:25",
    66: "draw:r1:bounce r1:0:.4:0 r1>fx_dist_p0:20 r2:4:.5:4 r2>bWarp:20",
    67: "r1:0:.15:0 r1>fx_flang_mix:20 r2:1:.18:0 r2>aWarp:20",
    68: "draw:r1:stairs r1:0:.5:3 r1>aPos:25 r2:4:.5:5 r2>fx_phase_p0:20",
    69: "r1:2:.3:0 r1>fx_phase_p0:25 r2:0:.15:0 r2>fx_rev_mix:15",
    70: "draw:r1:swell r1:0:.2:0 r1>fx_rev_mix:20 r2:1:.2:0:.5 r2>bPan:25",
    71: "r1:4:.4:0 r1>aWarp:20 r2:0:.1:0 r2>fx_rev_p1:15",
}
assert sorted(RK)==list(range(72)), [i for i in range(72) if i not in RK]
import json, collections
names=[p['name'] for p in json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','resources','data','presets.json'),encoding='utf-8'))]
sig=collections.Counter()
for i,r in V.items():
    sig[(r['A'],r['B'],r['l1'][:2],r['l2'][:2],r['mx'])]+=1
assert max(sig.values())==1
SRC={'vie':0,'lfo1':1,'lfo2':2,'env':3,'kb':4,'rnd':5}
DST={'pitchA':0,'pitchB':1,'posA':2,'posB':3,'cutoff':4,'reso':5,'grain':6,'master':7}
for r in V.values():
    for t in r['mx'].split():
        s,rest=t.split('>'); d,a=rest.split(':'); assert s in SRC and d in DST, t
def f(x): 
    s=('%.3f'%x).rstrip('0')
    if s.endswith('.'): s+=''
    if '.' not in s: s+='.'
    return s+'f'
out=['// généré par tools/gen_voices.py (enveloppes, LFO, filtre et modulations de chaque preset d\'usine)','const FactoryVoice kVoices[kNumFactoryPresets] = {']
for i in range(72):
    r=V[i]
    A=', '.join(f(x) for x in r['A']); B=', '.join(f(x) for x in r['B'])
    l1=r['l1']; l2=r['l2']
    out.append('    /* %02d %-17s */ {{%s}, {%s}, %d, %d, %d, %d, {%s, %s, %s, %s}, %d, %d, {%s, %s, %s, %s}, %d, %s, %s, "%s",\n        "%s"},'%(
        i+1, names[i], A, B, r['modes'][0], r['modes'][1], r['link'], l1[0], f(l1[1]),f(l1[2]),f(l1[3]),f(l1[4]), l1[5], l2[0], f(l2[1]),f(l2[2]),f(l2[3]),f(l2[4]), l2[5], f(r['cut']), f(r['reso']), r['mx'], RK[i]))
out.append('};')
open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','src','core','FactoryVoice.inc'),'w').write('\n'.join(out)+'\n')
print('ok', len(V))
