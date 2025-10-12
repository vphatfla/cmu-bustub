WITH pa_players AS (
    SELECT DISTINCT cp.playerID
    FROM collegeplaying as cp
    INNER JOIN schools AS s
        ON s.schoolID = cp.schoolID
    WHERE s.state = 'PA'
)
SELECT p.nameFirst || ' (' || p.nameGiven || ') ' ||  p.nameLast as name, MAX(a.HR) as HR
FROM appearances AS a
INNER JOIN people AS p
    ON a.playerID = p.playerID
INNER JOIN pa_players as pap
    ON p.playerID = pap.playerID
GROUP BY p.nameFirst, p.nameGiven, p.nameLast
ORDER BY HR DESC, p.nameFirst ASC
LIMIT 10;
