-- PRQLite feature demo. Run with:
--   Get-Content examples/demo.sql | .\build\prqlite.exe
-- (The REPL starts a fresh database each launch, so this script is self-contained.)

-- 1) DDL + INSERT (emp.dept_id is a FOREIGN KEY into dept.id)
CREATE TABLE dept (id INT, dname TEXT);
CREATE TABLE emp (id INT, name TEXT, dept_id INT REFERENCES dept(id), salary INT);
INSERT INTO dept VALUES (1,'eng'),(2,'sales'),(3,'mgmt');
INSERT INTO emp VALUES (1,'Alice',1,100),(2,'Bob',1,200),(3,'Carol',2,150),(4,'Dave',2,NULL),(5,'Eve',3,300);

-- 2) SELECT + WHERE, NULL, BETWEEN, LIKE, IN
SELECT * FROM emp;
SELECT name FROM emp WHERE salary IS NULL;
SELECT name FROM emp WHERE salary BETWEEN 100 AND 200;
SELECT name FROM emp WHERE name LIKE '_a%';
SELECT name FROM emp WHERE dept_id IN (1,3);

-- 3) Aggregates + GROUP BY + HAVING
SELECT COUNT(*), SUM(salary), MIN(salary), MAX(salary) FROM emp;
SELECT dept_id, COUNT(*), AVG(salary) FROM emp GROUP BY dept_id;
SELECT dept_id FROM emp GROUP BY dept_id HAVING dept_id = 1;

-- 4) ORDER BY + LIMIT
SELECT name, salary FROM emp WHERE salary IS NOT NULL ORDER BY salary DESC LIMIT 3;

-- 5) INNER JOIN
SELECT emp.name, dept.dname FROM emp INNER JOIN dept ON emp.dept_id = dept.id ORDER BY emp.name;

-- 6) Index (used automatically for point lookups on id)
CREATE INDEX emp_id ON emp (id);
SELECT name FROM emp WHERE id = 4;

-- 7) Subqueries (scalar / IN / EXISTS)
SELECT name FROM emp WHERE dept_id = (SELECT id FROM dept WHERE dname='mgmt');
SELECT name FROM emp WHERE dept_id IN (SELECT id FROM dept WHERE dname='eng');
SELECT dname FROM dept WHERE EXISTS (SELECT id FROM emp WHERE emp.salary = 300);

-- 8) Foreign-key enforcement (BOTH of these should report an ERROR)
INSERT INTO emp VALUES (9,'Nobody',99,50);
DELETE FROM dept WHERE id = 1;

-- 9) UPDATE + DELETE
UPDATE emp SET salary = 175 WHERE id = 4;
SELECT name, salary FROM emp WHERE id = 4;
DELETE FROM emp WHERE id = 5;
SELECT COUNT(*) FROM emp;

-- 10) Transactions: ROLLBACK undoes the DELETE, COUNT returns to 4
BEGIN;
DELETE FROM emp WHERE dept_id = 2;
SELECT COUNT(*) FROM emp;
ROLLBACK;
SELECT COUNT(*) FROM emp;

-- 11) ALTER TABLE ADD COLUMN (existing rows get NULL)
ALTER TABLE dept ADD COLUMN floor INT;
SELECT * FROM dept WHERE id = 1;
