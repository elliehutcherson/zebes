-- Inserting record into TextureConfig table
INSERT INTO TextureConfig (texture_path)
VALUES ('assets/zebes/textures/samus-idle-left.png'),
       ('assets/zebes/textures/samus-idle-right.png'),
       ('assets/zebes/textures/samus-turning-left.png'),
       ('assets/zebes/textures/samus-turning-right.png'),
       ('assets/zebes/textures/samus-running-left.png'),
       ('assets/zebes/textures/samus-running-right.png'),
       ('assets/zebes/textures/samus-jumping-left.png'),
       ('assets/zebes/textures/samus-jumping-right.png'),
       ('assets/zebes/textures/sunny-back.png'),
       ('assets/zebes/textures/sunny-custom-tileset.png'),
       ('assets/zebes/textures/sunny-middle.png'),
       ('assets/zebes/textures/sunny-props.png'),
       ('assets/zebes/textures/sunny-tileset.png');

-- Inserting records into SpriteConfig table
-- type INTEGER NOT NULL
-- type_name TEXT NOT NULL
-- texture_path TEXT NOT NULL
-- ticks_per_sprite INTEGER NOT NULL
INSERT INTO SpriteConfig (type, type_name, texture_path, ticks_per_sprite)
VALUES 
(1, 'kGrass1', 'assets/zebes/textures/sunny-tileset.png', 0),
(2, 'kGrass2', 'assets/zebes/textures/sunny-tileset.png', 0),
(3, 'kGrass3', 'assets/zebes/textures/sunny-tileset.png', 0),
(4, 'kDirt1', 'assets/zebes/textures/sunny-tileset.png', 0),
(5, 'kGrassSlopeUpRight', 'assets/zebes/textures/sunny-tileset.png', 0),
(6, 'kGrass1Left', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(7, 'kGrass1Right', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(8, 'kGrass1Down', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(9, 'kGrassCornerUpLeft', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(10, 'kGrassCornerUpRight', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(11, 'kGrassCornerDownLeft', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(12, 'kGrassCornerDownRight', 'assets/zebes/textures/sunny-custom-tileset.png', 0),
(100, 'kSamusIdleLeft', 'assets/zebes/textures/samus-idle-left.png', 10),
(101, 'kSamusIdleRight', 'assets/zebes/textures/samus-idle-right.png', 10),
(102, 'kSamusTurningLeft', 'assets/zebes/textures/samus-turning-left.png', 2),
(103, 'kSamusTurningRight', 'assets/zebes/textures/samus-turning-right.png', 2),
(104, 'kSamusRunningLeft', 'assets/zebes/textures/samus-running-left.png', 2),
(105, 'kSamusRunningRight', 'assets/zebes/textures/samus-running-right.png', 2);
-- (106, 'kSamusJumpingLeft', 'assets/zebes/textures/samus-jumping-left.png', 2);
-- (107, 'kSamusJumpingRight', 'assets/zebes/textures/samus-jumping-right.png', 2);


-- Inserting records into SubSpriteConfig table
INSERT INTO SubSpriteConfig (
  sprite_config_id, 
  sub_sprite_index, 
  texture_x, 
  texture_y, 
  texture_w, 
  texture_h, 
  texture_offset_x,
  texture_offset_y,
  render_w, 
  render_h, 
  render_offset_x, 
  render_offset_y)
VALUES 
(/*sprite_config_id=*/1, /*sub_sprite_index=*/0, /*texture_x=*/16, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),       --kGrass1
(/*sprite_config_id=*/2, /*sub_sprite_index=*/0, /*texture_x=*/48, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),       --kGrass2
(/*sprite_config_id=*/3, /*sub_sprite_index=*/0, /*texture_x=*/80, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),       --kGrass3
(/*sprite_config_id=*/4, /*sub_sprite_index=*/0, /*texture_x=*/112, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kDirt1
(/*sprite_config_id=*/5, /*sub_sprite_index=*/0, /*texture_x=*/304, /*texture_y=*/16, /*texture_w=*/32, /*texture_h=*/32, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/64, /*render_h=*/64, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kGrassSlopeUpRight
(/*sprite_config_id=*/6, /*sub_sprite_index=*/0, /*texture_x=*/48, /*texture_y=*/48, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),       --kGrass1Left
(/*sprite_config_id=*/7, /*sub_sprite_index=*/0, /*texture_x=*/48, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),       --kGrass1Right
(/*sprite_config_id=*/8, /*sub_sprite_index=*/0, /*texture_x=*/48, /*texture_y=*/80, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kGrass1Down
(/*sprite_config_id=*/9, /*sub_sprite_index=*/0, /*texture_x=*/16, /*texture_y=*/48, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kGrassCornerUpLeft
(/*sprite_config_id=*/10, /*sub_sprite_index=*/0, /*texture_x=*/16, /*texture_y=*/16, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kGrassCornerUpRight
(/*sprite_config_id=*/11, /*sub_sprite_index=*/0, /*texture_x=*/16, /*texture_y=*/80, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),      --kGrassCornerDownLeft
(/*sprite_config_id=*/12, /*sub_sprite_index=*/0, /*texture_x=*/16, /*texture_y=*/112, /*texture_w=*/16, /*texture_h=*/16, /*texture_offset_x=*/0, /*texture_offset_y=*/0, /*render_w=*/32, /*render_h=*/32, /*render_offset_x=*/0, /*render_offset_y=*/0),     --kGrassCornerDownRight

(/*sprite_config_id=*/13, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),    --kSamusIdleLeft 0
(/*sprite_config_id=*/13, /*sub_sprite_index=*/1, /*texture_x=*/28, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 1
(/*sprite_config_id=*/13, /*sub_sprite_index=*/2, /*texture_x=*/56, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 2
(/*sprite_config_id=*/13, /*sub_sprite_index=*/3, /*texture_x=*/84, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 3

(/*sprite_config_id=*/14, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),    --kSamusIdleLeft 0
(/*sprite_config_id=*/14, /*sub_sprite_index=*/1, /*texture_x=*/28, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 1
(/*sprite_config_id=*/14, /*sub_sprite_index=*/2, /*texture_x=*/56, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 2
(/*sprite_config_id=*/14, /*sub_sprite_index=*/3, /*texture_x=*/84, /*texture_y=*/0, /*texture_w=*/28, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/56, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),   --kSamusIdleLeft 3

(/*sprite_config_id=*/15, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/23, /*texture_h=*/46, /*texture_offset_x=*/-5, /*texture_offset_y=*/0, /*render_w=*/46, /*render_h=*/92, /*render_offset_x=*/-10, /*render_offset_y=*/0),     --kSamusTurningLeft 0
(/*sprite_config_id=*/15, /*sub_sprite_index=*/1, /*texture_x=*/23, /*texture_y=*/0, /*texture_w=*/26, /*texture_h=*/46, /*texture_offset_x=*/-7, /*texture_offset_y=*/0, /*render_w=*/52, /*render_h=*/92, /*render_offset_x=*/-14, /*render_offset_y=*/0),    --kSamusTurningLeft 1
(/*sprite_config_id=*/15, /*sub_sprite_index=*/2, /*texture_x=*/56, /*texture_y=*/0, /*texture_w=*/25, /*texture_h=*/46, /*texture_offset_x=*/-8, /*texture_offset_y=*/0, /*render_w=*/50, /*render_h=*/92, /*render_offset_x=*/-16, /*render_offset_y=*/0),    --kSamusTurningLeft 2

(/*sprite_config_id=*/16, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/25, /*texture_h=*/46, /*texture_offset_x=*/-8, /*texture_offset_y=*/0, /*render_w=*/50, /*render_h=*/92, /*render_offset_x=*/-16, /*render_offset_y=*/0),     --kSamusTurningRight 0
(/*sprite_config_id=*/16, /*sub_sprite_index=*/1, /*texture_x=*/25, /*texture_y=*/0, /*texture_w=*/26, /*texture_h=*/46, /*texture_offset_x=*/-7, /*texture_offset_y=*/0, /*render_w=*/52, /*render_h=*/92, /*render_offset_x=*/-14, /*render_offset_y=*/0),    --kSamusTurningRight 1
(/*sprite_config_id=*/16, /*sub_sprite_index=*/2, /*texture_x=*/51, /*texture_y=*/0, /*texture_w=*/23, /*texture_h=*/46, /*texture_offset_x=*/-5, /*texture_offset_y=*/0, /*render_w=*/46, /*render_h=*/92, /*render_offset_x=*/-10, /*render_offset_y=*/0),    --kSamusTurningRight 2

(/*sprite_config_id=*/17, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/20, /*texture_h=*/44, /*texture_offset_x=*/-4, /*texture_offset_y=*/0, /*render_w=*/40, /*render_h=*/88, /*render_offset_x=*/-8, /*render_offset_y=*/0),      --kSamusRunningLeft 0
(/*sprite_config_id=*/17, /*sub_sprite_index=*/1, /*texture_x=*/20, /*texture_y=*/0, /*texture_w=*/24, /*texture_h=*/44, /*texture_offset_x=*/-7, /*texture_offset_y=*/0, /*render_w=*/48, /*render_h=*/88, /*render_offset_x=*/-14, /*render_offset_y=*/0),    --kSamusRunningLeft 1
(/*sprite_config_id=*/17, /*sub_sprite_index=*/2, /*texture_x=*/45, /*texture_y=*/0, /*texture_w=*/31, /*texture_h=*/44, /*texture_offset_x=*/-11, /*texture_offset_y=*/0, /*render_w=*/62, /*render_h=*/88, /*render_offset_x=*/-22, /*render_offset_y=*/0),   --kSamusRunningLeft 2
(/*sprite_config_id=*/17, /*sub_sprite_index=*/3, /*texture_x=*/76, /*texture_y=*/0, /*texture_w=*/37, /*texture_h=*/44, /*texture_offset_x=*/-13, /*texture_offset_y=*/0, /*render_w=*/74, /*render_h=*/88, /*render_offset_x=*/-26, /*render_offset_y=*/0),   --kSamusRunningLeft 3
(/*sprite_config_id=*/17, /*sub_sprite_index=*/4, /*texture_x=*/113, /*texture_y=*/0, /*texture_w=*/34, /*texture_h=*/44, /*texture_offset_x=*/-12, /*texture_offset_y=*/0, /*render_w=*/68, /*render_h=*/88, /*render_offset_x=*/-24, /*render_offset_y=*/0),  --kSamusRunningLeft 4
(/*sprite_config_id=*/17, /*sub_sprite_index=*/5, /*texture_x=*/147, /*texture_y=*/0, /*texture_w=*/21, /*texture_h=*/44, /*texture_offset_x=*/-5, /*texture_offset_y=*/0, /*render_w=*/42, /*render_h=*/88, /*render_offset_x=*/-10, /*render_offset_y=*/0),   --kSamusRunningLeft 5
(/*sprite_config_id=*/17, /*sub_sprite_index=*/6, /*texture_x=*/168, /*texture_y=*/0, /*texture_w=*/24, /*texture_h=*/44, /*texture_offset_x=*/-6, /*texture_offset_y=*/0, /*render_w=*/48, /*render_h=*/88, /*render_offset_x=*/-12, /*render_offset_y=*/0),   --kSamusRunningLeft 6
(/*sprite_config_id=*/17, /*sub_sprite_index=*/7, /*texture_x=*/192, /*texture_y=*/0, /*texture_w=*/31, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/62, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),  --kSamusRunningLeft 7
(/*sprite_config_id=*/17, /*sub_sprite_index=*/8, /*texture_x=*/223, /*texture_y=*/0, /*texture_w=*/37, /*texture_h=*/44, /*texture_offset_x=*/-14, /*texture_offset_y=*/0, /*render_w=*/74, /*render_h=*/88, /*render_offset_x=*/-28, /*render_offset_y=*/0),  --kSamusRunningLeft 8
(/*sprite_config_id=*/17, /*sub_sprite_index=*/9, /*texture_x=*/260, /*texture_y=*/0, /*texture_w=*/34, /*texture_h=*/44, /*texture_offset_x=*/-13, /*texture_offset_y=*/0, /*render_w=*/64, /*render_h=*/88, /*render_offset_x=*/-26, /*render_offset_y=*/0),  --kSamusRunningLeft 9

(/*sprite_config_id=*/18, /*sub_sprite_index=*/0, /*texture_x=*/0, /*texture_y=*/0, /*texture_w=*/20, /*texture_h=*/44, /*texture_offset_x=*/-4, /*texture_offset_y=*/0, /*render_w=*/40, /*render_h=*/88, /*render_offset_x=*/-8, /*render_offset_y=*/0),      --kSamusRunningRight 0
(/*sprite_config_id=*/18, /*sub_sprite_index=*/1, /*texture_x=*/20, /*texture_y=*/0, /*texture_w=*/24, /*texture_h=*/44, /*texture_offset_x=*/-7, /*texture_offset_y=*/0, /*render_w=*/48, /*render_h=*/88, /*render_offset_x=*/-14, /*render_offset_y=*/0),    --kSamusRunningRight 1
(/*sprite_config_id=*/18, /*sub_sprite_index=*/2, /*texture_x=*/45, /*texture_y=*/0, /*texture_w=*/31, /*texture_h=*/44, /*texture_offset_x=*/-11, /*texture_offset_y=*/0, /*render_w=*/62, /*render_h=*/88, /*render_offset_x=*/-22, /*render_offset_y=*/0),   --kSamusRunningRight 2
(/*sprite_config_id=*/18, /*sub_sprite_index=*/3, /*texture_x=*/76, /*texture_y=*/0, /*texture_w=*/37, /*texture_h=*/44, /*texture_offset_x=*/-13, /*texture_offset_y=*/0, /*render_w=*/74, /*render_h=*/88, /*render_offset_x=*/-26, /*render_offset_y=*/0),   --kSamusRunningRight 3
(/*sprite_config_id=*/18, /*sub_sprite_index=*/4, /*texture_x=*/113, /*texture_y=*/0, /*texture_w=*/34, /*texture_h=*/44, /*texture_offset_x=*/-12, /*texture_offset_y=*/0, /*render_w=*/68, /*render_h=*/88, /*render_offset_x=*/-24, /*render_offset_y=*/0),  --kSamusRunningRight 4
(/*sprite_config_id=*/18, /*sub_sprite_index=*/5, /*texture_x=*/147, /*texture_y=*/0, /*texture_w=*/21, /*texture_h=*/44, /*texture_offset_x=*/-5, /*texture_offset_y=*/0, /*render_w=*/42, /*render_h=*/88, /*render_offset_x=*/-10, /*render_offset_y=*/0),   --kSamusRunningRight 5
(/*sprite_config_id=*/18, /*sub_sprite_index=*/6, /*texture_x=*/168, /*texture_y=*/0, /*texture_w=*/24, /*texture_h=*/44, /*texture_offset_x=*/-6, /*texture_offset_y=*/0, /*render_w=*/48, /*render_h=*/88, /*render_offset_x=*/-12, /*render_offset_y=*/0),   --kSamusRunningRight 6
(/*sprite_config_id=*/18, /*sub_sprite_index=*/7, /*texture_x=*/192, /*texture_y=*/0, /*texture_w=*/31, /*texture_h=*/44, /*texture_offset_x=*/-10, /*texture_offset_y=*/0, /*render_w=*/62, /*render_h=*/88, /*render_offset_x=*/-20, /*render_offset_y=*/0),  --kSamusRunningRight 7
(/*sprite_config_id=*/18, /*sub_sprite_index=*/8, /*texture_x=*/223, /*texture_y=*/0, /*texture_w=*/37, /*texture_h=*/44, /*texture_offset_x=*/-14, /*texture_offset_y=*/0, /*render_w=*/74, /*render_h=*/88, /*render_offset_x=*/-28, /*render_offset_y=*/0),  --kSamusRunningRight 8
(/*sprite_config_id=*/18, /*sub_sprite_index=*/9, /*texture_x=*/260, /*texture_y=*/0, /*texture_w=*/34, /*texture_h=*/44, /*texture_offset_x=*/-13, /*texture_offset_y=*/0, /*render_w=*/64, /*render_h=*/88, /*render_offset_x=*/-26, /*render_offset_y=*/0);  --kSamusRunningRight 9
