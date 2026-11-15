CREATE TABLE groups (
    id INTEGER PRIMARY KEY CHECK (id > 0),
    name TEXT NOT NULL
);
INSERT INTO groups (id, name) VALUES
    (1, 'Group 1'), (2, 'Group 2'), (3, 'Group 3'), (4, 'Group 4');
