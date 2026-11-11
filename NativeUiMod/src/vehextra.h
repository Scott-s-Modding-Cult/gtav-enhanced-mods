#pragma once
// Vehicle natives newer than the bundled Script Hook V SDK, called by hash. Every hash here was
// checked against alloc8or's native DB (github.com/alloc8or/gta5-nativedb-data) on 2026-11-11.
#include "nativeCaller.h"
#include "types.h"

namespace vehx {

// Interior / dashboard paint (SET/GET_VEHICLE_EXTRA_COLOUR_5 / _6).
inline void SetInteriorColour(Vehicle v, int c) { invoke<Void>(0xF40DD601A65F7F19, v, c); }
inline int GetInteriorColour(Vehicle v) { int c = 0; invoke<Void>(0x7D1464D472D32136, v, &c); return c; }
inline void SetDashboardColour(Vehicle v, int c) { invoke<Void>(0x6089CDF6A57F326C, v, c); }
inline int GetDashboardColour(Vehicle v) { int c = 0; invoke<Void>(0xB7635E80A5C31BFF, v, &c); return c; }
// Xenon headlight colour index 0-12; 255 = stock (SET/GET_VEHICLE_XENON_LIGHT_COLOR_INDEX).
inline void SetXenonColour(Vehicle v, int c) { invoke<Void>(0xE41033B25D003A07, v, c); }
inline int GetXenonColour(Vehicle v) { return invoke<int>(0x3DFF319A831E0CDB, v); }

// Rocket boost (Oppressor Mk II, Vigilante, Scramjet, Toreador, Voltic2...).
inline bool HasRocketBoost(Vehicle v) { return invoke<BOOL>(0x36D782F68B309BDA, v) != FALSE; }       // GET_HAS_ROCKET_BOOST
inline bool RocketBoostActive(Vehicle v) { return invoke<BOOL>(0x3D34E80EED4AE3BE, v) != FALSE; }    // IS_ROCKET_BOOST_ACTIVE
inline void SetRocketBoostFill(Vehicle v, float pct) { invoke<Void>(0xFEB2DDED3509562E, v, pct); }   // SET_ROCKET_BOOST_FILL
inline void SetRocketBoostRechargeTime(Vehicle v, float s) { invoke<Void>(0xE00F2AB100B76E89, v, s); }  // SET_SCRIPT_ROCKET_BOOST_RECHARGE_TIME
// Nitrous (Los Santos Tuners / drift cars).
inline bool NitrousActive(Vehicle v) { return invoke<BOOL>(0x491E822B2C464FE4, v) != FALSE; }        // IS_NITROUS_ACTIVE
inline void FullyChargeNitrous(Vehicle v) { invoke<Void>(0x1A2BCC8C636F9226, v); }                   // FULLY_CHARGE_NITROUS

}  // namespace vehx
