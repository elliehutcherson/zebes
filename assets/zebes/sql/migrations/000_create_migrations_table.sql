-- Migration 000: Create SchemaMigrations Table
CREATE TABLE IF NOT EXISTS SchemaMigrations (
    version INTEGER PRIMARY KEY,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);