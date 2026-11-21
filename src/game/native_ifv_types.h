#pragma once
#include "core/ini.h"
#include <array>
#include <vector>
#include <string>

struct NativeWeaponCatalog;
struct NativeIFVMode {
    int weapon=0,turret=0;
};
struct NativeIFVProfile {
    bool available=false,gunner=false;
    int version=0,passengerMode=0,turretCount=0;
    int burstDelayMin=3,burstDelayMax=5; // Source frames in v2; v1 retains catalog tick defaults.
    std::string image;
    std::vector<std::array<int,2>> weapons; // original numbered normal/elite catalog references
    std::vector<NativeIFVMode> modes; // IFVMode is a logical mode, not necessarily the weapon index
};
bool parseNativeIFV(const Ini::Section* row,bool required,const NativeWeaponCatalog* catalog,NativeIFVProfile& result);
