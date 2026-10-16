#pragma once

#include "workshop_mode_rs.h"

inline constexpr char kWorkshopNvsNamespace[] = "workshop";

struct WorkshopCredentials {
    char ssid[kWorkshopSsidCap];
    char password[kWorkshopPasswordCap];
};

namespace WorkshopCredentialStore {

[[nodiscard]] bool loadOrCreate(WorkshopCredentials &out);

}
