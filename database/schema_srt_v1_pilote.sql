-- SRT - Spot Repair Tracking
-- Schema pilote V1

CREATE TABLE csr_tags (
    id TEXT PRIMARY KEY,
    nom_csr TEXT NOT NULL UNIQUE,
    mac_ble TEXT NOT NULL UNIQUE,
    qr_payload TEXT NOT NULL UNIQUE,
    qr_code_data_url TEXT,
    statut TEXT NOT NULL DEFAULT 'disponible'
        CHECK (statut IN ('disponible', 'attribue', 'retire')),
    date_creation TEXT NOT NULL,
    date_modification TEXT NOT NULL
);

CREATE TABLE dossiers_srt (
    id TEXT PRIMARY KEY,
    plaque TEXT NOT NULL,
    csr_tag_id TEXT NOT NULL,
    photo_signalement TEXT NOT NULL,
    statut TEXT NOT NULL DEFAULT 'signale'
        CHECK (
            statut IN (
                'signale',
                'mouvement_detecte',
                'retour_detecte',
                'sortie_detectee',
                'termine'
            )
        ),
    photo_finale TEXT,
    date_creation TEXT NOT NULL,
    date_cloture TEXT,
    telegram_message_id TEXT,
    dernier_evenement TEXT NOT NULL DEFAULT 'creation',
    date_dernier_evenement TEXT NOT NULL,
    FOREIGN KEY (csr_tag_id) REFERENCES csr_tags(id)
);

CREATE TABLE evenements_srt (
    id TEXT PRIMARY KEY,
    dossier_id TEXT NOT NULL,
    type_evenement TEXT NOT NULL
        CHECK (
            type_evenement IN (
                'creation',
                'retour_detecte',
                'mouvement_detecte',
                'sortie_detectee',
                'photo_finale',
                'cloture'
            )
        ),
    source TEXT NOT NULL
        CHECK (source IN ('terminal', 'borne_srt', 'telegram')),
    message TEXT NOT NULL,
    date_creation TEXT NOT NULL,
    donnees_log TEXT,
    FOREIGN KEY (dossier_id) REFERENCES dossiers_srt(id)
);

CREATE INDEX idx_csr_tags_nom_csr ON csr_tags(nom_csr);
CREATE INDEX idx_csr_tags_statut ON csr_tags(statut);
CREATE INDEX idx_dossiers_srt_statut ON dossiers_srt(statut);
CREATE INDEX idx_dossiers_srt_csr_tag_id ON dossiers_srt(csr_tag_id);
CREATE INDEX idx_evenements_srt_dossier_id ON evenements_srt(dossier_id);
