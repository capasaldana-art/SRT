# SRT - Version pilote terrain

Ce dossier regroupe les pieces principales du pilote SRT.

## Application web

Fichier :

```text
web/index.html
```

A ouvrir dans Chrome ou Edge.

Fonctions :

- Dossiers en cours...
- Nouveau Dossier SRT
- Gestion CSR Tags
- generation QR Codes
- impression et telechargement QR Codes

## Borne SRT ESP32

Fichiers :

```text
arduino/borne_srt_test_entree_retour_v1.ino
arduino/borne_srt_entree_test_bridge_v1.ino
```

Utiliser d'abord :

```text
borne_srt_test_entree_retour_v1.ino
```

Objectif : verifier que la Borne SRT voit les 5 CSR Tags.

Utiliser ensuite :

```text
borne_srt_entree_test_bridge_v1.ino
```

Objectif : preparer le lien USB serie vers le simulateur Telegram.

## Telegram

Fichier :

```text
telegram/srt_telegram_simulateur_v9_bridge.py
```

Principe :

```text
Borne SRT -> USB serie -> PC -> Telegram
```

Important : ne pas mettre le token Telegram dans le code. Utiliser les variables :

```text
SRT_BOT_TOKEN
SRT_CHAT_ID
```

## Base de donnees

Fichier :

```text
database/schema_srt_v1_pilote.sql
```

## Documentation

Dossier :

```text
docs
```

Contient :

- architecture V1 pilote
- contrats d'integration
- guide Borne SRT + Telegram
- protocole de test terrain

