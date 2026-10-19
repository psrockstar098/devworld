-- Mission 10: count residents per job, alphabetical.
CREATE TABLE residents(name TEXT, job TEXT);
INSERT INTO residents VALUES
  ('Mara','mechanic'),('Ben','keeper'),('Junie','guide'),
  ('Rafi','keeper'),('Theo','regular');

SELECT job, COUNT(*) FROM residents GROUP BY job ORDER BY job;
