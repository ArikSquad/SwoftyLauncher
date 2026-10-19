// struct: CarWheelState
// subsystem: car
// size: ? L2 decomp:re/decomp/142e90560.c
// notes: tyre/wheel-state object at CarPhysicsRecord+0x58. tyre_set_index selects the live wheel; the
// wheel container holds 4 per-wheel objects, each exposing a scalar at wheel+0x88 (the four are averaged
// x0.25 into mgr's robin_hood map<u8,float>). The per-wheel scalar's meaning (tyre temp vs wear vs load)
// NEEDS A RUNTIME TRACE.
struct CarWheelState {
    /* 0x00e8 */ uint8_t tyre_set_index;  // L2 decomp:re/decomp/142e90560.c;kb:subsystems/car.md#live u8 live wheel/tyre-set index (0xff = none)
    /* 0x0580 */ void *wheel_container;   // L2 decomp:re/decomp/142e90560.c;kb:subsystems/car.md#live holds 4 per-wheel object ptrs at +0x70/+0x78/+0x80/+0x88; each wheel's scalar is [wheel+0x88] (float)
};
