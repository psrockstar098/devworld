-- Mission 4: list every resident's name.
CREATE TABLE residents(name TEXT, job TEXT);
INSERT INTO residents VALUES ('Mara','mechanic'),('Ben','keeper'),('Junie','guide');

SELECT name FROM residents;
