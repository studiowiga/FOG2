# Fog2 — Studio Wiga

Plugin VST3 (Windows 64 bits + macOS universel Apple Silicon/Intel) qui copie la pédale **Electro-Harmonix POG2** (Polyphonic Octave Generator), sur le visuel « FOG2 ».

## Compiler avec GitHub
1. Mets **tous les fichiers à la racine du dépôt** (avec le dossier `.github/workflows`).
2. Un push lance l'onglet **Actions** → workflow *Build Fog2 VST3 (Windows + macOS)*.
3. À la fin, télécharge les artefacts :
   - `Fog2-VST3-Windows` → copie `Fog2.vst3` dans `C:\Program Files\Common Files\VST3\`
   - `Fog2-VST3-macOS` → copie `Fog2.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` puis lance une fois `xattr -cr ~/Library/Audio/Plug-Ins/VST3/Fog2.vst3`
4. Dans Reaper : re-scanne les plug-ins. Le plugin apparaît comme **Fog2 (Studio Wiga)**.

## Commandes (comme sur la POG2)
| Commande | Rôle |
|---|---|
| DRY OUTPUT | niveau du son direct (100 % au repère 80 %) |
| -2 / -1 / +1 / +2 OCTAVES | niveau de chaque voix d'octave (polyphonique : accords OK) |
| ATTACK | montée en volume des octaves, note par note (en bas = coupé, jusqu'à 3 s → effet « reverse ») |
| LP FILTER | fréquence du filtre passe-bas 2 pôles (50 Hz → 18 kHz) |
| DETUNE | désaccord / chorus des voix +1 et +2 (la vitesse ET la profondeur augmentent) |
| bouton **DRY FX** | LED éteinte : direct propre · rouge : direct → Attack · vert : direct → Attack + LP · ambre : direct → Attack + LP + Detune |
| bouton **Q** | 4 résonances du filtre (la LED s'allume de plus en plus fort) |
| bouton PRESET (rond blanc) | tourner (glisser / molette) = choisir 1-8 · clic = charger · **maintenir 3 s** = sauvegarder (les LED clignotent) |
| footswitch PRESET | charge le preset choisi ; s'il est déjà chargé → preset suivant ; s'il a été modifié → le recharge |
| LED PRESET | fixe = preset chargé · clignote vite = preset modifié |
| footswitch BYPASS | effet / bypass (LED rouge allumée = effet actif) |

Double-clic sur un fader = valeur par défaut. Survoler / déplacer un fader affiche sa valeur.

## Clic droit (n'importe où)
- **Plugin size** : 67 % (défaut, taille minimale), 75 %, 100 % — on peut aussi tirer le coin, jamais plus petit que 67 %.
- **Factory sounds** : 12 sons (12-String, Organ, Bass Sub, Cello Swell, String Pad, Glass Bells, Leslie, Reso Sub Synth…).
- **Restore factory sounds in presets 1-8**.
- **MIDI** : footswitch PRESET = CC 80, BYPASS = CC 81 par défaut, « learn » pour en choisir d'autres (clic droit sur un footswitch), mode pédales « latching ». Program Change 1-8 = presets 1-8.

## Fonctionnement du son
Banc de ~150 filtres passe-bande complexes (2 par demi-ton) ; dans chaque bande la phase est doublée/quadruplée (+1/+2) ou divisée par 2/4 (-1/-2). Seules les bandes qui sont un pic du spectre parlent, donc chaque corde produit sa propre octave → vraie polyphonie, sans « phasing ». Les bandes sous 1 kHz tournent à ~12 kHz pour économiser le processeur.
