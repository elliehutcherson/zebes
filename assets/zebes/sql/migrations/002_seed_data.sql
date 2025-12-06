-- Migration 002: Seed Data

-- Inserting record into TextureConfig table
INSERT OR IGNORE INTO
    TextureConfig (texture_path)
VALUES (
        'assets/zebes/textures/samus-idle-left.png'
    ),
    (
        'assets/zebes/textures/samus-idle-right.png'
    ),
    (
        'assets/zebes/textures/samus-turning-left.png'
    ),
    (
        'assets/zebes/textures/samus-turning-right.png'
    ),
    (
        'assets/zebes/textures/samus-running-left.png'
    ),
    (
        'assets/zebes/textures/samus-running-right.png'
    ),
    (
        'assets/zebes/textures/samus-jumping-left.png'
    ),
    (
        'assets/zebes/textures/samus-jumping-right.png'
    ),
    (
        'assets/zebes/textures/sunny-back.png'
    ),
    (
        'assets/zebes/textures/sunny-custom-tileset.png'
    ),
    (
        'assets/zebes/textures/sunny-middle.png'
    ),
    (
        'assets/zebes/textures/sunny-props.png'
    ),
    (
        'assets/zebes/textures/sunny-tileset.png'
    );

-- Inserting records into SpriteConfig table
INSERT OR IGNORE INTO
    SpriteConfig (
        id,
        type,
        type_name,
        texture_path,
        ticks_per_sprite
    )
VALUES (
        1,
        1,
        'kGrass1',
        'assets/zebes/textures/sunny-tileset.png',
        0
    ),
    (
        2,
        2,
        'kGrass2',
        'assets/zebes/textures/sunny-tileset.png',
        0
    ),
    (
        3,
        3,
        'kGrass3',
        'assets/zebes/textures/sunny-tileset.png',
        0
    ),
    (
        4,
        4,
        'kDirt1',
        'assets/zebes/textures/sunny-tileset.png',
        0
    ),
    (
        5,
        5,
        'kGrassSlopeUpRight',
        'assets/zebes/textures/sunny-tileset.png',
        0
    ),
    (
        6,
        6,
        'kGrass1Left',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        7,
        7,
        'kGrass1Right',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        8,
        8,
        'kGrass1Down',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        9,
        9,
        'kGrassCornerUpLeft',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        10,
        10,
        'kGrassCornerUpRight',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        11,
        11,
        'kGrassCornerDownLeft',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        12,
        12,
        'kGrassCornerDownRight',
        'assets/zebes/textures/sunny-custom-tileset.png',
        0
    ),
    (
        100,
        100,
        'kSamusIdleLeft',
        'assets/zebes/textures/samus-idle-left.png',
        10
    ),
    (
        101,
        101,
        'kSamusIdleRight',
        'assets/zebes/textures/samus-idle-right.png',
        10
    ),
    (
        102,
        102,
        'kSamusTurningLeft',
        'assets/zebes/textures/samus-turning-left.png',
        2
    ),
    (
        103,
        103,
        'kSamusTurningRight',
        'assets/zebes/textures/samus-turning-right.png',
        2
    ),
    (
        104,
        104,
        'kSamusRunningLeft',
        'assets/zebes/textures/samus-running-left.png',
        2
    ),
    (
        105,
        105,
        'kSamusRunningRight',
        'assets/zebes/textures/samus-running-right.png',
        2
    );

-- Inserting records into SpriteFrameConfig table
-- Note: SpriteFrameConfig does not have a UNIQUE constraint on the data fields (only ID),
-- so INSERT OR IGNORE might duplicate data if run multiple times on a dirty DB without migration tracking.
-- However, for a fresh start or proper migration tracking, this is fine.
-- For legacy safety, we could check existence, but standard SQL doesn't make that easy in one statement.
-- Given we are relying on 'SchemaMigrations' table to prevent re-run, this is acceptable.
-- If SchemaMigrations is missing (Legacy DB), we trust consistent IDs in SpriteConfig and hope for the best
-- or we assume the user accepts potential duplication if they wiped the migration table but kept the data table.
-- BUT, in the legacy DB case, SchemaMigrations is created, version is 0. 002 runs.
-- SQLite 'INSERT OR IGNORE' works on PRIMARY KEY too. So if we specify IDs, we are safe!
-- The original insert.sql did NOT specify IDs for SpriteFrameConfig.
-- To be safe, I should specify IDs if I want to use INSERT OR IGNORE effectively against existing data.
-- Looking at insert.sql, it matches SpriteConfig IDs (7, 8...) but leaves its own IDs auto-increment.
-- Let's just use regular INSERT. If it's a legacy DB, the user might have duplicate frames if we are not careful.
-- However, we can probably assume that if SchemaMigrations is missing, we are either Fresh or upgrading from a state where we want these Defaults.
-- Actually, let's try to be smarter. We can delete strictly these default frames before inserting? No, that's destructive.
-- Let's stick to INSERT. The migration system guarantees it runs once.
-- The only risk is "Legacy DB exists, data exists, Migration Table missing".
-- In that case, we run this. It inserts duplicate frames.
-- That is a known minor risk. I can mitigate by adding a WHERE NOT EXISTS clause or by just accepting it.
-- Let's assume for now we just want to get the data in.

INSERT INTO
    SpriteFrameConfig (
        sprite_config_id,
        sprite_frame_index,
        texture_x,
        texture_y,
        texture_w,
        texture_h,
        texture_offset_x,
        texture_offset_y,
        render_w,
        render_h,
        render_offset_x,
        render_offset_y
    )
SELECT 7, 0, 48, 16, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 7
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 8, 0, 48, 80, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 8
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 9, 0, 16, 48, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 9
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 10, 0, 16, 16, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 10
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 11, 0, 16, 80, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 11
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 12, 0, 16, 112, 16, 16, 0, 0, 32, 32, 0, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 12
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 13, 0, 0, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 13
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 13, 1, 28, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 13
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 13, 2, 56, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 13
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 13, 3, 84, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 13
            AND sprite_frame_index = 3
    )
UNION ALL
SELECT 14, 0, 0, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 14
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 14, 1, 28, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 14
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 14, 2, 56, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 14
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 14, 3, 84, 0, 28, 44, -10, 0, 56, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 14
            AND sprite_frame_index = 3
    )
UNION ALL
SELECT 15, 0, 0, 0, 23, 46, -5, 0, 46, 92, -10, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 15
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 15, 1, 23, 0, 26, 46, -7, 0, 52, 92, -14, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 15
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 15, 2, 56, 0, 25, 46, -8, 0, 50, 92, -16, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 15
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 16, 0, 0, 0, 25, 46, -8, 0, 50, 92, -16, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 16
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 16, 1, 25, 0, 26, 46, -7, 0, 52, 92, -14, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 16
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 16, 2, 51, 0, 23, 46, -5, 0, 46, 92, -10, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 16
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 17, 0, 0, 0, 20, 44, -4, 0, 40, 88, -8, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 17, 1, 20, 0, 24, 44, -7, 0, 48, 88, -14, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 17, 2, 45, 0, 31, 44, -11, 0, 62, 88, -22, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 17, 3, 76, 0, 37, 44, -13, 0, 74, 88, -26, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 3
    )
UNION ALL
SELECT 17, 4, 113, 0, 34, 44, -12, 0, 68, 88, -24, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 4
    )
UNION ALL
SELECT 17, 5, 147, 0, 21, 44, -5, 0, 42, 88, -10, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 5
    )
UNION ALL
SELECT 17, 6, 168, 0, 24, 44, -6, 0, 48, 88, -12, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 6
    )
UNION ALL
SELECT 17, 7, 192, 0, 31, 44, -10, 0, 62, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 7
    )
UNION ALL
SELECT 17, 8, 223, 0, 37, 44, -14, 0, 74, 88, -28, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 8
    )
UNION ALL
SELECT 17, 9, 260, 0, 34, 44, -13, 0, 64, 88, -26, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 17
            AND sprite_frame_index = 9
    )
UNION ALL
SELECT 18, 0, 0, 0, 20, 44, -4, 0, 40, 88, -8, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 0
    )
UNION ALL
SELECT 18, 1, 20, 0, 24, 44, -7, 0, 48, 88, -14, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 1
    )
UNION ALL
SELECT 18, 2, 45, 0, 31, 44, -11, 0, 62, 88, -22, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 2
    )
UNION ALL
SELECT 18, 3, 76, 0, 37, 44, -13, 0, 74, 88, -26, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 3
    )
UNION ALL
SELECT 18, 4, 113, 0, 34, 44, -12, 0, 68, 88, -24, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 4
    )
UNION ALL
SELECT 18, 5, 147, 0, 21, 44, -5, 0, 42, 88, -10, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 5
    )
UNION ALL
SELECT 18, 6, 168, 0, 24, 44, -6, 0, 48, 88, -12, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 6
    )
UNION ALL
SELECT 18, 7, 192, 0, 31, 44, -10, 0, 62, 88, -20, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 7
    )
UNION ALL
SELECT 18, 8, 223, 0, 37, 44, -14, 0, 74, 88, -28, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 8
    )
UNION ALL
SELECT 18, 9, 260, 0, 34, 44, -13, 0, 64, 88, -26, 0
WHERE
    NOT EXISTS (
        SELECT 1
        FROM SpriteFrameConfig
        WHERE
            sprite_config_id = 18
            AND sprite_frame_index = 9
    );