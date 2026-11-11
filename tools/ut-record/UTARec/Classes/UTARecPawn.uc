//=============================================================================
// UTARecPawn -- UT_Ants UTA-0456. The stock player, with its keys pressed by
// a script. ut-record asks for it with ?Class=UTARec.UTARecPawn.
//
// It declares no default-properties block, so every movement value is
// TMale1's (spec INV-3, whose test greps this file for that block's keyword). It overrides PlayerInput only: while a UTARec run is active
// it sets the input variables to what the held keys would leave, presses
// the fire buttons a weapon run holds (UTA-0019), logs the tick, then hands over to the original's own PlayerInput. Everything after
// that -- the dodge edges, PlayerMove, ProcessMove, native physics -- is
// the original's.
//=============================================================================
class UTARecPawn expands TMale1;

var UTARec Rec;
var bool bJumpHeld; // J was held last tick, so this tick is not its first
var bool bFireHeld, bAltHeld, bSwitchHeld; // P, A and N, likewise

// A weapon run logs every hit this pawn takes (UTA-0019 SS 4.7).
function TakeDamage(int Damage, Pawn InstigatedBy, vector HitLocation, vector Momentum, name DamageType)
{
	local int Health0;
	local vector Velocity0;
	local EPhysics Physics0;

	Health0 = Health;
	Velocity0 = Velocity;
	Physics0 = Physics;
	Super.TakeDamage(Damage, InstigatedBy, HitLocation, Momentum, DamageType);
	if (Rec != None)
		Rec.LogDamage(self, InstigatedBy, Damage, DamageType, HitLocation, Momentum, Health0, Velocity0, Physics0);
}

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
		// The fire buttons as a bound key works them: the exec function on
		// the press, the button byte held, which a weapon reads to refire
		// and to release a charge.
		bFire = 0;
		bAltFire = 0;
		if (InStr(Keys, "P") >= 0)
		{
			bFire = 1;
			if (!bFireHeld)
				Fire(0);
			bFireHeld = true;
		}
		else
			bFireHeld = false;
		if (InStr(Keys, "A") >= 0)
		{
			bAltFire = 1;
			if (!bAltHeld)
				AltFire(0);
			bAltHeld = true;
		}
		else
			bAltHeld = false;
		if (InStr(Keys, "N") >= 0)
		{
			if (!bSwitchHeld)
				Rec.SwitchNow(self);
			bSwitchHeld = true;
		}
		else
			bSwitchHeld = false;
		Rec.LogTick(self, Keys, DeltaTime);
	}
	else
	{
		bJumpHeld = false;
		bFireHeld = false;
		bAltHeld = false;
		bSwitchHeld = false;
	}
	Super.PlayerInput(DeltaTime);
}
