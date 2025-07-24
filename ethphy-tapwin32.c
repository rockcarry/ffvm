#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <windows.h>
#include "ethphy.h"

//=============
// TAP IOCTLs
//=============
#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1, METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2, METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3, METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7, METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9, METHOD_BUFFERED)

//=================
// Registry keys
//=================
#define ADAPTER_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define NETWORK_CONNECTIONS_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

//======================
// Filesystem prefixes
//======================
#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define TAPSUFFIX         ".tap"

static int is_tap_win32_dev(const char *guid)
{
    HKEY netcard_key;
    LONG status;
    DWORD len;
    int i = 0;

    status = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        ADAPTER_KEY,
        0,
        KEY_READ,
        &netcard_key);

    if (status != ERROR_SUCCESS) {
        return FALSE;
    }

    for (;;) {
        char enum_name[256];
        char unit_string[256];
        HKEY unit_key;
        char component_id_string[] = "ComponentId";
        char component_id[256];
        char net_cfg_instance_id_string[] = "NetCfgInstanceId";
        char net_cfg_instance_id[256];
        DWORD data_type;

        len = sizeof (enum_name);
        status = RegEnumKeyEx(
            netcard_key,
            i,
            enum_name,
            &len,
            NULL,
            NULL,
            NULL,
            NULL);

        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        } else if (status != ERROR_SUCCESS) {
            return FALSE;
        }

        snprintf(unit_string, sizeof(unit_string), "%s\\%s", ADAPTER_KEY, enum_name);

        status = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            unit_string,
            0,
            KEY_READ,
            &unit_key);

        if (status != ERROR_SUCCESS) {
            return FALSE;
        } else {
            len = sizeof (component_id);
            status = RegQueryValueEx(
                unit_key,
                component_id_string,
                NULL,
                &data_type,
                (LPBYTE)component_id,
                &len);

            if (!(status != ERROR_SUCCESS || data_type != REG_SZ)) {
                len = sizeof (net_cfg_instance_id);
                status = RegQueryValueEx(
                    unit_key,
                    net_cfg_instance_id_string,
                    NULL,
                    &data_type,
                    (LPBYTE)net_cfg_instance_id,
                    &len);

                if (status == ERROR_SUCCESS && data_type == REG_SZ) {
                    if (/* !strcmp (component_id, TAP_COMPONENT_ID) &&*/
                        !strcmp (net_cfg_instance_id, guid)) {
                        RegCloseKey (unit_key);
                        RegCloseKey (netcard_key);
                        return TRUE;
                    }
                }
            }
            RegCloseKey (unit_key);
        }
        ++i;
    }

    RegCloseKey (netcard_key);
    return FALSE;
}

static int get_device_guid(
    char *name,
    int name_size,
    char *actual_name,
    int actual_name_size)
{
    LONG status;
    HKEY control_net_key;
    DWORD len;
    int i = 0;
    int stop = 0;

    status = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        NETWORK_CONNECTIONS_KEY,
        0,
        KEY_READ,
        &control_net_key);

    if (status != ERROR_SUCCESS) {
        return -1;
    }

    while (!stop) {
        char enum_name[256];
        char connection_string[256];
        HKEY connection_key;
        char name_data[256];
        DWORD name_type;
        const char name_string[] = "Name";

        len = sizeof (enum_name);
        status = RegEnumKeyEx(
            control_net_key,
            i,
            enum_name,
            &len,
            NULL,
            NULL,
            NULL,
            NULL);

        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        } else if (status != ERROR_SUCCESS) {
            return -1;
        }

        snprintf(connection_string, sizeof(connection_string), "%s\\%s\\Connection", NETWORK_CONNECTIONS_KEY, enum_name);
        status = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            connection_string,
            0,
            KEY_READ,
            &connection_key);

        if (status == ERROR_SUCCESS) {
            len = sizeof (name_data);
            status = RegQueryValueEx(
                connection_key,
                name_string,
                NULL,
                &name_type,
                (LPBYTE)name_data,
                &len);

            if (status != ERROR_SUCCESS || name_type != REG_SZ) {
                ++i;
                continue;
            }
            else {
                if (is_tap_win32_dev(enum_name)) {
                    snprintf(name, name_size, "%s", enum_name);
                    if (actual_name) {
                        if (strcmp(actual_name, "") != 0) {
                            if (strcmp(name_data, actual_name) != 0) {
                                RegCloseKey (connection_key);
                                ++i;
                                continue;
                            }
                        } else {
                            snprintf(actual_name, actual_name_size, "%s", name_data);
                        }
                    }
                    stop = 1;
                }
            }

            RegCloseKey (connection_key);
        }
        ++i;
    }

    RegCloseKey (control_net_key);

    if (stop == 0)
        return -1;

    return 0;
}

static int tap_win32_set_status(HANDLE handle, int status)
{
    unsigned long len = 0;
    return DeviceIoControl(handle, TAP_IOCTL_SET_MEDIA_STATUS,
                &status, sizeof (status),
                &status, sizeof (status), &len, NULL);
}

typedef struct {
    HANDLE   handle;

    #define FLAG_EXIT (1 << 0)
    uint32_t  flags;
    pthread_t thread;
    PFN_ETHPHY_CALLBACK callback;
    void               *cbctx;
} PHYDEV;

static void* ethphy_work_proc(void *arg)
{
    PHYDEV *phy = arg;
    uint8_t buf[1600];
    while (!(phy->flags & FLAG_EXIT)) {
        DWORD readn = 0; OVERLAPPED osRead = {};
        if (!ReadFile(phy->handle, buf, sizeof(buf), &readn, &osRead)) {
            DWORD dwErr = GetLastError();
            switch (dwErr) {
            case ERROR_IO_PENDING:
                GetOverlappedResult(phy->handle, &osRead, &readn, TRUE);
                break;
            }
        }
        if (readn > 0 && phy->callback) phy->callback(phy->cbctx, (char*)buf, readn);
    }
    return NULL;
}

void* ethphy_open(char *ifname, PFN_ETHPHY_CALLBACK callback, void *cbctx)
{
    PHYDEV *phy = calloc(1, sizeof(PHYDEV));
    if (!phy) return NULL;

    phy->callback = callback;
    phy->cbctx    = cbctx;

    char device_guid[0x100] = {};
    char name_buffer[0x100] = {};
    char device_path[MAX_PATH];
    snprintf(name_buffer, sizeof(name_buffer), "%s", ifname);
    int rc = get_device_guid(device_guid, sizeof(device_guid), name_buffer, sizeof(name_buffer));
    if (rc) { printf("phy_open, failed to get device guid !\n"); goto failed; }

    snprintf(device_path, sizeof(device_path), "%s%s%s", USERMODEDEVICEDIR, device_guid, TAPSUFFIX);
    phy->handle = CreateFile(device_path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
    if (!phy->handle) { printf("phy_open, failed to CreateFile !\n"); goto failed; }

    struct {
        unsigned long major;
        unsigned long minor;
        unsigned long debug;
    } version;
    DWORD version_len;
    BOOL bret = DeviceIoControl(phy->handle, TAP_IOCTL_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &version_len, NULL);
    if (bret == FALSE) { printf("phy_open, failed to get verion !\n"); goto failed; }

    if (!tap_win32_set_status(phy->handle, TRUE)) { printf("phy_open, failed to set status !\n"); goto failed; }
    pthread_create(&phy->thread, 0, ethphy_work_proc, phy);
    return phy;

failed:
    if (phy->handle) CloseHandle(phy->handle);
    free(phy);
    return NULL;
}

void ethphy_close(void *ctx)
{
    PHYDEV *phy = ctx;
    if (!phy) return;
    phy->flags |= FLAG_EXIT;
    CloseHandle(phy->handle);
    pthread_join(phy->thread, NULL);
    tap_win32_set_status(phy->handle, FALSE);
    free(phy);
}

int ethphy_send(void *ctx, char *buf, int len)
{
    PHYDEV *phy = ctx;
    if (!phy) return -1;
    DWORD      written =  0;
    OVERLAPPED osWrite = {};
    if (!WriteFile(phy->handle, buf, len, &written, &osWrite)) {
        DWORD dwErr = GetLastError();
        switch (dwErr) {
        case ERROR_IO_PENDING:
            GetOverlappedResult(phy->handle, &osWrite, &written, TRUE);
            break;
        }
    }
    return 0;
}
