// struct: CarPhysicsRecord
// subsystem: car
// size: ? L2 decomp:re/decomp/142e90560.c
// notes: the live-vehicle physics/sim record at CarLiveVehicle+0x20 (live vehicle = *(mgr+0x110..)).
// Gateway to per-wheel tyre state. Distinct from CarPerfVehicle (the R&D attribute object).
struct CarPhysicsRecord {
    /* 0x0058 */ void *wheel_state;  // L2 decomp:re/decomp/142e90560.c;kb:subsystems/car.md#live -> CarWheelState (tyre/wheel-state object)
};
