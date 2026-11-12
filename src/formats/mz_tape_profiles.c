/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#include "mz_tape_profiles.h"

#include <string.h>

#define CAPS_NATIVE (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_RECORD | \
                     MZ_TAPE_CAP_AUTONAME)
#define CAPS_LOADER_RECORD (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_RECORD | \
                            MZ_TAPE_CAP_AUTONAME | MZ_TAPE_CAP_LOADER)

static const mz_tape_profile_t mz_tape_profiles_P[MZ_TAPE_PROFILE_COUNT]
     = {
    { MZ_TAPE_PROFILE_MZ800_NORMAL_1X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 3807U, 4087U, 7506U, 7786U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_2X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 1818U, 2228U, 3753U, 4164U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_3X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 1407U, 1994U, 2815U, 3577U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ700_NORMAL_1X, MZ_TAPE_FRAMING_MZ700_NATIVE,
      CAPS_NATIVE, 3840U, 4224U, 7424U, 7904U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ700_FAST3_3X, MZ_TAPE_FRAMING_MZ700_FAST3,
      (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_LOADER),
      1280U, 1280U, 2560U, 2560U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_4, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 1232U, 1877U, 2522U, 2874U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_3, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 1407U, 1994U, 2815U, 3577U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_2, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 1818U, 2228U, 3753U, 4164U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_TC_1_3, MZ_TAPE_FRAMING_TC_TURBO,
      CAPS_LOADER_RECORD, 1687U, 1673U, 3360U, 3360U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_TC_1_2, MZ_TAPE_FRAMING_TC_TURBO,
      CAPS_LOADER_RECORD, 2269U, 2255U, 4524U, 4524U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_4X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 1232U, 1877U, 2522U, 2874U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    /* Intercopy V10.2 1200 Bd writer, rounded to 16 MHz Timer3 ticks. */
    { MZ_TAPE_PROFILE_IC_1_1, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 3753U, 4222U, 7506U, 7917U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    /* Turbo Copy V1.22 MODE3 1:1: count 551/1102 @ nominal CKMS 1.10 MHz. */
    { MZ_TAPE_PROFILE_TC_1_1, MZ_TAPE_FRAMING_TC_TURBO,
      CAPS_LOADER_RECORD, 4015U, 4000U, 8015U, 8015U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false }
};

_Static_assert((sizeof(mz_tape_profiles_P) /
               sizeof(mz_tape_profiles_P[0])) == MZ_TAPE_PROFILE_COUNT,
              "MZ tape profile table is incomplete");

bool mz_tape_profile_read(mz_tape_profile_id_t id,
                          mz_tape_profile_t *profile)
{
    if ((profile == NULL) || ((unsigned)id >= MZ_TAPE_PROFILE_COUNT))
    {
        return false;
    }
    memcpy(profile, &mz_tape_profiles_P[(uint8_t)id], sizeof(*profile));
    return true;
}

bool mz_tape_profile_has_capability(mz_tape_profile_id_t id, uint8_t cap)
{
    mz_tape_profile_t profile;
    return mz_tape_profile_read(id, &profile) &&
           ((profile.caps & cap) == cap);
}

/* Mode values are kept identical to the reference metadata/store schema. */
mz_tape_profile_id_t mz_tape_profile_for_mode(loader_mode_t mode) {
    switch(mode) {
    case LOADER_MODE_NORMAL_1_2: return MZ_TAPE_PROFILE_MZ800_NORMAL_2X;
    case LOADER_MODE_NORMAL_1_3: return MZ_TAPE_PROFILE_MZ800_NORMAL_3X;
    case LOADER_MODE_NORMAL_1_4: return MZ_TAPE_PROFILE_MZ800_NORMAL_4X;
    case LOADER_MODE_MZ700_1X: return MZ_TAPE_PROFILE_MZ700_NORMAL_1X;
    case LOADER_MODE_MZ700_3X: return MZ_TAPE_PROFILE_MZ700_FAST3_3X;
    case LOADER_MODE_IC_1_1: return MZ_TAPE_PROFILE_IC_1_1;
    case LOADER_MODE_IC_1_2: return MZ_TAPE_PROFILE_IC_1_2;
    case LOADER_MODE_IC_1_3: return MZ_TAPE_PROFILE_IC_1_3;
    case LOADER_MODE_IC_1_4: return MZ_TAPE_PROFILE_IC_1_4;
    case LOADER_MODE_TC_1_1: return MZ_TAPE_PROFILE_TC_1_1;
    case LOADER_MODE_TC_1_2: return MZ_TAPE_PROFILE_TC_1_2;
    case LOADER_MODE_TC_1_3: return MZ_TAPE_PROFILE_TC_1_3;
    default: return MZ_TAPE_PROFILE_MZ800_NORMAL_1X;
    }
}
const char *loader_mode_name(loader_mode_t mode) {
    static const char *const names[]={"NORMAL 1:1","AUTO","UL","UL MZ800","UL MZ700",
        "MZ700 FAST3","IC 1:4","IC 1:3","IC 1:2","TC 1:3","TC 1:2",
        "NORMAL 1:2","NORMAL 1:3","MZ700 1:1","NORMAL 1:4","IC 1:1","TC 1:1"};
    return (unsigned)mode<LOADER_MODE_COUNT ? names[mode] : "INVALID";
}
