-- Migration 003: Add frames_per_cycle to SpriteFrameConfig
ALTER TABLE SpriteFrameConfig
ADD COLUMN frames_per_cycle INTEGER DEFAULT 1;