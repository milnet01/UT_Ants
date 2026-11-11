//=============================================================================
// UTARecToken -- UT_Ants UTA-0019 SS 4.7. An item a UTARecTarget carries and
// nothing else: no armour, no use. Its only work is to be an inventory, so
// Pawn.TakeDamage sends the target's damage through ReduceDamage.
//=============================================================================
class UTARecToken expands Inventory;

defaultproperties
{
	bHidden=True
}
