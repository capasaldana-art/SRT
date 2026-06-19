#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
SRT Telegram Simulateur V9 Bridge

Objectif :
- Garder le principe du Dossier SRT vivant.
- Eviter de mettre le token Telegram dans le code.
- Lire les evenements de la Borne SRT depuis le port serie.
- Transformer SRT_EVENT|tag=CSR-03|event=retour_detecte en alerte Telegram.

Installation :
    pip install requests pyserial pillow

Variables a definir :
    SRT_BOT_TOKEN
    SRT_CHAT_ID

Lancement sans Borne SRT :
    python srt_telegram_simulateur_v9_bridge.py

Lancement avec Borne SRT :
    python srt_telegram_simulateur_v9_bridge.py --serial COM5
"""

import argparse
import os
import sys
import time
from datetime import datetime
from pathlib import Path

import requests

try:
    import serial
except ImportError:
    serial = None


BOT_TOKEN = os.getenv("SRT_BOT_TOKEN", "")
CHAT_ID = os.getenv("SRT_CHAT_ID", "")
API = f"https://api.telegram.org/bot{BOT_TOKEN}" if BOT_TOKEN else ""
DEMO_IMAGE_PATH = Path("srt_photo_signalement_demo.jpg")


state = {
    "dossiers": {
        "CSR-03": {
            "plaque": "AA-984-55",
            "modele": "Clio blanche",
            "csr_tag": "CSR-03",
            "photos_signalement": 1,
            "photo_finale": False,
            "status": "signale",
            "last_event": "Dossier SRT cree",
            "last_seen": "",
            "message_id": None,
        }
    },
    "tags_libres": ["CSR-01", "CSR-02", "CSR-04", "CSR-05"],
}


def now_fr():
    return datetime.now().strftime("%d/%m/%Y %H:%M")


def ensure_config():
    if not BOT_TOKEN or not CHAT_ID:
        print("Configuration Telegram manquante.")
        print("Definis SRT_BOT_TOKEN et SRT_CHAT_ID avant de lancer le simulateur.")
        print("Exemple PowerShell :")
        print("$env:SRT_BOT_TOKEN='123456:ABC'")
        print("$env:SRT_CHAT_ID='-1001234567890'")
        return False
    return True


def tg_post(method, payload=None, files=None):
    try:
        if files:
            response = requests.post(f"{API}/{method}", data=payload or {}, files=files, timeout=30)
        else:
            response = requests.post(f"{API}/{method}", json=payload or {}, timeout=20)

        if not response.ok:
            print("Erreur Telegram:", response.text)
        return response
    except Exception as exc:
        print("Erreur reseau Telegram:", exc)
        return None


def button(text, data):
    return {"text": text, "callback_data": data}


def send_text(text, buttons=None):
    payload = {
        "chat_id": CHAT_ID,
        "text": text.strip(),
        "parse_mode": "HTML",
        "disable_web_page_preview": True,
    }
    if buttons:
        payload["reply_markup"] = {"inline_keyboard": buttons}
    return tg_post("sendMessage", payload)


def send_photo(path, caption, reply_markup=None):
    payload = {
        "chat_id": CHAT_ID,
        "caption": caption.strip(),
        "parse_mode": "HTML",
    }
    if reply_markup:
        payload["reply_markup"] = reply_markup

    with open(path, "rb") as photo_file:
        return tg_post("sendPhoto", payload=payload, files={"photo": photo_file})


def edit_caption(message_id, caption, reply_markup=None):
    payload = {
        "chat_id": CHAT_ID,
        "message_id": message_id,
        "caption": caption.strip(),
        "parse_mode": "HTML",
    }
    if reply_markup:
        payload["reply_markup"] = reply_markup
    return tg_post("editMessageCaption", payload)


def ensure_demo_image():
    if DEMO_IMAGE_PATH.exists():
        return

    try:
        from PIL import Image, ImageDraw
    except ImportError:
        raise RuntimeError("Module manquant : pip install pillow")

    img = Image.new("RGB", (900, 1200), (238, 238, 238))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((70, 200, 830, 820), radius=45, fill=(248, 248, 248), outline=(180, 180, 180), width=6)
    draw.rounded_rectangle((145, 490, 755, 720), radius=35, fill=(215, 215, 215), outline=(160, 160, 160), width=4)
    draw.ellipse((515, 555, 635, 675), outline=(210, 60, 40), width=10)
    draw.line((495, 535, 655, 695), fill=(210, 60, 40), width=6)
    draw.line((655, 535, 495, 695), fill=(210, 60, 40), width=6)
    draw.rounded_rectangle((90, 875, 810, 1080), radius=20, fill=(30, 30, 30))
    draw.text((125, 915), "PHOTO SIGNALEMENT SRT", fill=(255, 255, 255))
    draw.text((125, 975), "Plaque : AA-984-55", fill=(255, 255, 255))
    draw.text((125, 1030), "CSR Tag : CSR-03", fill=(255, 255, 255))
    img.save(DEMO_IMAGE_PATH, quality=92)


def get_dossier(tag):
    return state["dossiers"].get(tag)


def dossier_keyboard(tag):
    return {
        "inline_keyboard": [
            [button("Dossiers en cours...", "check")],
            [button("Intervention terminee", f"done:{tag}")],
        ]
    }


def dossier_caption(d):
    last_seen = d["last_seen"] or "pas encore detecte"
    photo_finale = "ajoutee" if d["photo_finale"] else "non ajoutee"

    return f"""
<b>VEHICULE SIGNALE</b>

<b>{d["plaque"]}</b>
{d["modele"]}

<b>CSR Tag :</b> {d["csr_tag"]}
<b>Signalement :</b> {d["photos_signalement"]} photo
<b>Statut :</b> {d["status"]}
<b>Dernier evenement :</b> {d["last_event"]}
<b>Derniere detection :</b> {last_seen}
<b>Photo finale :</b> {photo_finale}
"""


def publish_or_update_dossier(tag):
    d = get_dossier(tag)
    if not d:
        send_text(f"Dossier SRT introuvable pour {tag}.")
        return

    caption = dossier_caption(d)
    markup = dossier_keyboard(tag)

    if d["message_id"] is None:
        response = send_photo(DEMO_IMAGE_PATH, caption, reply_markup=markup)
        if response and response.ok:
            d["message_id"] = response.json()["result"]["message_id"]
    else:
        edit_caption(d["message_id"], caption, reply_markup=markup)


def action_creation(tag="CSR-03"):
    d = get_dossier(tag)
    if not d:
        send_text(f"Dossier SRT introuvable pour {tag}.")
        return

    d["status"] = "signale"
    d["last_event"] = f"Dossier cree le {now_fr()}"

    if tag in state["tags_libres"]:
        state["tags_libres"].remove(tag)

    publish_or_update_dossier(tag)


def action_retour(tag="CSR-03", source="console"):
    d = get_dossier(tag)
    if not d:
        send_text(f"Retour detecte pour {tag}, mais aucun Dossier SRT actif.")
        return

    d["status"] = "present sur parc"
    d["last_seen"] = now_fr()
    d["last_event"] = f"Retour detecte le {d['last_seen']} par {source}"

    publish_or_update_dossier(tag)
    send_text(f"<b>RETOUR DETECTE</b>\n\n{d['plaque']}\n{tag}\n\nDossier SRT mis a jour.")


def action_mouvement(tag="CSR-03", source="console"):
    d = get_dossier(tag)
    if not d:
        send_text(f"Mouvement detecte pour {tag}, mais aucun Dossier SRT actif.")
        return

    d["status"] = "mouvement detecte"
    d["last_event"] = f"Mouvement detecte le {now_fr()} par {source}"

    publish_or_update_dossier(tag)
    send_text(f"<b>MOUVEMENT DETECTE</b>\n\n{d['plaque']}\n{tag}\n\nDossier SRT mis a jour.")


def action_sortie(tag="CSR-03", source="console"):
    d = get_dossier(tag)
    if not d:
        send_text(f"Sortie detectee pour {tag}, mais aucun Dossier SRT actif.")
        return

    d["status"] = "sorti"
    d["last_event"] = f"Sortie detectee le {now_fr()} par {source}"

    publish_or_update_dossier(tag)
    send_text(f"<b>SORTIE DETECTEE</b>\n\n{d['plaque']}\n{tag}\n\nDossier SRT mis a jour.")


def finish_dossier(tag="CSR-03"):
    d = get_dossier(tag)
    if not d:
        send_text(f"Dossier SRT introuvable pour {tag}.")
        return

    d["status"] = "termine"
    d["last_event"] = f"Intervention terminee le {now_fr()}"

    if tag not in state["tags_libres"]:
        state["tags_libres"].append(tag)

    publish_or_update_dossier(tag)

    phrases = [
        "Se grenn diri ka fe sak diri.",
        "Petits degats. Grandes differences.",
        "Quelques minutes aujourd'hui. Beaucoup moins demain.",
    ]
    phrase = phrases[hash(d["plaque"]) % len(phrases)]

    send_text(f"""
<b>Intervention terminee.</b>

{d["plaque"]}
{d["csr_tag"]} disponible pour un nouveau dossier.

{phrase}
""")


def parse_srt_event(line):
    line = line.strip()
    if not line.startswith("SRT_EVENT|"):
        return None

    event = {}
    for part in line.split("|")[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            event[key.strip()] = value.strip()
    return event


def handle_srt_event(event):
    tag = event.get("tag")
    kind = event.get("event")
    borne = event.get("borne", "Borne SRT")

    if not tag or not kind:
        print("Evenement incomplet:", event)
        return

    print(f"Evenement recu : {kind} / {tag} / {borne}")

    if kind == "retour_detecte":
        action_retour(tag, source=borne)
    elif kind == "mouvement_detecte":
        action_mouvement(tag, source=borne)
    elif kind == "sortie_detectee":
        action_sortie(tag, source=borne)
    else:
        print("Evenement ignore:", kind)


def serial_loop(port, baudrate=115200):
    if serial is None:
        print("Module pyserial manquant : pip install pyserial")
        return

    print(f"Ecoute Borne SRT sur {port} a {baudrate} bauds.")
    with serial.Serial(port, baudrate=baudrate, timeout=1) as ser:
        while True:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            print(line)

            event = parse_srt_event(line)
            if event:
                handle_srt_event(event)


def menu():
    print("""
SRT Telegram Simulateur V9 Bridge

1 - Creer / publier Dossier SRT CSR-03
2 - Simuler retour detecte CSR-03
3 - Simuler mouvement detecte CSR-03
4 - Simuler sortie detectee CSR-03
5 - Liberer CSR Tag CSR-03
0 - Quitter
""")


def console_loop():
    while True:
        menu()
        choice = input("Choix : ").strip()
        if choice == "1":
            action_creation("CSR-03")
        elif choice == "2":
            action_retour("CSR-03")
        elif choice == "3":
            action_mouvement("CSR-03")
        elif choice == "4":
            action_sortie("CSR-03")
        elif choice == "5":
            finish_dossier("CSR-03")
        elif choice == "0":
            print("Fin.")
            break
        else:
            print("Choix invalide.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", help="Port serie de la Borne SRT, exemple COM5")
    parser.add_argument("--baudrate", type=int, default=115200)
    args = parser.parse_args()

    if not ensure_config():
        return 1

    ensure_demo_image()

    if args.serial:
        serial_loop(args.serial, args.baudrate)
    else:
        console_loop()

    return 0


if __name__ == "__main__":
    sys.exit(main())
