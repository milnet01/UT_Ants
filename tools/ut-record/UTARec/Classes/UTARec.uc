//=============================================================================
// UTARec -- UT_Ants UTA-0456. Sequences the scripted runs UTARec.ini lists and
// logs the player, tick by tick, for ut-record. UTARecPawn calls InputTick
// from its PlayerInput, so a run's clock is the player's own input tick.
//
// A run is one line: "<name> <x> <y> <z> <yaw>;<t> <keys>;...;end <t>",
// times in game seconds from the run's first tick, keys a string of
// F B L R J D W, or "-" for none. ut-record writes the lines.
//
// Log lines (format 1; docs/specs/UTA-0456-movement-recorder.md section 4.5):
//   UTAREC 1 BEGIN run=<name> map=<map> rate=<cap> pose=<x>,<y>,<z>,<yaw>
//   UTAREC 1 PAWN class=<class> <key>=<value> ...
//   UTAREC 1 T <tick> <time> <dt> <keys> <fwd> <strafe> <x> <y> <z> <vx> <vy> <vz> <ax> <ay> <az> <physics> <dodge> <anim> <base>
//   UTAREC 1 END run=<name> ticks=<n>
//   UTAREC SKIP <name>, UTAREC START <x> <y> <z> <yaw>, UTAREC DONE
//=============================================================================
class UTARec expands Mutator config(UTARec);

const MaxSteps = 32; // ut-record.py refuses more steps a run; change both

var config string Runs[64]; // ut-record.py refuses more than 64; change both
var config string MapName;
var config float Rate;      // the frame-rate cap this launch runs at
var config float K;         // what a held key leaves in its axis (section 4.6)
var config bool bCalibrate; // log the axes as received; run no script
var config float CalibrateSeconds;
var config float SettleSeconds; // still and walking this long before a run
var config float SkipSeconds;   // a pose that never settles is skipped

var int Index;      // the run in Runs
var int Phase;      // 0 between runs, 1 settling, 2 running
var float Waited, Still, StartTime;
var int Tick;
var string RunName;
var vector Pose;
var int PoseYaw;
var float StepTime[32];
var string StepKeys[32];
var int Steps;
var float EndTime;
var bool bStartsLogged;
var bool bRowDue; // InputTick returned a run's keys; LogTick writes their row

// The next space-separated field of S, which loses it.
function string NextField(out string S)
{
	local int At;
	local string Field;

	while (Left(S, 1) == " ")
		S = Mid(S, 1);
	At = InStr(S, " ");
	if (At < 0)
	{
		Field = S;
		S = "";
	}
	else
	{
		Field = Left(S, At);
		S = Mid(S, At + 1);
	}
	return Field;
}

// The next ';'-separated part of S, which loses it.
function string NextPart(out string S)
{
	local int At;
	local string Part;

	At = InStr(S, ";");
	if (At < 0)
	{
		Part = S;
		S = "";
	}
	else
	{
		Part = Left(S, At);
		S = Mid(S, At + 1);
	}
	return Part;
}

function bool ParseRun(string Line)
{
	local string Part, Head;

	Head = NextPart(Line);
	RunName = NextField(Head);
	Pose.X = float(NextField(Head));
	Pose.Y = float(NextField(Head));
	Pose.Z = float(NextField(Head));
	PoseYaw = int(NextField(Head));
	Steps = 0;
	EndTime = -1;
	while (Line != "")
	{
		Part = NextPart(Line);
		Head = NextField(Part);
		if (Head == "end")
			EndTime = float(NextField(Part));
		else if (Steps < MaxSteps)
		{
			StepTime[Steps] = float(Head);
			StepKeys[Steps] = NextField(Part);
			Steps++;
		}
	}
	return Steps > 0 && EndTime > 0;
}

function string KeysAt(float T)
{
	local int i;
	local string Keys;

	Keys = "-";
	for (i = 0; i < Steps; i++)
		if (StepTime[i] <= T)
			Keys = StepKeys[i];
	return Keys;
}

function string Vec(vector V)
{
	return V.X$" "$V.Y$" "$V.Z;
}

function int BotCount()
{
	local Pawn P;
	local int N;

	for (P = Level.PawnList; P != None; P = P.nextPawn)
		if (P.IsA('Bot'))
			N++;
	return N;
}

function LogStarts()
{
	local NavigationPoint N;

	for (N = Level.NavigationPointList; N != None; N = N.nextNavigationPoint)
		if (N.IsA('PlayerStart'))
			log("UTAREC START "$Vec(N.Location)$" "$N.Rotation.Yaw);
	bStartsLogged = true;
}

// Called once per player input tick. Returns the keys held this tick, or ""
// when no run is active (the player's own input is left alone).
function string InputTick(UTARecPawn P, float DeltaTime)
{
	if (!bStartsLogged)
		LogStarts();
	if (P.IsInState('PlayerWaiting'))
	{
		P.bReadyToPlay = true; // as a click on Fire would; the match then starts
		return "";
	}
	if (!P.IsInState('PlayerWalking'))
	{
		// Died (a pit, lava) or otherwise left walking: the run is abandoned,
		// and a dead player is put back for the next one.
		if (Phase != 0)
		{
			log("UTAREC SKIP "$RunName$" left PlayerWalking for "$P.GetStateName()$" at tick "$Tick);
			Phase = 0;
			Index++;
		}
		if (P.IsInState('Dying'))
			P.ServerReStartPlayer();
		return "";
	}
	if (K <= 0)
	{
		log("UTAREC NOK");
		P.ConsoleCommand("exit");
		return "";
	}

	if (Phase == 0)
	{
		if (Index >= ArrayCount(Runs) || Runs[Index] == "")
		{
			log("UTAREC DONE "$Index);
			P.ConsoleCommand("exit");
			Index = ArrayCount(Runs);
			return "";
		}
		if (!ParseRun(Runs[Index]))
		{
			log("UTAREC SKIP "$RunName$" unreadable");
			Index++;
			return "";
		}
		Phase = 1;
		Waited = 0;
		Still = 0;
		if (!P.SetLocation(Pose))
		{
			log("UTAREC SKIP "$RunName$" pose refused");
			Phase = 0;
			Index++;
			return "";
		}
		P.Velocity = vect(0,0,0);
		P.Acceleration = vect(0,0,0);
		P.SetRotation(rot(0,1,0) * PoseYaw);
		P.ViewRotation = rot(0,1,0) * PoseYaw;
		P.SetPhysics(PHYS_Falling); // it lands, then walks
		return "-";
	}

	if (Phase == 1)
	{
		Waited += DeltaTime;
		if (P.Physics == PHYS_Walking && VSize(P.Velocity) < 1 && P.DodgeDir == DODGE_None)
			Still += DeltaTime;
		else
			Still = 0;
		if (Still >= SettleSeconds)
		{
			Phase = 2;
			Tick = 0;
			log("UTAREC 1 BEGIN run="$RunName$" map="$MapName$" rate="$Rate$" pose="$Pose.X$","$Pose.Y$","$Pose.Z$","$PoseYaw);
			log("UTAREC 1 PAWN class="$P.Class.Name$" GroundSpeed="$P.GroundSpeed$" AirSpeed="$P.AirSpeed
				$" AccelRate="$P.AccelRate$" JumpZ="$P.JumpZ$" AirControl="$P.AirControl$" MaxStepHeight="$P.MaxStepHeight
				$" CollisionRadius="$P.CollisionRadius$" CollisionHeight="$P.CollisionHeight$" DodgeClickTime="$P.DodgeClickTime
				$" BaseEyeHeight="$P.BaseEyeHeight$" Mass="$P.Mass$" TimeDilation="$Level.TimeDilation
				$" ZoneGravityZ="$P.Region.Zone.ZoneGravity.Z$" ZoneGroundFriction="$P.Region.Zone.ZoneGroundFriction
				$" Bots="$BotCount());
		}
		else if (Waited >= SkipSeconds)
		{
			log("UTAREC SKIP "$RunName$" never settled");
			Phase = 0;
			Index++;
		}
		return "-";
	}

	// Phase 2: running. Tick 0 is the first tick after the player settled.
	if (Tick == 0)
		StartTime = Level.TimeSeconds;
	if (Level.TimeSeconds - StartTime >= EndTime)
	{
		log("UTAREC 1 END run="$RunName$" ticks="$Tick);
		Phase = 0;
		Index++;
		return "-";
	}
	bRowDue = true;
	return KeysAt(Level.TimeSeconds - StartTime);
}

// The tick's row: this tick's input, and the state the last tick left.
function LogTick(UTARecPawn P, string Keys, float DeltaTime)
{
	local string BaseName;

	if (!bRowDue)
		return;
	bRowDue = false;
	if (P.Base == None)
		BaseName = "None";
	else
		BaseName = string(P.Base.Name);
	log("UTAREC 1 T "$Tick$" "$Level.TimeSeconds$" "$DeltaTime$" "$Keys$" "$P.aBaseY$" "$P.aStrafe
		$" "$Vec(P.Location)$" "$Vec(P.Velocity)$" "$Vec(P.Acceleration)
		$" "$GetEnum(enum'EPhysics', P.Physics)$" "$GetEnum(enum'EDodgeDir', P.DodgeDir)$" "$P.AnimSequence$" "$BaseName);
	Tick++;
}

// Calibration: no script; log the axes the player's real keys leave.
function LogCalibration(UTARecPawn P, float DeltaTime)
{
	if (!bStartsLogged)
	{
		bStartsLogged = true;
		StartTime = Level.TimeSeconds;
		log("UTAREC CAL READY");
	}
	if (P.IsInState('PlayerWaiting'))
		P.bReadyToPlay = true;
	log("UTAREC CAL "$DeltaTime$" "$P.aBaseY$" "$P.aStrafe);
	if (Level.TimeSeconds - StartTime > CalibrateSeconds)
	{
		log("UTAREC DONE");
		P.ConsoleCommand("exit");
	}
}

defaultproperties
{
	// Measured by ut-record --calibrate, 2026-11-10: 6000 on every held tick
	// at 30 and 120 fps (the aliases' Speed=300, scaled by the native input).
	K=6000
	CalibrateSeconds=12
	SettleSeconds=0.5
	SkipSeconds=5
}
