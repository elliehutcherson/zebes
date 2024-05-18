-- SQLite
CREATE TABLE SpriteConfig (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type INTEGER NOT NULL,
    type_name TEXT NOT NULL,
    texture_path TEXT NOT NULL,
    ticks_per_sprite INTEGER NOT NULL
);

CREATE TABLE SubSpriteConfig (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sprite_config_id INTEGER NOT NULL,
    sub_sprite_index INTEGER NOT NULL,
    texture_x INTEGER NOT NULL,
    texture_y INTEGER NOT NULL,
    texture_w INTEGER NOT NULL,
    texture_h INTEGER NOT NULL,
    texture_offset_x INTEGER NOT NULL,
    texture_offset_y INTEGER NOT NULL,
    render_w INTEGER NOT NULL,
    render_h INTEGER NOT NULL,
    render_offset_x INTEGER NOT NULL,
    render_offset_y INTEGER NOT NULL,
    FOREIGN KEY (sprite_config_id) REFERENCES SpriteConfig(id)
);