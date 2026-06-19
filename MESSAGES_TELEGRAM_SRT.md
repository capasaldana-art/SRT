# MESSAGES TELEGRAM SRT

Reference officielle des messages Telegram du projet SRT.

Les futures modifications BLE / distance / anti-spam ne doivent jamais reecrire les messages Telegram officiels.

## Regle produit

Telegram doit informer et declencher la bonne action humaine.
Telegram ne doit pas devenir un outil de planification complet.

Il ne faut pas creer de workflow Telegram `Proposer date retour`, ni de boutons `Aujourd'hui`, `Demain`, `Dans 2 jours`, `Date manuelle`, ni de stockage de date retour.

Pour les evenements sortie / retour / mouvement, le bouton attendu est :

`📞 Appeler Rémi`

## Architecture metier

- Terminal SRT : creation du dossier parc, plaque, photo impact, CSR Tag.
- Bornes ESP32 : envoient les evenements terrain a Apps Script.
- Apps Script : retrouve le dossier actif par CSR Tag et envoie Telegram.
- Telegram : cockpit d'action, Fin chantier, photo finale, cloture.

Un message Telegram evenement doit etre comprehensible seul, sans remonter le fil.

## Message dossier cree

🚗 Vehicule signale

Plaque : {PLAQUE}
CSR Tag : {CSR_TAG}
Photo impact : {DISPONIBLE_OU_NON}

Dossier SRT cree.
Statut : dossier actif

Boutons :

- ✅ Fin chantier
- 📞 Appeler Rémi

## Sortie detectee

🔴 Passage sortie detecte

Plaque : {PLAQUE}
CSR Tag : {CSR_TAG}
Photo impact : {DISPONIBLE_OU_NON}
Date creation : {DATE_CREATION}
Statut actuel : {STATUT}
Dernier evenement : {DERNIER_EVENEMENT}

Historique court :
{HISTORIQUE_COURT}

Le vehicule vient de passer en zone sortie.
S'il part avant intervention, appeler Remi pour caler le retour ou decider de la suite.

Bouton unique :

- 📞 Appeler Rémi

## Vehicule revenu

🟢 Vehicule revenu sur parc

Plaque : {PLAQUE}
CSR Tag : {CSR_TAG}
Photo impact : {DISPONIBLE_OU_NON}
Date creation : {DATE_CREATION}
Statut actuel : {STATUT}
Dernier evenement : {DERNIER_EVENEMENT}

Historique court :
{HISTORIQUE_COURT}

Le vehicule est revenu en zone entree / retour.
Appeler Rémi si une decision est necessaire.

Bouton unique :

- 📞 Appeler Rémi

## Mouvement detecte

🟠 Mouvement detecte

Plaque : {PLAQUE_SI_CONNUE}
CSR Tag : {CSR_TAG}

Le vehicule semble avoir bouge.
Depart, lavage ou deplacement parc possible.

Dossier SRT a verifier.

Bouton unique :

- 📞 Appeler Rémi

## Photo finale depuis Telegram

La photo finale ne se prend pas dans le Terminal SRT.
La fin chantier est une action Telegram.

Workflow officiel :

1. Le dossier est cree depuis le Terminal SRT.
2. Telegram affiche la carte dossier.
3. L'intervenant clique sur `✅ Fin chantier`.
4. Le bot passe le dossier en statut `attente_photo_finale`.
5. Le bot demande la photo finale dans Telegram.
6. L'intervenant envoie la photo finale dans Telegram.
7. Le bot rattache la photo au dossier.
8. Le bot cloture le dossier.
9. Le bot libere le CSR Tag.

Pas de photo finale Telegram = pas de CSR Tag libere.

## Liberation officielle

🔓 CSR Tag libere 👑❄️

Plaque : {PLAQUE}
CSR Tag : {CSR_TAG}

Libéré, délivré.
Ce tag ne me suivra plus jamais.

Il est prêt pour la prochaine mission.
