WITH pa_players AS (
    SELECT DISTINCT cp.playerID
    FROM collegeplaying AS cp
    INNER JOIN schools AS s
        ON s.schoolID = cp.schoolID
    WHERE s.state = 'PA'
)
SELECT p.nameFirst || ' (' || p.nameGiven || ') ' || p.nameLast AS name, MAX(a.HR) AS max_hr_appearance
FROM appearances AS a
INNER JOIN people AS p
    ON a.playerID = p.playerID
INNER JOIN pa_players AS pap
    ON p.playerID = pap.playerID
GROUP BY p.nameFirst, p.nameGiven, p.nameLast
ORDER BY max_hr_appearance DESC, p.nameFirst ASC
LIMIT 10;
