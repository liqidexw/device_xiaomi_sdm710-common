/*
 * Copyright (C) 2019-2020 The mokeeOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "FingerprintInscreenService"

#include "FingerprintInscreen.h"

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <cmath>
#include <fstream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>

#define FINGERPRINT_ACQUIRED_VENDOR 6

#define COMMAND_NIT 10
#define PARAM_NIT_630_FOD 1
#define PARAM_NIT_NONE 0

#define FOD_PRESSED_PATH "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_pressed"
#define FOD_PRESSED_ON 1
#define FOD_PRESSED_OFF 0

#define FOD_STATUS_PATH "/sys/devices/virtual/touch/tp_dev/fod_status"
#define FOD_STATUS_ON 1
#define FOD_STATUS_OFF 0

#define FOD_UI_PATH "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_ui"

#define FOD_DEFAULT_X 445
#define FOD_DEFAULT_Y 1910
#define FOD_DEFAULT_SIZE 190

namespace {

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

static std::vector<std::string> GetListProperty(const std::string& key)
{
    return android::base::Split(android::base::GetProperty(key, ""), ",");
}

template <typename T>
static std::vector<T> GetIntListProperty(const std::string& key,
                                         const std::vector<T> default_values,
                                         T min = std::numeric_limits<T>::min(),
                                         T max = std::numeric_limits<T>::max())
{
    std::vector<std::string> strings = GetListProperty(key);
    std::vector<std::string>::const_iterator it;
    std::vector<T> values;
    T value;

    if (strings.size() != default_values.size())
            goto unexpected;

    for (it = strings.begin(); it != strings.end(); it++) {
        if (!android::base::ParseInt(*it, &value, min, max))
                goto unexpected;

        values.push_back(value);
    }

    return values;

unexpected:
    LOG(WARNING) << "property '" << key
                 << "' does not exist or has an unexpected value\n";

    return default_values;
}

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

} // anonymous namespace

namespace vendor {
namespace mokee {
namespace biometrics {
namespace fingerprint {
namespace inscreen {
namespace V1_0 {
namespace implementation {

FingerprintInscreen::FingerprintInscreen() {
    xiaomiFingerprintService = IXiaomiFingerprint::getService();

    std::vector<int32_t> ints { FOD_DEFAULT_X, FOD_DEFAULT_Y };

    ints = GetIntListProperty("persist.vendor.sys.fp.fod.location.X_Y", ints);
    fodPosX = ints[0];
    fodPosY = ints[1];

    ints = { FOD_DEFAULT_SIZE, FOD_DEFAULT_SIZE };
    ints = GetIntListProperty("persist.vendor.sys.fp.fod.size.width_height", ints);
    if (ints[0] != ints[1])
           LOG(WARNING) << "FoD size should be square but it is not (width = "
                        << ints[0] << ", height = " << ints[1] << "\n";
    fodSize = std::max(ints[0], ints[1]);

    LOG(INFO) << "FoD is located at " << fodPosX << "," << fodPosY
              << " with size " << fodSize << "pixels\n";

    std::thread([this]() {
        int fd = open(FOD_UI_PATH, O_RDONLY);
        if (fd < 0) {
            LOG(ERROR) << "failed to open fd, err: " << fd;
            return;
        }

        struct pollfd fodUiPoll = {
            .fd = fd,
            .events = POLLERR | POLLPRI,
            .revents = 0,
        };

        while (true) {
            int rc = poll(&fodUiPoll, 1, -1);
            if (rc < 0) {
                LOG(ERROR) << "failed to poll fd, err: " << rc;
                continue;
            }

            xiaomiFingerprintService->extCmd(COMMAND_NIT,
                    readBool(fd) ? PARAM_NIT_630_FOD : PARAM_NIT_NONE);
        }
    }).detach();
}

Return<int32_t> FingerprintInscreen::getPositionX() {
    return fodPosX;
}

Return<int32_t> FingerprintInscreen::getPositionY() {
    return fodPosY;
}

Return<int32_t> FingerprintInscreen::getSize() {
    return fodSize;
}

Return<void> FingerprintInscreen::onStartEnroll() {
    return Void();
}

Return<void> FingerprintInscreen::onFinishEnroll() {
    return Void();
}

Return<void> FingerprintInscreen::onPress() {
    set(FOD_PRESSED_PATH, FOD_PRESSED_ON);
    return Void();
}

Return<void> FingerprintInscreen::onRelease() {
    set(FOD_PRESSED_PATH, FOD_PRESSED_OFF);
    return Void();
}

Return<void> FingerprintInscreen::onShowFODView() {
    set(FOD_STATUS_PATH, FOD_STATUS_ON);
    return Void();
}

Return<void> FingerprintInscreen::onHideFODView() {
    set(FOD_STATUS_PATH, FOD_STATUS_OFF);
    return Void();
}

Return<bool> FingerprintInscreen::handleAcquired(int32_t acquiredInfo, int32_t vendorCode) {
    std::lock_guard<std::mutex> _lock(mCallbackLock);
    if (mCallback == nullptr) {
        return false;
    }

    if (acquiredInfo == FINGERPRINT_ACQUIRED_VENDOR) {
        if (vendorCode == 22) {
            Return<void> ret = mCallback->onFingerDown();
            if (!ret.isOk()) {
                LOG(ERROR) << "FingerDown() error: " << ret.description();
            }
            return true;
        }

        if (vendorCode == 23) {
            Return<void> ret = mCallback->onFingerUp();
            if (!ret.isOk()) {
                LOG(ERROR) << "FingerUp() error: " << ret.description();
            }
            return true;
        }
    }

    return false;
}

Return<bool> FingerprintInscreen::handleError(int32_t error, int32_t vendorCode) {
    LOG(ERROR) << "error: " << error << ", vendorCode: " << vendorCode;
    return false;
}

Return<void> FingerprintInscreen::setLongPressEnabled(bool) {
    return Void();
}

Return<int32_t> FingerprintInscreen::getDimAmount(int32_t) {
    return 0;
}

Return<bool> FingerprintInscreen::shouldBoostBrightness() {
    return false;
}

Return<void> FingerprintInscreen::setCallback(const sp<IFingerprintInscreenCallback>& callback) {
    {
        std::lock_guard<std::mutex> _lock(mCallbackLock);
        mCallback = callback;
    }

    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace inscreen
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace mokee
}  // namespace vendor
