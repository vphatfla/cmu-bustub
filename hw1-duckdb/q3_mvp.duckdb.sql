WITH player_gold_awards AS (
    SELECT ap.playerID as playerID, COUNT(DISTINCT ap.yearID) as award_years
    FROM awardsplayers as ap
    INNER JOIN leagues as l
        ON ap.lgID = l.lgID
    WHERE ap.awardID='Gold Glove' AND ap.yearID > 1999 AND l.active='Y'
    GROUP BY ap.playerID
),
team_batting AS (
    SELECT teamID, yearID, AVG(G_batting) as avg_batting
    FROM appearances
    WHERE yearID > 1999
    GROUP BY teamID, yearID
),
pairs AS (
    SELECT a.playerID, a.teamID
    FROM appearances AS a
    INNER JOIN team_batting AS tb 
        ON a.teamID = tb.teamID AND a.yearID = tb.yearID
    WHERE a.G_batting > tab.avg_batting
)
SElECT p.nameGiven, player_batting.teamID, player_gold_awards.award_years
FROM 
