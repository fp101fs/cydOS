#ifndef LAUNCHER_NVS_HELPERS_H
#define LAUNCHER_NVS_HELPERS_H

#include <Arduino.h>
#include <nvs.h>
#include <vector>

// Single entry point for NVS access in the Launcher backend (vendored from bmorcelli/Launcher).
namespace lnvs {

class Handle {
public:
    Handle() = default;
    Handle(const char *ns, bool write);
    ~Handle();
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    bool open(const char *ns, bool write);

    explicit operator bool() const { return handle != 0; }
    nvs_handle_t raw() const { return handle; }
    bool commit();

private:
    nvs_handle_t handle = 0;
};

std::vector<String> keys(const char *ns, nvs_type_t filter = NVS_TYPE_ANY);
bool exists(const char *ns);
bool eraseNamespace(const char *ns);

String getString(nvs_handle_t handle, const char *key, size_t maxLen = 0);
String getString(const char *ns, const char *key, size_t maxLen = 0);

bool setString(nvs_handle_t handle, const char *key, const char *value);
bool setString(const char *ns, const char *key, const char *value);

bool eraseKey(nvs_handle_t handle, const char *key);

bool getBool(nvs_handle_t handle, const char *key, bool &value);
bool setBool(nvs_handle_t handle, const char *key, bool value);
bool getInt(nvs_handle_t handle, const char *key, int &value);
bool setInt(nvs_handle_t handle, const char *key, int value);

bool copyBlob(nvs_handle_t from, const char *fromKey, nvs_handle_t to, const char *toKey, size_t maxLen);

const char *typeName(nvs_type_t type);
nvs_type_t typeFromName(const char *name);
bool getScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t &value);
bool setScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t value);

} // namespace lnvs

#endif /* LAUNCHER_NVS_HELPERS_H */
