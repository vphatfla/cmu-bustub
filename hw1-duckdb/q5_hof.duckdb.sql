WITH hof AS (
    SELECT pp.nameFirst || ' (' || pp.nameGiven || ') ' || pp.nameLast AS c_name, halloffame.playerID
    FROM halloffame
    INNER JOIN people AS pp
        ON pp.playerID = halloffame.playerID
    WHERE inducted = 'Y'
    ORDER BY c_name ASC
)
SELECT hof.c_name AS hof_player_name, tmate.c_name AS earliest_teammate_name, tyear.earliest_year AS earliest_teammate_year
FROM hof
CROSS JOIN LATERAL (
    SELECT MIN(yearID) AS earliest_year, teamID, playerID
    FROM appearances
    WHERE playerID = hof.playerID
    GROUP BY playerID, teamID
    ORDER BY earliest_year
    LIMIT 1
) AS tyear
CROSS JOIN LATERAL (
    SELECT pp.nameFirst || ' (' || pp.nameGiven || ') ' || pp.nameLast AS c_name
    FROM appearances AS ap
    INNER JOIN people AS pp
        ON pp.playerID = ap.playerID
    WHERE ap.playerID != hof.playerID AND ap.yearID = tyear.earliest_year AND ap.teamID = tyear.teamID
    ORDER BY c_name ASC
    LIMIT 1
) AS tmate
ORDER BY hof_player_name ASC
LIMIT 10;
