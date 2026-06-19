/*
  SRT - BOT TELEGRAM ACTIONNEL
  Workflow officiel :
  Terminal SRT -> Apps Script -> Telegram
  Borne ESP32 -> Apps Script -> dossier actif par CSR Tag -> Telegram enrichi
  Fin chantier -> demande photo finale -> photo Telegram -> cloture -> CSR Tag libere

  A deployer dans Google Apps Script en Web App.
  Puis appeler setupWebhook("URL_WEB_APP_DEPLOYEE") une fois.
*/

const SRT_BOT_TOKEN = "A_REMPLIR_DANS_APPS_SCRIPT";
const SRT_DEFAULT_CHAT_ID = "A_REMPLIR_DANS_APPS_SCRIPT";
const SRT_TERMINAL_URL = "https://capasaldana-art.github.io/SRT/";
const SRT_PENDING_TTL_MS = 2 * 60 * 60 * 1000;

const STORE_DOSSIERS = "srt_dossiers_v1";
const STORE_PENDING = "srt_pending_final_photo_v1";

function doPost(e) {
  const body = e && e.postData && e.postData.contents ? e.postData.contents : "{}";
  let payload;
  try {
    payload = JSON.parse(body);
  } catch (err) {
    return jsonResponse({ ok: false, error: "json_invalide" });
  }

  if (payload && payload.source === "terminal_srt" && payload.action === "create_dossier") {
    return jsonResponse(handleTerminalCreateDossier(payload));
  }

  if (isBorneEventPayload(payload)) {
    return jsonResponse(handleBorneEvent(payload));
  }

  handleTelegramUpdate(payload);
  return jsonResponse({ ok: true });
}

function doGet() {
  return jsonResponse({ ok: true, service: "SRT Telegram Actions Bot" });
}

function setupWebhook(webAppUrl) {
  const response = telegramRequest("setWebhook", {
    url: webAppUrl,
    allowed_updates: ["message", "callback_query"]
  });
  Logger.log(JSON.stringify(response));
}

function handleTelegramUpdate(update) {
  if (update.callback_query) {
    handleCallbackQuery(update.callback_query);
    return;
  }
  if (update.message && update.message.photo) {
    handlePhotoMessage(update.message);
  }
}

function handleTerminalCreateDossier(payload) {
  const dossier = normalizeDossierPayload(payload);
  if (!dossier.dossier_id || !dossier.csr_tag || !dossier.plaque) {
    return { ok: false, error: "dossier_incomplet" };
  }

  const dossiers = loadMap(STORE_DOSSIERS);
  const active = findActiveDossierByTagInMap(dossiers, dossier.csr_tag);
  if (active && active.dossier_id !== dossier.dossier_id) {
    sendTelegramMessage(dossier.chat_id,
      "\u26A0\uFE0F CSR Tag deja actif\n\n" +
      "Plaque : " + active.plaque + "\n" +
      "CSR Tag : " + active.csr_tag + "\n\n" +
      "Un CSR Tag ne peut etre lie qu'a un seul dossier actif."
    );
    return { ok: false, error: "csr_tag_deja_actif", dossier_id: active.dossier_id };
  }

  dossiers[dossier.dossier_id] = dossier;
  saveMap(STORE_DOSSIERS, dossiers);

  sendDossierCard(dossier);
  return { ok: true, dossier_id: dossier.dossier_id };
}

function normalizeDossierPayload(payload) {
  const now = new Date().toISOString();
  const hasImpactPhoto = Boolean(
    (Array.isArray(payload.photos_impact) && payload.photos_impact.length) ||
    (payload.photos && payload.photos.impact)
  );

  return {
    dossier_id: String(payload.dossier_id || payload.id || ""),
    plaque: String(payload.plaque || ""),
    csr_tag: String(payload.csr_tag || payload.csrTag || ""),
    chat_id: String(payload.chat_id || SRT_DEFAULT_CHAT_ID),
    statut: String(payload.status || "dossier_actif"),
    note: String(payload.note || ""),
    has_photo_impact: hasImpactPhoto,
    photo_finale_file_id: "",
    csr_tag_libere: false,
    created_at: payload.created_at || now,
    updated_at: now,
    dernier_evenement: "Dossier cree",
    history: [
      { at: now, action: "dossier_cree" }
    ]
  };
}

function sendDossierCard(dossier) {
  const text =
    "\uD83D\uDE97 Vehicule signale\n\n" +
    "Plaque : " + dossier.plaque + "\n" +
    "CSR Tag : " + dossier.csr_tag + "\n" +
    "Photo impact : " + (dossier.has_photo_impact ? "enregistree" : "non transmise") + "\n\n" +
    "Dossier SRT cree.\n" +
    "Statut : dossier actif";

  return sendTelegramMessage(dossier.chat_id, text, {
    inline_keyboard: [
      [
        {
          text: "\u2705 Fin chantier",
          callback_data: "action:finish_request:" + dossier.dossier_id + ":" + dossier.csr_tag
        }
      ],
      [
        { text: "\uD83D\uDCDE Appeler R\u00E9mi", callback_data: "action:call_remi" }
      ]
    ]
  });
}

function isBorneEventPayload(payload) {
  if (!payload) return false;
  const source = String(payload.source || "").toLowerCase();
  const action = String(payload.action || "").toLowerCase();
  return (
    source === "borne_srt" ||
    source === "esp32" ||
    source === "borne_esp32" ||
    action === "borne_event" ||
    action === "event_detected" ||
    action === "srt_event"
  );
}

function handleBorneEvent(payload) {
  const csrTag = String(payload.csr_tag || payload.csrTag || payload.tag || payload.label || "").trim();
  const chatId = String(payload.chat_id || SRT_DEFAULT_CHAT_ID);
  if (!csrTag) return { ok: false, error: "csr_tag_manquant" };

  const dossiers = loadMap(STORE_DOSSIERS);
  const dossier = findActiveDossierByTagInMap(dossiers, csrTag);
  if (!dossier) {
    sendTelegramMessage(chatId,
      "\u26A0\uFE0F Mouvement detecte sans dossier actif\n\n" +
      "CSR Tag : " + csrTag + "\n\n" +
      "Aucun dossier actif n'est rattache a ce CSR Tag.",
      callRemiKeyboard()
    );
    return { ok: false, error: "aucun_dossier_actif", csr_tag: csrTag };
  }

  const eventKind = normalizeEventKind(payload);
  const now = new Date().toISOString();
  dossier.statut = dossier.statut === "cloture" ? "cloture" : "dossier_actif";
  dossier.updated_at = now;
  dossier.dernier_evenement = eventKind.label;
  dossier.history = dossier.history || [];
  dossier.history.push({ at: now, action: eventKind.history });
  dossiers[dossier.dossier_id] = dossier;
  saveMap(STORE_DOSSIERS, dossiers);

  sendVehicleEventMessage(dossier, eventKind);
  return { ok: true, dossier_id: dossier.dossier_id, csr_tag: csrTag, event: eventKind.type };
}

function normalizeEventKind(payload) {
  const raw = String(payload.event_type || payload.event || payload.zone_type || payload.zone || payload.action || "").toUpperCase();
  if (raw.indexOf("SORTIE") >= 0 || raw.indexOf("EXIT") >= 0) {
    return {
      type: "sortie",
      title: "\uD83D\uDD34 Passage sortie detecte",
      label: "Passage sortie detecte",
      history: "passage_sortie_detecte",
      body: "Le v\u00E9hicule vient de passer en zone sortie.\nS'il part avant intervention, appeler R\u00E9mi pour caler le retour ou d\u00E9cider de la suite."
    };
  }
  if (raw.indexOf("RETOUR") >= 0 || raw.indexOf("ENTREE") >= 0 || raw.indexOf("RETURN") >= 0) {
    return {
      type: "retour",
      title: "\uD83D\uDFE2 Vehicule revenu sur parc",
      label: "Vehicule revenu sur parc",
      history: "vehicule_revenu_sur_parc",
      body: "Le v\u00E9hicule est revenu en zone entr\u00E9e / retour.\nAppeler R\u00E9mi si une d\u00E9cision est n\u00E9cessaire."
    };
  }
  return {
    type: "mouvement",
    title: "\uD83D\uDFE0 Mouvement detecte",
    label: "Mouvement detecte",
    history: "mouvement_detecte",
    body: "Le v\u00E9hicule semble avoir boug\u00E9.\nD\u00E9part, lavage ou d\u00E9placement parc possible.\nDossier SRT \u00E0 v\u00E9rifier."
  };
}

function sendVehicleEventMessage(dossier, eventKind) {
  const text =
    eventKind.title + "\n\n" +
    buildDossierReminder(dossier) + "\n\n" +
    eventKind.body;

  return sendTelegramMessage(dossier.chat_id || SRT_DEFAULT_CHAT_ID, text, callRemiKeyboard());
}

function buildDossierReminder(dossier) {
  return (
    "Plaque : " + valueOrUnknown(dossier.plaque) + "\n" +
    "CSR Tag : " + valueOrUnknown(dossier.csr_tag) + "\n" +
    "Photo impact : " + (dossier.has_photo_impact ? "disponible" : "non transmise") + "\n" +
    "Date creation : " + formatDateForTelegram(dossier.created_at) + "\n" +
    "Statut actuel : " + valueOrUnknown(dossier.statut) + "\n" +
    "Dernier evenement : " + valueOrUnknown(dossier.dernier_evenement) + "\n\n" +
    "Historique court :\n" + buildShortHistory(dossier)
  );
}

function buildShortHistory(dossier) {
  const history = dossier.history || [];
  const last = history.slice(Math.max(0, history.length - 4));
  if (!last.length) return "- Aucun historique";
  return last.map(function (item) {
    return "- " + formatDateForTelegram(item.at) + " : " + String(item.action || "");
  }).join("\n");
}

function findActiveDossierByTagInMap(dossiers, csrTag) {
  const target = String(csrTag || "").trim().toUpperCase();
  const ids = Object.keys(dossiers);
  for (let i = 0; i < ids.length; i++) {
    const d = dossiers[ids[i]];
    if (String(d.csr_tag || "").trim().toUpperCase() === target && d.statut !== "cloture" && !d.csr_tag_libere) {
      return d;
    }
  }
  return null;
}

function handleCallbackQuery(callbackQuery) {
  const data = callbackQuery.data || "";
  const chatId = callbackQuery.message && callbackQuery.message.chat ? callbackQuery.message.chat.id : SRT_DEFAULT_CHAT_ID;
  const userId = callbackQuery.from ? callbackQuery.from.id : "";

  answerCallbackQuery(callbackQuery.id);

  if (data.indexOf("action:finish_request:") === 0) {
    const parts = data.split(":");
    const dossierId = parts[2] || "";
    const csrTag = parts[3] || "";
    requestFinalPhoto(dossierId, csrTag, chatId, userId, callbackQuery.message);
    return;
  }

  if (data === "action:call_remi") {
    sendTelegramMessage(chatId, "\uD83D\uDCDE Appeler R\u00E9mi pour caler la suite du dossier SRT.");
  }
}

function requestFinalPhoto(dossierId, csrTag, chatId, userId, sourceMessage) {
  const dossiers = loadMap(STORE_DOSSIERS);
  let dossier = dossiers[dossierId];

  if (!dossier) {
    dossier = dossierFromTelegramMessage(dossierId, csrTag, chatId, sourceMessage);
    if (!dossier) {
      sendTelegramMessage(chatId, "Dossier introuvable pour " + csrTag + ". Ouvre le Terminal SRT et verifie le CSR Tag.");
      return;
    }
  }

  dossier.statut = "attente_photo_finale";
  dossier.updated_at = new Date().toISOString();
  dossier.dernier_evenement = "Photo finale demandee";
  dossier.history = dossier.history || [];
  dossier.history.push({ at: dossier.updated_at, action: "photo_finale_demandee" });
  dossiers[dossierId] = dossier;
  saveMap(STORE_DOSSIERS, dossiers);

  const prompt =
    "\uD83D\uDCF7 Photo finale demandee\n\n" +
    "Plaque : " + dossier.plaque + "\n" +
    "CSR Tag : " + dossier.csr_tag + "\n\n" +
    "Envoie maintenant la photo du vehicule termine en reponse a ce message.";

  const sent = sendTelegramMessage(chatId, prompt);
  const promptMessageId = sent && sent.result ? sent.result.message_id : "";

  const pending = loadMap(STORE_PENDING);
  pending[pendingKey(chatId, userId)] = {
    dossier_id: dossierId,
    csr_tag: dossier.csr_tag,
    plaque: dossier.plaque,
    chat_id: String(chatId),
    user_id: String(userId || ""),
    prompt_message_id: String(promptMessageId || ""),
    expires_at: Date.now() + SRT_PENDING_TTL_MS
  };
  saveMap(STORE_PENDING, pending);

  if (sourceMessage && sourceMessage.message_id) {
    tryEditDossierCard(chatId, sourceMessage.message_id, dossier, "attente_photo_finale");
  }
}

function dossierFromTelegramMessage(dossierId, csrTag, chatId, sourceMessage) {
  const text = sourceMessage && (sourceMessage.caption || sourceMessage.text) ? String(sourceMessage.caption || sourceMessage.text) : "";
  const plaqueMatch = text.match(/Plaque\s*:\s*([^\n]+)/i);
  const tagMatch = text.match(/CSR Tag\s*:\s*([^\n]+)/i);
  const plaque = plaqueMatch ? plaqueMatch[1].trim() : "";
  const tag = tagMatch ? tagMatch[1].trim() : csrTag;

  if (!plaque || !tag) return null;

  const now = new Date().toISOString();
  return {
    dossier_id: dossierId,
    plaque: plaque,
    csr_tag: tag,
    chat_id: String(chatId),
    statut: "dossier_actif",
    has_photo_impact: false,
    photo_finale_file_id: "",
    csr_tag_libere: false,
    created_at: now,
    updated_at: now,
    dernier_evenement: "Dossier reconstruit depuis Telegram",
    history: [
      { at: now, action: "dossier_reconstruit_depuis_telegram" }
    ]
  };
}

function handlePhotoMessage(message) {
  const chatId = message.chat.id;
  const userId = message.from ? message.from.id : "";
  const pending = loadMap(STORE_PENDING);
  const key = pendingKey(chatId, userId);
  let pendingItem = pending[key] || null;

  if (!pendingItem && message.reply_to_message) {
    pendingItem = findPendingByPromptMessageId(pending, chatId, message.reply_to_message.message_id);
  }

  if (!pendingItem || Number(pendingItem.expires_at || 0) < Date.now()) {
    sendTelegramMessage(chatId,
      "Photo recue, mais aucun dossier n'attend de photo finale.\n" +
      "Utilise d'abord le bouton \"Fin chantier\" sur le dossier concerne."
    );
    return;
  }

  const photos = message.photo || [];
  const bestPhoto = photos[photos.length - 1];
  if (!bestPhoto || !bestPhoto.file_id) {
    sendTelegramMessage(chatId, "Photo recue, mais impossible de recuperer le fichier. Reessaie avec une nouvelle photo.");
    return;
  }

  closeDossierWithFinalPhoto(pendingItem, bestPhoto.file_id);
  delete pending[key];
  removePendingByDossierId(pending, pendingItem.dossier_id);
  saveMap(STORE_PENDING, pending);

  sendTelegramMessage(chatId,
    "\uD83D\uDCF7 Photo finale recue\n\n" +
    "Plaque : " + pendingItem.plaque + "\n" +
    "CSR Tag : " + pendingItem.csr_tag + "\n\n" +
    "Le vehicule est termine."
  );

  sendTelegramMessage(chatId,
    "\uD83D\uDD13 CSR Tag libere \uD83D\uDC51\u2744\uFE0F\n\n" +
    "Plaque : " + pendingItem.plaque + "\n" +
    "CSR Tag : " + pendingItem.csr_tag + "\n\n" +
    "Lib\u00E9r\u00E9, d\u00E9livr\u00E9.\n" +
    "Ce tag ne me suivra plus jamais.\n\n" +
    "Il est pr\u00EAt pour la prochaine mission."
  );
}

function closeDossierWithFinalPhoto(pendingItem, fileId) {
  const dossiers = loadMap(STORE_DOSSIERS);
  const dossier = dossiers[pendingItem.dossier_id] || {};
  const now = new Date().toISOString();

  dossier.dossier_id = pendingItem.dossier_id;
  dossier.plaque = pendingItem.plaque;
  dossier.csr_tag = pendingItem.csr_tag;
  dossier.chat_id = pendingItem.chat_id;
  dossier.statut = "cloture";
  dossier.photo_finale_file_id = fileId;
  dossier.csr_tag_libere = true;
  dossier.updated_at = now;
  dossier.closed_at = now;
  dossier.dernier_evenement = "CSR Tag libere";
  dossier.history = dossier.history || [];
  dossier.history.push({ at: now, action: "photo_finale_recue" });
  dossier.history.push({ at: now, action: "dossier_cloture" });
  dossier.history.push({ at: now, action: "csr_tag_libere" });

  dossiers[pendingItem.dossier_id] = dossier;
  saveMap(STORE_DOSSIERS, dossiers);
}

function tryEditDossierCard(chatId, messageId, dossier, status) {
  const text =
    "\uD83D\uDE97 Vehicule signale\n\n" +
    "Plaque : " + dossier.plaque + "\n" +
    "CSR Tag : " + dossier.csr_tag + "\n\n" +
    "Statut : " + status + "\n" +
    "Photo finale : demandee\n" +
    "CSR Tag : non libere";

  try {
    telegramRequest("editMessageText", {
      chat_id: String(chatId),
      message_id: Number(messageId),
      text: text,
      reply_markup: {
        inline_keyboard: [[{ text: "\uD83D\uDCF7 Photo finale demandee", callback_data: "action:noop" }]]
      }
    });
  } catch (err) {
    sendTelegramMessage(chatId, text);
  }
}

function callRemiKeyboard() {
  return {
    inline_keyboard: [
      [{ text: "\uD83D\uDCDE Appeler R\u00E9mi", callback_data: "action:call_remi" }]
    ]
  };
}

function sendTelegramMessage(chatId, text, replyMarkup) {
  const payload = {
    chat_id: String(chatId),
    text: text,
    disable_web_page_preview: false
  };
  if (replyMarkup) payload.reply_markup = replyMarkup;
  return telegramRequest("sendMessage", payload);
}

function answerCallbackQuery(callbackQueryId) {
  if (!callbackQueryId) return;
  telegramRequest("answerCallbackQuery", { callback_query_id: callbackQueryId });
}

function telegramRequest(method, payload) {
  const response = UrlFetchApp.fetch(telegramUrl(method), {
    method: "post",
    contentType: "application/json; charset=utf-8",
    payload: JSON.stringify(payload),
    muteHttpExceptions: true
  });
  const text = response.getContentText();
  try {
    return JSON.parse(text);
  } catch (err) {
    return { ok: false, raw: text };
  }
}

function telegramUrl(method) {
  return "https://api.telegram.org/bot" + SRT_BOT_TOKEN + "/" + method;
}

function pendingKey(chatId, userId) {
  return String(chatId) + ":" + String(userId || "unknown");
}

function findPendingByPromptMessageId(pending, chatId, messageId) {
  const target = String(messageId);
  const chat = String(chatId);
  const keys = Object.keys(pending);
  for (let i = 0; i < keys.length; i++) {
    const item = pending[keys[i]];
    if (String(item.chat_id) === chat && String(item.prompt_message_id) === target) {
      return item;
    }
  }
  return null;
}

function removePendingByDossierId(pending, dossierId) {
  const keys = Object.keys(pending);
  for (let i = 0; i < keys.length; i++) {
    if (pending[keys[i]].dossier_id === dossierId) delete pending[keys[i]];
  }
}

function valueOrUnknown(value) {
  const text = String(value || "").trim();
  return text ? text : "non renseigne";
}

function formatDateForTelegram(value) {
  if (!value) return "non renseignee";
  try {
    return Utilities.formatDate(new Date(value), "Europe/Paris", "dd/MM/yyyy HH:mm");
  } catch (err) {
    return String(value);
  }
}

function loadMap(key) {
  const raw = PropertiesService.getScriptProperties().getProperty(key);
  if (!raw) return {};
  try {
    return JSON.parse(raw) || {};
  } catch (err) {
    return {};
  }
}

function saveMap(key, value) {
  PropertiesService.getScriptProperties().setProperty(key, JSON.stringify(value));
}

function jsonResponse(value) {
  return ContentService
    .createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}

