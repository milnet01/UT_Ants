//=============================================================================
// UTARecPawn -- UT_Ants UTA-0456. The stock player, with its keys pressed by
// a script. ut-record asks for it with ?Class=UTARec.UTARecPawn.
//
// It declares no default-properties block, so every movement value is
// TMale1's (spec INV-3, whose test greps this file for that block's keyword). It overrides PlayerInput only: while a UTARec run is active
// it sets the input variables to what the held keys would leave, logs the
// tick, then hands over to the original's own PlayerInput. Everything after
// that -- the dodge edges, PlayerMove, ProcessMove, native physics -- is
// the original's.
//=============================================================================
class UTARecPawn expands TMale1;

var UTARec Rec;
var bool bJumpHeld; // J was held last tick, so this tick is not its first

event PlayerInput(float DeltaTime)
{
	local string Keys;
	local float K;

	if (Rec == None)
		foreach AllActors(class'UTARec', Rec)
			break;
	if (Rec == None)
	{
		Super.PlayerInput(DeltaTime);
		return;
	}
	if (Rec.bCalibrate)
	{
		Rec.LogCalibration(self, DeltaTime);
		Super.PlayerInput(DeltaTime);
		return;
	}

	Keys = Rec.InputTick(self, DeltaTime);
	if (Keys != "")
	{
		K = Rec.K;
		// No mouse and no turn keys: turning is out of scope (spec section 9).
		aMouseX = 0;
		aMouseY = 0;
		aBaseX = 0;
		aTurn = 0;
		aLookUp = 0;
		aBaseY = 0;
		aStrafe = 0;
		aUp = 0;
		bDuck = 0;
		bRun = 0;
		if (InStr(Keys, "F") >= 0) aBaseY += K;
		if (InStr(Keys, "B") >= 0) aBaseY -= K;
		if (InStr(Keys, "L") >= 0) aStrafe -= K;
		if (InStr(Keys, "R") >= 0) aStrafe += K;
		if (InStr(Keys, "D") >= 0)
		{
			bDuck = 1;
			aUp -= K;
		}
		if (InStr(Keys, "W") >= 0) bRun = 1;
		if (InStr(Keys, "J") >= 0)
		{
			aUp += K;
			if (!bJumpHeld)
				Jump();
			bJumpHeld = true;
		}
		else
			bJumpHeld = false;
		Rec.LogTick(self, Keys, DeltaTime);
	}
	else
		bJumpHeld = false;
	Super.PlayerInput(DeltaTime);
}
