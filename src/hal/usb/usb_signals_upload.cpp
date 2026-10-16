#include "usb_comm_internal.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

void UsbCommInternal::commitSignalsUpload() {
    const StorageDriver::ChunkCommit result =
        StorageDriver::endChunkedWriteChecked(ConfigLoader::acceptSignalsUpload);
    if (result == StorageDriver::ChunkCommit::Rejected) {
        UsbComm::sendError("invalid_signals");
        return;
    }
    if (result == StorageDriver::ChunkCommit::Failed) {
        UsbComm::sendError("write_failed");
        return;
    }
    LOG_INFO("USB", "PUT_FILE signals.json accepted — rebooting");
    UsbCommInternal::rebootIntoSavedConfig();
}
