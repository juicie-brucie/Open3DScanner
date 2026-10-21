#include "device_claim.h"

namespace openscanner::helper {

bool NullDeviceClaim::Acquire(std::string* why) {
    if (why) {
        *why =
            "no device backend selected (null); pass --claim iokit to open the scanner's USB device";
    }
    return false;
}

void NullDeviceClaim::Release() {}

}  // namespace openscanner::helper
