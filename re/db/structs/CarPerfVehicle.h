// struct: CarPerfVehicle
// subsystem: car
// size: ? L2 decomp:re/decomp/143514670.c (R&D write-back target; object far larger than 0x1960)
// notes: the per-car performance object the R&D apply path writes into. Reached from the owner
// (team/engine-supplier) vehicle vector *(Upgrades+8) [+0x298 begin .. +0x2a0 end]. The live physics
// vehicle (manager+0x110 list, +0x20 physics child) is a DIFFERENT object; see subsystems/car.md#live.
struct CarPerfVehicle {
    /* 0x0d10 */ uint8_t attr_elem0[0xc40]; // L2 decomp:re/decomp/143514670.c;xref:0x143514c64 first attribute element; array stride 0xc40; each element: fitted effect-def ptr list at element+0x000 (cap 0x180), list count at element+0xc00
    /* 0x1960 */ uint64_t attr_count;       // L2 decomp:re/decomp/143514670.c;xref:0x143514c50 count of 0xc40 attribute elements iterated by Car_Fit_UpgradesToRating
};
