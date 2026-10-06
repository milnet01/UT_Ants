//=============================================================================
// UTACam -- UT_Ants UTA-0306. Puts the local player's view at each camera
// UTACam.ini lists, hides the HUD, logs the camera the frame is drawn from,
// and takes the engine's own screenshot, then exits. ut-compare writes the
// ini and runs the client; nothing in the map changes.
//
// A camera is one line as ut-shot reads it: x y z pitch yaw roll fov, in
// UT99 units and angles, fov horizontal in degrees.
//=============================================================================
class UTACam expands Mutator config(UTACam);

var config string Cameras[64];
var config float StartSeconds; // let the level load and settle
var config float HoldSeconds;  // a camera is held this long before its shot
var config float ShotSeconds;  // and this long after, so the file is written

var int Index;
var bool bPosed;
// The pose being held. The waiting-for-match state drifts the player and
// resets its pitch, so Tick puts it back every frame until the shot.
var PlayerPawn Posed;
var vector PawnAt;
var rotator Aim;
var float Fov;

// The next space-separated field of S, which loses it.
function float NextField(out string S)
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
	return float(Field);
}

function Hold(PlayerPawn PP)
{
	PP.SetLocation(PawnAt);
	PP.Velocity = vect(0,0,0);
	PP.Acceleration = vect(0,0,0);
	PP.SetRotation(Aim);
	PP.ViewRotation = Aim;
	PP.DesiredFOV = Fov;
	PP.FOVAngle = Fov;
	// Nothing but the world in the frame: the HUD, the start-of-match text
	// (a progress message, which the console draws) and the console's typing
	// line.
	if (PP.myHUD != None)
	{
		PP.myHUD.Destroy();
		PP.myHUD = None;
	}
	PP.ClearProgressMessages();
}

function Tick(float DeltaTime)
{
	if (bPosed && Posed != None)
		Hold(Posed);
}

function PostBeginPlay()
{
	Super.PostBeginPlay();
	Index = -1;
	SetTimer(StartSeconds, false);
}

function Timer()
{
	local Pawn P;
	local PlayerPawn PP;
	local Actor ViewActor;
	local vector Want, CamLoc;
	local rotator Rot, CamRot;
	local string Line;

	for (P = Level.PawnList; P != None; P = P.nextPawn)
		if (PlayerPawn(P) != None)
		{
			PP = PlayerPawn(P);
			break;
		}
	if (PP == None)
	{
		SetTimer(1.0, false);
		return;
	}

	// Second half of a camera: the view has held long enough to draw settled.
	if (bPosed)
	{
		bPosed = false;
		PP.PlayerCalcView(ViewActor, CamLoc, CamRot);
		PP.ConsoleCommand("shot");
		log("UTACAM SHOT "$Index$" cam="$CamLoc.X$","$CamLoc.Y$","$CamLoc.Z
			$" rot="$CamRot.Pitch$","$CamRot.Yaw$","$CamRot.Roll$" view="$ViewActor$" state="$PP.GetStateName()
			$" target="$PP.ViewTarget$" loc="$PP.Location.X$","$PP.Location.Y$","$PP.Location.Z
			// Where the eye is in the BSP: a leaf of -1 and zone 0 is solid,
			// from which the original draws a view ours does not.
			$" headZone="$PP.HeadRegion.Zone$" headZoneNumber="$PP.HeadRegion.ZoneNumber$" headLeaf="$PP.HeadRegion.iLeaf
			$" zone="$PP.Region.Zone$" leaf="$PP.Region.iLeaf);
		SetTimer(ShotSeconds, false);
		return;
	}

	Index++;
	if (Index >= 64 || Cameras[Index] == "")
	{
		log("UTACAM DONE "$Index);
		PP.ConsoleCommand("exit");
		return;
	}

	Line = Cameras[Index];
	Want.X = NextField(Line);
	Want.Y = NextField(Line);
	Want.Z = NextField(Line);
	Rot.Pitch = NextField(Line);
	Rot.Yaw = NextField(Line);
	Rot.Roll = NextField(Line);
	Fov = NextField(Line);
	Aim = Rot;

	PP.SetCollision(false, false, false);
	PP.bCollideWorld = false;
	PP.SetPhysics(PHYS_None);
	PP.bBehindView = false;

	// The view is drawn from the pawn's location plus its eye offset, so place
	// the pawn, read where the view lands, and move by the difference.
	PawnAt = Want;
	Hold(PP);
	PP.PlayerCalcView(ViewActor, CamLoc, CamRot);
	PawnAt = PP.Location + (Want - CamLoc);
	Hold(PP);
	Posed = PP;
	PP.PlayerCalcView(ViewActor, CamLoc, CamRot);
	log("UTACAM POSE "$Index$" cam="$CamLoc.X$","$CamLoc.Y$","$CamLoc.Z
		$" rot="$CamRot.Pitch$","$CamRot.Yaw$","$CamRot.Roll$" fov="$PP.FOVAngle);
	bPosed = true;
	SetTimer(HoldSeconds, false);
}

defaultproperties
{
	StartSeconds=6.0
	HoldSeconds=4.0
	ShotSeconds=2.0
}
