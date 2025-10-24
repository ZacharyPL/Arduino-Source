/*  Gamepad Input Support
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_GamepadInput_GamepadInput_H
#define PokemonAutomation_GamepadInput_GamepadInput_H

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <QObject>

namespace PokemonAutomation{

class Logger;
class ControllerSession;

namespace GamepadInput{


struct DeviceInfo{
    int id = -1;
    std::string name;
    std::string manufacturer;
};


class DeviceMonitor : public QObject{
    Q_OBJECT
public:
    ~DeviceMonitor();

    static DeviceMonitor& instance();

    std::vector<DeviceInfo> devices() const;
    std::string device_name(int device_id) const;

signals:
    void devicesChanged();

private:
    explicit DeviceMonitor(QObject* parent);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};


struct GamepadStatus{
    bool enabled_request = false;
    bool device_selected = false;
    bool device_connected = false;
    bool session_ready = false;
    bool active = false;
    std::string device_name;
    std::string block_reason;
};

//  Returns true if the build includes Qt's Gamepad module and runtime support
//  for physical controllers. When false, gamepad functionality is disabled
//  and the related APIs become inert.
bool is_gamepad_supported();


class GamepadController : public QObject{
    Q_OBJECT
public:
    GamepadController(Logger& logger, ControllerSession& session, QObject* parent = nullptr);
    ~GamepadController();

    void set_device(std::optional<int> device_id);
    std::optional<int> device() const;

    void set_enabled(bool enabled);
    bool enabled() const;

    GamepadStatus status() const;

signals:
    void statusChanged();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};


}
}

#endif
