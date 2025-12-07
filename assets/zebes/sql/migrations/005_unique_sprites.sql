-- Migration 005: Add unique constraints to SpriteConfig

-- Create the new table with UNIQUE constraints
CREATE TABLE IF NOT EXISTS SpriteConfig_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type INTEGER NOT NULL UNIQUE,
    type_name TEXT NOT NULL UNIQUE,
    texture_path TEXT NOT NULL,
    FOREIGN KEY (texture_path) REFERENCES TextureConfig (texture_path) ON DELETE CASCADE
);

-- Copy data from the old table to the new table
-- Note: 'ticks_per_sprite' was removed in migration 004, so we don't copy it.
INSERT INTO
    SpriteConfig_new (
        id,
        type,
        type_name,
        texture_path
    )
SELECT id, type, type_name, texture_path
FROM SpriteConfig;

-- Drop the old table
DROP TABLE SpriteConfig;

-- Rename the new table to the old name
ALTER TABLE SpriteConfig_new RENAME TO SpriteConfig;