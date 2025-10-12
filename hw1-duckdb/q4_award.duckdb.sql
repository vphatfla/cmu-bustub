WITH manager_award_team AS (
    SELECT  m.teamID, m.yearID, m.lgID
    FROM managers AS m
    INNER JOIN awardsmanagers AS am
        ON am.playerID = m.playerID AND am.yearID = m.yearID
), player_award_team AS (
    SELECT ap.teamID AS teamID, ap.lgID AS lgID, ap.yearID AS yearID
    FROM appearances AS ap
    INNER JOIN awardsplayers AS awp
        ON awp.playerID = ap.playerID AND awp.yearID = ap.yearID
    GROUP BY ap.teamID, ap.yearID, ap.lgID
    HAVING COUNT(DISTINCT awp.playerID) > 5
)
SELECT l.league AS league, ANY_VALUE(t.name) AS team_name, COUNT(DISTINCT ma.yearID) AS distinct_years
FROM manager_award_team AS ma
INNER JOIN player_award_team AS pa
    ON ma.teamID = pa.teamID AND ma.yearID = pa.yearID AND ma.lgID = pa.lgID
INNER JOIN teams AS t
    ON ma.teamID = t.teamID AND ma.yearID = t.yearID AND ma.lgID = t.lgID
INNER JOIN leagues AS l
    ON ma.lgID = l.lgID
WHERE l.active = 'Y'
GROUP BY l.league, t.teamID
HAVING COUNT(DISTINCT ma.yearID) > 1
ORDER BY distinct_years DESC, team_name ASC;
