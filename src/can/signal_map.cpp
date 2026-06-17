#include "signal_map.h"
#include "signal_map_rs.h"

SignalId signalIdFromName(const char *name) {
    return signal_id_from_name_rs(name);
}
