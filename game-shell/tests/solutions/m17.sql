-- Mission 17: experiments with at least one successful run.
CREATE TABLE experiments(id INTEGER, name TEXT);
CREATE TABLE runs(id INTEGER, experiment_id INTEGER, result TEXT);
INSERT INTO experiments VALUES (1,'cold-start'),(2,'warm-cache'),(3,'orphan');
INSERT INTO runs VALUES
  (1,1,'ok'),(2,1,'fail'),(3,1,'ok'),
  (4,2,'ok'),(5,3,'fail'),(6,3,'fail');

SELECT e.name AS name, COUNT(*) AS successes
FROM experiments e
JOIN runs r ON r.experiment_id = e.id
WHERE r.result = 'ok'
GROUP BY e.id
ORDER BY successes DESC, e.name ASC;
