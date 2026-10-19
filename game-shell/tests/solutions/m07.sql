-- Mission 7: names with visits > 10, alphabetical.
CREATE TABLE residents(name TEXT, visits INTEGER);
INSERT INTO residents VALUES ('Mara',42),('Ben',15),('Junie',8),('Theo',23);

SELECT name FROM residents WHERE visits > 10 ORDER BY name;
