/*
Copyright (C) 2009-2010 Chasseur de bots

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

enum RoundState {
	RoundState_None,
	RoundState_Pre,
	RoundState_Round,
	RoundState_Finished,
	RoundState_Post
}

const uint RoundState_None = 0;
const uint RoundState_Pre = 1;
const uint RoundState_Round = 2;
const uint RoundState_Finished = 3;
const uint RoundState_Post = 4;

//eRoundStates roundState = RoundState_None; FIXME enum
uint roundState = RoundState_None;

bool roundCheckEndTime; // you can check if roundStateEndTime == 0 but roundStateEndTime can overflow
int64 roundStartTime;    // roundStartTime because only spawn protection uses it
int64 roundStateEndTime; // XXX: this should be fixed in all gts

int roundCountDown;

uint roundCount;

int attackingTeam;
int defendingTeam;

bool attackersHurried;
bool defendersHurried;
bool was1vx;

void playerKilled( Entity @victim, Entity @attacker, Entity @inflictor ) {
	// this happens if you kill a corpse or something...
	if( @victim == null || @victim.client == null ) {
		return;
	}

	// ch :
	cPlayer @pVictim = @playerFromClient( @victim.client );

	if( match.getState() != MATCH_STATE_PLAYTIME )
		return;

	if( roundState >= RoundState_Finished )
		return;

	if( @victim == @bombCarrier ) {
		bombDrop( BombDrop_Killed );
	}

	if( @attacker != null && @attacker.client != null && attacker.team != victim.team ) {
		cPlayer @player = @playerFromClient( @attacker.client );

		player.killsThisRound++;

		int required_for_ace = attacker.team == TEAM_ALPHA ? match.betaPlayersTotal : match.alphaPlayersTotal;
		if( required_for_ace >= 3 && player.killsThisRound == required_for_ace ) {
			G_AnnouncerSound( null, sndAce, GS_MAX_TEAMS, false, null );
		}
	}

	// check if the player's team is now dead
	checkPlayersAlive( victim.team );
}

void checkPlayersAlive( int team ) {
	uint alive = playersAliveOnTeam( team );

	if( alive == 0 ) {
		if( team == attackingTeam ) {
			if( bombState != BombState_Planted ) {
				roundWonBy( defendingTeam );
			}
		}
		else {
			roundWonBy( attackingTeam );
		}

		return;
	}

	int other = otherTeam( team );
	uint aliveOther = playersAliveOnTeam( other );

	if( alive == 1 ) {
		if( aliveOther == 1 ) {
			if( was1vx ) {
				G_AnnouncerSound( null, snd1v1, GS_MAX_TEAMS, false, null );
			}
		}
		else if( aliveOther >= 3 ) {
			G_AnnouncerSound( null, snd1vx, team, false, null );
			G_AnnouncerSound( null, sndxv1, other, false, null );
			was1vx = true;
		}
	}
}

void setTeams() {
	uint limit = cvarScoreLimit.integer;

	if( limit == 0 || roundCount > ( limit - 1 ) * 2 ) {
		// the first overtime round is ( limit - 1 ) * 2 + 1
		// which is of the form 2n + 1 so is odd
		bool odd = roundCount % 2 == 1;
		attackingTeam = odd ? INITIAL_ATTACKERS : INITIAL_DEFENDERS;
		defendingTeam = odd ? INITIAL_DEFENDERS : INITIAL_ATTACKERS;
		return;
	}

	bool first_half = roundCount < limit;
	attackingTeam = first_half ? INITIAL_ATTACKERS : INITIAL_DEFENDERS;
	defendingTeam = first_half ? INITIAL_DEFENDERS : INITIAL_ATTACKERS;
}

void newGame() {
	roundCount = 1;
	setTeams();

	for( int t = TEAM_PLAYERS; t < GS_MAX_TEAMS; t++ ) {
		gametype.setTeamSpawnsystem( t, SPAWNSYSTEM_HOLD, 0, 0, true );

		Team @team = @G_GetTeam( t );

		for( int i = 0; @team.ent( i ) != null; i++ ) {
			team.ent( i ).client.stats.clear();
		}
	}

	newRound();
}

// this function doesn't care how the round was won
void roundWonBy( int winner ) {
	int loser = winner == attackingTeam ? defendingTeam : attackingTeam;

	G_CenterPrintMsg( null, S_COLOR_CYAN + ( winner == attackingTeam ? "OFFENSE WINS!" : "DEFENSE WINS!" ) );

	uint64 sound = Hash64( "sounds/announcer/bomb/score_team0" + random_uniform( 1, 3 ) );
	G_AnnouncerSound( null, sound, winner, true, null );

	sound = Hash64( "sounds/announcer/bomb/score_enemy0" + random_uniform( 1, 3 ) );
	G_AnnouncerSound( null, sound, loser, true, null );

	Team @teamWinner = @G_GetTeam( winner );

	if( winner == TEAM_ALPHA ) {
		match.alphaScore++;
	}
	else {
		match.betaScore++;
	}

	for( int i = 0; @teamWinner.ent( i ) != null; i++ ) {
		Entity @ent = @teamWinner.ent( i );

		if( !ent.isGhosting() ) {
			ent.client.addAward( S_COLOR_GREEN + "Victory!" );
		}
	}

	roundNewState( RoundState_Finished );
}

void newRound() {
	roundNewState( RoundState_Pre );
}

void endGame() {
	roundNewState( RoundState_None );

	GENERIC_SetUpEndMatch();
}

bool scoreLimitHit() {
	return match.scoreLimitHit() && abs( int( match.alphaScore ) - int( match.betaScore ) ) > 1;
}

void setRoundType() {
	RoundType type = RoundType_Normal;

	uint limit = cvarScoreLimit.integer;

	bool match_point = match.alphaScore == limit - 1 || match.betaScore == limit - 1;
	bool overtime = roundCount > ( limit - 1 ) * 2;

	if( overtime ) {
		type = match.alphaScore == match.betaScore ? RoundType_Overtime : RoundType_OvertimeMatchPoint;
	}
	else if( match_point ) {
		type = RoundType_MatchPoint;
	}

	for( int t = TEAM_ALPHA; t < GS_MAX_TEAMS; t++ ) {
		Team @team = @G_GetTeam( t );

		for( int i = 0; @team.ent( i ) != null; i++ ) {
			Client @client = @team.ent( i ).client;
			match.roundType = type;
		}
	}
}

//void roundNewState( eRoundStates state ) FIXME enum
void roundNewState( uint state ) {
	if( state > RoundState_Post ) {
		state = RoundState_Pre;
	}

	roundState = state;

	switch( roundState ) {
		case RoundState_None:
			break;

		case RoundState_Pre:
			roundCountDown = COUNTDOWN_MAX;

			setTeams();

			roundCheckEndTime = true;
			roundStateEndTime = levelTime + 5000;

			gametype.shootingDisabled = true;
			gametype.removeInactivePlayers = false;

			attackersHurried = false;
			defendersHurried = false;
			was1vx = false;

			resetBombSites();

			G_ResetLevel();

			resetBomb();

			resetKillCounters();
			respawnAllPlayers();
			disableMovement();
			setRoundType();

			bombGiveToRandom();

			break;

		case RoundState_Round:
			roundCheckEndTime = true;
			roundStartTime = levelTime;
			roundStateEndTime = levelTime + int( cvarRoundTime.value * 1000.0f );

			gametype.shootingDisabled = false;
			gametype.removeInactivePlayers = true;

			enableMovement();

			announce( Announcement_RoundStarted );

			break;

		case RoundState_Finished:
			roundCheckEndTime = true;
			roundStateEndTime = levelTime + 1500; // magic numbers are awesome

			gametype.shootingDisabled = false;

			break;

		case RoundState_Post:
			if( scoreLimitHit() ) {
				match.launchState( match.getState() + 1 );

				return;
			}

			roundCheckEndTime = true;
			roundStateEndTime = levelTime + 3000; // XXX: old bomb did +5s but i don't see the point

			roundCount++;
			break;
	}
}

int last_time = 0;

void roundThink() {
	if( roundState == RoundState_None ) {
		return;
	}

	if( roundState == RoundState_Pre ) {
		int remainingSeconds = int( ( roundStateEndTime - levelTime ) * 0.001f ) + 1;

		if( remainingSeconds < 0 ) {
			remainingSeconds = 0;
		}

		if( remainingSeconds < roundCountDown ) {
			roundCountDown = remainingSeconds;

			if( roundCountDown == COUNTDOWN_MAX ) {
				uint64 sound = Hash64( "sounds/announcer/countdown/ready0" + random_uniform( 1, 3 ) );
				G_AnnouncerSound( null, sound, GS_MAX_TEAMS, false, null );
			}
			else {
				if( roundCountDown < 4 ) {
					uint64 sound = Hash64( "sounds/announcer/countdown/" + roundCountDown + "_0" + random_uniform( 1, 3 ) );
					G_AnnouncerSound( null, sound, GS_MAX_TEAMS, false, null );
				}
			}
		}

		match.alphaPlayersTotal = playersAliveOnTeam( TEAM_ALPHA );
		match.betaPlayersTotal = playersAliveOnTeam( TEAM_BETA );

		last_time = roundStateEndTime - levelTime + int( cvarRoundTime.value * 1000.0f );
		match.setClockOverride( last_time );
	}

	// i suppose the following blocks could be merged to save an if or 2
	if( roundCheckEndTime && levelTime > roundStateEndTime ) {
		if( roundState == RoundState_Round ) {
			if( bombState != BombState_Planted ) {
				roundWonBy( defendingTeam );
				last_time = 1; // kinda hacky, this shows at 0:00

				// put this after roundWonBy or it gets overwritten
				G_CenterPrintMsg( null, S_COLOR_RED + "Timelimit hit!" );

				return;
			}
		}
		else {
			//roundNewState( eRoundStates( roundState + 1 ) ); FIXME enum
			roundNewState( roundState + 1 );

			return;
		}
	}

	if( roundState == RoundState_Round ) {
		// monitor the bomb's health
		if( @bombModel == null || bombModel.classname != "bomb" ) {
			bombModelCreate();

			roundWonBy( defendingTeam );

			// put this after roundWonBy or it gets overwritten
			G_CenterPrintMsg( null, S_COLOR_RED + "The attacking team has lost the bomb!!!" );

			return;
		}

		// warn defs if bomb will explode soon
		// warn offs if the round ends soon and they haven't planted
		if( bombState == BombState_Planted ) {
			last_time = -1;

			if( !defendersHurried && levelTime + BOMB_HURRYUP_TIME >= bombActionTime ) {
				G_AnnouncerSound( null, sndHurry, defendingTeam, true, null );
				defendersHurried = true;
			}
		}
		else {
			last_time = roundStateEndTime - levelTime;

			if( !attackersHurried && levelTime + BOMB_HURRYUP_TIME >= roundStateEndTime ) {
				G_AnnouncerSound( null, sndHurry, attackingTeam, true, null );
				attackersHurried = true;
			}
		}

		match.setClockOverride( last_time );

		bombThink();
	}
	else {
		match.setClockOverride( last_time );

		if( roundState > RoundState_Round ) {
			bombPostRoundThink();
		}
	}
}

uint playersAliveOnTeam( int teamNum ) {
	uint alive = 0;

	Team @team = @G_GetTeam( teamNum );

	for( int i = 0; @team.ent( i ) != null; i++ ) {
		Entity @ent = @team.ent( i );

		// check health incase they died this frame
		if( !ent.isGhosting() && ent.health > 0 ) {
			alive++;
		}
	}

	return alive;
}

// loops through players on teamNum and returns Entity of first alive player
// this is only used when playersAliveOnTeam returns 1
// hence the assert
Client @firstAliveOnTeam( int teamNum ) {
	Team @team = @G_GetTeam( teamNum );

	for( int i = 0; @team.ent( i ) != null; i++ ) {
		Entity @ent = @team.ent( i );

		// check health incase they died this frame

		if( !ent.isGhosting() && ent.health > 0 ) {
			return @ent.client;
		}
	}

	assert( false, "round.as firstAliveOnTeam: found nobody" );

	return null; // shut up compiler
}

void respawnAllPlayers() {
	for( int t = TEAM_PLAYERS; t < GS_MAX_TEAMS; t++ ) {
		Team @team = @G_GetTeam( t );

		for( int i = 0; @team.ent( i ) != null; i++ ) {
			team.ent( i ).client.respawn( false );
		}
	}
}

int otherTeam( int team ) {
	return team == attackingTeam ? defendingTeam : attackingTeam;
}

void enableMovement() {
	for( int t = TEAM_PLAYERS; t < GS_MAX_TEAMS; t++ ) {
		Team @team = @G_GetTeam( t );

		for( int i = 0; @team.ent( i ) != null; i++ ) {
			Client @client = @team.ent( i ).client;

			client.pmoveMaxSpeed = -1;
			client.pmoveDashSpeed = -1;
			client.pmoveFeatures = client.pmoveFeatures | PMFEAT_JUMP | PMFEAT_SPECIAL;
		}
	}
}

void disableMovementFor( Client @client ) {
	client.pmoveMaxSpeed = 100;
	client.pmoveDashSpeed = 0;
	client.pmoveFeatures = client.pmoveFeatures & ~( PMFEAT_JUMP | PMFEAT_SPECIAL );
}

void disableMovement() {
	for( int t = TEAM_PLAYERS; t < GS_MAX_TEAMS; t++ ) {
		Team @team = @G_GetTeam( t );

		for( int i = 0; @team.ent( i ) != null; i++ ) {
			Client @client = @team.ent( i ).client;

			disableMovementFor( @client );
		}
	}
}
