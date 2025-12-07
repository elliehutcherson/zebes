-- Migration 004: Remove ticks_per_sprite and add frames_per_cycle

-- frames_per_cycle already added in 003

-- Copy ticks_per_sprite to frames_per_cycle for existing frames
UPDATE SpriteFrameConfig
SET
    frames_per_cycle = (
        SELECT ticks_per_sprite
        FROM SpriteConfig
        WHERE
            SpriteConfig.id = SpriteFrameConfig.sprite_config_id
    );

-- Remove ticks_per_sprite from SpriteConfig
-- SQLite < 3.35.0 does not support DROP COLUMN.
-- Using a safer method: creating new table, copying, dropping old, renaming.

CREATE TABLE SpriteConfig_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type INTEGER NOT NULL,
    type_name TEXT NOT NULL,
    texture_path TEXT NOT NULL
);

INSERT INTO
    SpriteConfig_new (
        id,
        type,
        type_name,
        texture_path
    )
SELECT id, type, type_name, texture_path
FROM SpriteConfig;

DROP TABLE SpriteConfig;

ALTER TABLE SpriteConfig_new RENAME TO SpriteConfig;