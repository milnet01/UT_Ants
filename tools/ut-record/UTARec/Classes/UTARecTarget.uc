//=============================================================================
// UTARecTarget -- UT_Ants UTA-0019 SS 4.7. What a weapon run shoots at: a
// pawn the player's size and mass, with no mind, that logs every hit.
//
// It counts as a player (bIsPlayer) and carries a UTARecToken, so damage
// reaches it as it reaches a real opponent with a weapon: through
// ReduceDamage, where hardcore mode scales it by 1.5 (Pawn.TakeDamage gives a
// player with no inventory the raw damage). Its health is set too high for
// any run to use up.
//=============================================================================
class UTARecTarget expands Pawn;

var UTARec Rec;

function PostBeginPlay()
{
	local UTARecToken Token;

	Super.PostBeginPlay();
	Token = Spawn(class'UTARecToken');
	if (Token != None)
		Token.GiveTo(self);
}

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

defaultproperties
{
	bIsPlayer=True
	Health=100000
	Mass=100
	CollisionRadius=17
	CollisionHeight=39
	bCollideActors=True
	bCollideWorld=True
	bBlockActors=True
	bBlockPlayers=True
	bProjTarget=True
	DrawType=DT_Mesh
	Mesh=LodMesh'Botpack.Soldier'
	GroundSpeed=400
	JumpZ=325
	BaseEyeHeight=27
	EyeHeight=27
}
