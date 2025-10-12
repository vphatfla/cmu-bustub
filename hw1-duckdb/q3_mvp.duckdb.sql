WITH player_gold_awards AS (
    SELECT ap.playerID AS playerID, COUNT(DISTINCT ap.yearID) AS award_years
    FROM awardsplayers AS ap
    INNER JOIN leagues AS l
        ON ap.lgID = l.lgID
    WHERE ap.awardID='Gold Glove' AND ap.yearID > 1999 AND l.active='Y'
    GROUP BY ap.playerID
)
SELECT p.nameGiven, a.teamID, pg.award_years AS distinct_years
FROM appearances AS a
INNER JOIN player_gold_awards AS pg 
    ON a.playerID = pg.playerID
INNER JOIN people AS p
    ON a.playerID = p.playerID
WHERE a.yearID > 1999 AND a.G_batting > (
    SELECT AVG(G_batting)
    FROM appearances AS a2
    WHERE a2.teamID = a.teamID AND a2.yearID = a.yearID
)
GROUP BY p.nameGiven, a.teamID, pg.award_years
ORDER BY pg.award_years DESC, p.nameGiven ASC
LIMIT 10;
