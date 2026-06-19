# README BACKEND SRT

## 1. Ou coller l'URL Apps Script

Le fichier actif GitHub Pages est :

`index.html`

Dans le fichier, chercher :

```javascript
const SRT_BACKEND_URL="COLLER_ICI_L_URL_APPLICATION_WEB_GOOGLE_APPS_SCRIPT_EN_EXEC";
```

Remplacer par l'URL de l'application web Google Apps Script, par exemple :

```javascript
const SRT_BACKEND_URL=localStorage.getItem("srt_backend_url")||"";
```

## 2. Comment tester

1. Ouvrir `https://capasaldana-art.github.io/SRT/`.
2. Creer un dossier test.
3. Exemple : plaque `TEST-01`, CSR Tag `CSR-01`.
4. Le dossier doit rester visible dans le Terminal SRT.
5. Le Terminal envoie aussi le dossier a Apps Script avec un POST.

Payload envoye :

```json
{
  "source": "terminal_srt",
  "action": "create_dossier",
  "dossier_id": "SRT-...",
  "plaque": "TEST-01",
  "csr_tag": "CSR-01",
  "status": "actif",
  "created_at": "...",
  "photos_impact": ["..."]
}
```

## 3. Quoi verifier dans Telegram

Telegram doit recevoir la carte du vehicule signale avec :

- plaque
- CSR Tag
- bouton `Fin chantier`

La photo finale ne se fait pas dans le Terminal SRT.
Elle se fait uniquement dans Telegram apres clic sur `Fin chantier`.

## 4. Quoi verifier dans Apps Script

Dans Google Apps Script :

1. Ouvrir `Executions`.
2. Verifier qu'un `doPost` arrive quand un dossier est cree dans le Terminal.
3. Verifier que le payload contient :
   - `source: "terminal_srt"`
   - `action: "create_dossier"`
   - `dossier_id`
   - `plaque`
   - `csr_tag`

Si Telegram ne recoit rien, verifier d'abord :

- URL Apps Script bien collee dans `SRT_BACKEND_URL`
- Web App deployee en acces `Tout le monde`
- webhook Telegram deja installe
- execution `doPost` visible dans Apps Script

