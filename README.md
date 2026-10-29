# Fog Z — Studio Wiga

Plugin VST3 (Windows 64 bits + macOS universel Apple Silicon/Intel) qui copie la pédale **Electro-Harmonix POG2** (Polyphonic Octave Generator), sur le visuel « FOGZ ».

v1.4.0 : MIDI Learn au clic droit sur chaque commande (Clear MIDI, assignations sauvegardées avec le projet)
v1.3.0 : anti-gel v2 (JUCE 8.0.15, caches refaits par le timer, chien de garde, aucun appel à l'hôte depuis le thread audio)

## Compiler avec GitHub
1. Mets **tous les fichiers à la racine du dépôt** (avec le dossier `.github/workflows`).
2. Un push lance l'onglet **Actions** → *Build Fog Z VST3 (Windows + macOS)*.
3. Télécharge les artefacts :
   - `FogZ-VST3-Windows` → copie `Fog Z.vst3` dans `C:\Program Files\Common Files\VST3\`
   - `FogZ-VST3-macOS` → copie `Fog Z.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` puis une fois : `xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Fog Z.vst3"`
4. Reaper : re-scanne. Le plugin s'appelle **Fog Z (Studio Wiga)**.

## Fenêtre
Même taille que Ring Think : 665 × 498 par défaut (67 %), jamais plus petite. Clic droit → Plugin size : 67 / 75 / 100 / 125 %, ou tirer le coin.

## Presets
- Les **12 sons** apparaissent dans le menu déroulant des presets de la fenêtre du plugin (Reaper : liste en haut à gauche) et dans clic droit → Factory sounds.
- Les **8 premiers** sont rangés dans les mémoires 1-8 de la pédale (potard PRESET).

1. Organ Pad · 2. Baritone Growl · 3. Theremin-ish · 4. Cello · 5. Lower + Upper Clean · 6. Organ Drawbars (toutes les voix) · 7. Glass Choir · 8. Sub Synth Bass · 9. 12-String Shimmer · 10. Reverse Swell · 11. Leslie Rotary · 12. Fog Horn

## Commandes (comme sur la POG2)
| Commande | Rôle |
|---|---|
| DRY OUTPUT | son direct (100 % au repère 80 %) |
| -2 / -1 / +1 / +2 OCTAVES | niveau de chaque voix (polyphonique) |
| ATTACK | montée en volume note par note (en bas = coupé, jusqu'à 3 s), courbe lente au début puis forte ; la note est soutenue pendant la montée (jusqu'à +9,5 dB) pour finir fort |
| LP FILTER | filtre passe-bas 2 pôles 50 Hz → 18 kHz |
| DETUNE | voix +1 et +2 : chorus lent en bas, chorus rapide puis vibrato (rotatif) en haut — vitesse ET profondeur montent ensemble |
| DRY FX | LED éteinte : direct propre · rouge : → Attack · vert : → Attack + LP · ambre : → Attack + LP + Detune |
| Q | 4 résonances (LED de plus en plus lumineuse) |
| gros bouton blanc | sélecteur à 8 crans : tourner (glisser vers le haut / la droite ou molette) **charge aussitôt** le preset, butées en 1 et 8 · clic = recharger · maintenir 3 s = sauvegarder |
| footswitch PRESET | charge le preset choisi ; déjà chargé → suivant ; modifié → le recharge |
| LED PRESET | fixe = chargé · clignote = modifié |
| footswitch BYPASS | effet / bypass |

Double-clic sur un fader = valeur par défaut. MIDI (clic droit) : footswitch PRESET = CC 80, BYPASS = CC 81 par défaut, Program Change 1-8 = presets 1-8. **MIDI Learn** sur toutes les commandes : clic droit sur un fader, DRY FX, Q, le bouton PRESET ou un footswitch -> « MIDI Learn », puis bouger la pédale d'expression / appuyer sur le bouton MIDI ; « Clear MIDI » retire l'assignation. Le sous-menu MIDI liste toutes les commandes et « Reset MIDI to defaults ». Assignations sauvegardées avec le projet.

## Moteur
~190 filtres passe-bande complexes à 3 étages (≈ 3 par demi-ton au-dessus de 360 Hz) ; dans chaque bande la phase est multipliée (+1 / +2) ou divisée (-1 / -2) ; seules les bandes qui sont un pic du spectre parlent → chaque corde d'un accord garde sa propre octave. Les bandes sous 1 kHz tournent à ~12 kHz pour économiser le processeur. Limiteur doux en sortie, filtre anti-grondement 16 Hz sur les octaves basses. Porte de bruit par bande (le souffle et le ronflement ne déclenchent plus de notes) et suiveur d’arrêt : quand la guitare s’arrête net, les octaves se coupent aussitôt au lieu de sonner ~80 ms en « bip ». Porte de repos : guitare branchée mais pas jouée (ronflement, buzz, souffle), les octaves restent muettes ; elles ne s’ouvrent qu’à +12 dB au-dessus du bruit de fond appris en continu.

- v1.3.1 : menu de taille de fenêtre : « 60 % (default) », 75 %, 100 % (plus de taille en pixels)
