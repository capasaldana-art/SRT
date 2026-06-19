# MANIFEST SRT

## Fichiers modifies

- `MESSAGES_TELEGRAM_SRT.md`
- `MANIFEST_SRT.md`
- `firmware/borne_srt_workflow_reel_court_v1/borne_srt_entree_workflow_reel_court_v1.ino`
- `firmware/borne_srt_workflow_reel_court_v1/borne_srt_sortie_workflow_reel_court_v1.ino`
- `outputs/repo-srt-v1/web/index.html`
- `google_apps_script_telegram_actions_srt.gs`
- `README_BACKEND_SRT.md`

## Messages officialises

- Demarrage borne ESP32
- Entree / retour
- Sortie
- Mouvement
- Evenements borne rattaches au dossier actif par CSR Tag
- Creation dossier Terminal SRT
- Photo finale
- Intervention terminee
- Cloture dossier
- Photo finale depuis Telegram
- Liberation CSR Tag apres photo finale

## Fichiers a copier

- Dossier Arduino : `outputs/A_FLASHER_ARDUINO_SRT_WORKFLOW_COURT`
- Zip Arduino : `outputs/A_FLASHER_ARDUINO_SRT_WORKFLOW_COURT.zip`
- Dossier GitHub : `outputs/A_COPIER_SUR_GITHUB_SRT_V1`
- Zip GitHub : `outputs/A_COPIER_SUR_GITHUB_SRT_V1.zip`
- Bot Apps Script : `google_apps_script_telegram_actions_srt.gs`
- Configuration backend : `README_BACKEND_SRT.md`

## Rappel important

Les futures modifications BLE, distance, RSSI, validation, anti-spam ou mouvement ne doivent pas modifier les messages Telegram officiels.

La photo finale ne doit pas etre prise dans le Terminal SRT.
Le clic Telegram `Fin chantier` demande seulement la photo.
Le CSR Tag est libere uniquement apres reception de la photo finale dans Telegram.

Pas de workflow Telegram `Proposer date retour`.
Pas de boutons `Aujourd'hui`, `Demain`, `Dans 2 jours` ou `Date manuelle`.
Les messages sortie / retour / mouvement gardent un seul bouton : `Appeler Rémi`.

Les messages terrain sont dans :

- `MESSAGES_TELEGRAM_SRT.md`
- fonctions `buildStartupMessage()`, `buildEntryMessage()`, `buildExitMessage()`, `buildMotionMessage()` dans les firmwares
- constante `SRT_TELEGRAM_MESSAGES` dans `index.html`
