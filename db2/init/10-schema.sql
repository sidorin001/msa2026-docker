CREATE TABLE items (
    id INTEGER PRIMARY KEY CHECK (id > 0),
    group_id INTEGER NOT NULL CHECK (group_id > 0),
    name TEXT NOT NULL
);
CREATE INDEX items_group_id_idx ON items(group_id);
INSERT INTO items (id, group_id, name) VALUES
    (1, 1, 'Item 1'), (2, 1, 'Item 2'),
    (3, 2, 'Item 3'), (4, 2, 'Item 4'), (5, 2, 'Item 5'),
    (6, 3, 'Item 6'), (7, 3, 'Item 7'), (8, 3, 'Item 8'),
    (9, 3, 'Item 9'), (10, 3, 'Item 10');
