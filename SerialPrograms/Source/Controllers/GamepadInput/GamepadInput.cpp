/*  Gamepad Input Support
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include "Controllers/GamepadInput/GamepadInput.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <QCoreApplication>
#include <QMetaObject>
#include <QReadLocker>
#include <QReadWriteLock>
#include <QString>
#include <QTimer>

#include "Common/Cpp/Color.h"
#include "Common/Cpp/Time.h"
#include "CommonFramework/Logging/Logger.h"
#include "Controllers/ControllerSession.h"
#include "Controllers/ControllerTypes.h"
#include "NintendoSwitch/Controllers/NintendoSwitch_ControllerButtons.h"
#include "NintendoSwitch/Controllers/NintendoSwitch_VirtualControllerState.h"
#include "NintendoSwitch/Controllers/NintendoSwitch_ProController.h"

using namespace std::chrono_literals;

namespace PokemonAutomation{
namespace GamepadInput{

struct GamepadState{
    bool connected = false;

    bool south = false;   // Bottom (A / Cross)
    bool east = false;    // Right  (B / Circle)
    bool west = false;    // Left   (X / Square)
    bool north = false;   // Top    (Y / Triangle)

    bool l1 = false;
    bool r1 = false;
    uint8_t trigger_l2 = 0;
    uint8_t trigger_r2 = 0;
    bool l3 = false;
    bool r3 = false;

    bool select = false;
    bool start = false;
    bool guide = false;
    bool share = false;
    bool options = false;
    bool center = false;

    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;

    int16_t axis_left_x = 0;
    int16_t axis_left_y = 0;
    int16_t axis_right_x = 0;
    int16_t axis_right_y = 0;

    bool operator==(const GamepadState& other) const{
        return connected == other.connected &&
               south == other.south && east == other.east && west == other.west && north == other.north &&
               l1 == other.l1 && r1 == other.r1 &&
               trigger_l2 == other.trigger_l2 && trigger_r2 == other.trigger_r2 &&
               l3 == other.l3 && r3 == other.r3 &&
               select == other.select && start == other.start && guide == other.guide &&
               share == other.share && options == other.options && center == other.center &&
               dpad_up == other.dpad_up && dpad_down == other.dpad_down && dpad_left == other.dpad_left && dpad_right == other.dpad_right &&
               axis_left_x == other.axis_left_x && axis_left_y == other.axis_left_y &&
               axis_right_x == other.axis_right_x && axis_right_y == other.axis_right_y;
    }

    bool operator!=(const GamepadState& other) const{ return !(*this == other); }

    bool is_neutral() const{
        return !south && !east && !west && !north &&
               !l1 && !r1 && trigger_l2 == 0 && trigger_r2 == 0 &&
               !l3 && !r3 &&
               !select && !start && !guide && !share && !options && !center &&
               !dpad_up && !dpad_down && !dpad_left && !dpad_right &&
               axis_left_x == 0 && axis_left_y == 0 &&
               axis_right_x == 0 && axis_right_y == 0;
    }
};

}

Q_DECLARE_METATYPE(PokemonAutomation::GamepadInput::GamepadState);

namespace{

using GamepadInput::GamepadState;
using GamepadInput::DeviceInfo;

constexpr int TRIGGER_THRESHOLD = 64;
constexpr int MAX_GAMEPADS = 4;

uint8_t axis_to_switch(int16_t axis){
    int value = axis + 128;
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool is_pro_controller_type(ControllerType type){
    switch (type){
    case ControllerType::NintendoSwitch_WiredController:
    case ControllerType::NintendoSwitch_WiredProController:
    case ControllerType::NintendoSwitch_WirelessProController:
    case ControllerType::NintendoSwitch2_WiredController:
    case ControllerType::NintendoSwitch2_WiredProController:
    case ControllerType::NintendoSwitch2_WirelessProController:
        return true;
    default:
        return false;
    }
}

NintendoSwitch::DpadPosition compose_dpad(const GamepadState& state){
    int dx = (state.dpad_right ? 1 : 0) - (state.dpad_left ? 1 : 0);
    int dy = (state.dpad_down ? 1 : 0) - (state.dpad_up ? 1 : 0);
    if (dx == 0 && dy == 0){
        return NintendoSwitch::DpadPosition::DPAD_NONE;
    }
    if (dx == 0){
        return dy > 0
            ? NintendoSwitch::DpadPosition::DPAD_DOWN
            : NintendoSwitch::DpadPosition::DPAD_UP;
    }
    if (dy == 0){
        return dx > 0
            ? NintendoSwitch::DpadPosition::DPAD_RIGHT
            : NintendoSwitch::DpadPosition::DPAD_LEFT;
    }
    if (dx > 0 && dy > 0) return NintendoSwitch::DpadPosition::DPAD_DOWN_RIGHT;
    if (dx > 0 && dy < 0) return NintendoSwitch::DpadPosition::DPAD_UP_RIGHT;
    if (dx < 0 && dy > 0) return NintendoSwitch::DpadPosition::DPAD_DOWN_LEFT;
    return NintendoSwitch::DpadPosition::DPAD_UP_LEFT;
}

bool apply_to_pro_state(const GamepadState& source, NintendoSwitch::ProControllerState& dest){
    dest.clear();
    if (!source.connected){
        return true;
    }

    if (source.south) dest.buttons |= NintendoSwitch::Button::BUTTON_B;
    if (source.east)  dest.buttons |= NintendoSwitch::Button::BUTTON_A;
    if (source.west)  dest.buttons |= NintendoSwitch::Button::BUTTON_Y;
    if (source.north) dest.buttons |= NintendoSwitch::Button::BUTTON_X;

    if (source.l1) dest.buttons |= NintendoSwitch::Button::BUTTON_L;
    if (source.r1) dest.buttons |= NintendoSwitch::Button::BUTTON_R;
    if (source.trigger_l2 >= TRIGGER_THRESHOLD) dest.buttons |= NintendoSwitch::Button::BUTTON_ZL;
    if (source.trigger_r2 >= TRIGGER_THRESHOLD) dest.buttons |= NintendoSwitch::Button::BUTTON_ZR;

    if (source.l3) dest.buttons |= NintendoSwitch::Button::BUTTON_LCLICK;
    if (source.r3) dest.buttons |= NintendoSwitch::Button::BUTTON_RCLICK;

    if (source.select)  dest.buttons |= NintendoSwitch::Button::BUTTON_MINUS;
    if (source.start)   dest.buttons |= NintendoSwitch::Button::BUTTON_PLUS;
    if (source.options) dest.buttons |= NintendoSwitch::Button::BUTTON_PLUS;
    if (source.share)   dest.buttons |= NintendoSwitch::Button::BUTTON_CAPTURE;
    if (source.guide)   dest.buttons |= NintendoSwitch::Button::BUTTON_HOME;
    if (source.center)  dest.buttons |= NintendoSwitch::Button::BUTTON_CAPTURE;

    if (source.dpad_up)    dest.buttons |= NintendoSwitch::Button::BUTTON_UP;
    if (source.dpad_down)  dest.buttons |= NintendoSwitch::Button::BUTTON_DOWN;
    if (source.dpad_left)  dest.buttons |= NintendoSwitch::Button::BUTTON_LEFT;
    if (source.dpad_right) dest.buttons |= NintendoSwitch::Button::BUTTON_RIGHT;

    dest.dpad = compose_dpad(source);

    dest.left_x  = axis_to_switch(source.axis_left_x);
    dest.left_y  = axis_to_switch(-source.axis_left_y);
    dest.right_x = axis_to_switch(source.axis_right_x);
    dest.right_y = axis_to_switch(-source.axis_right_y);

    return dest.is_neutral();
}

}


#if defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>

namespace GamepadInput{

namespace{

std::string fallback_device_name(int deviceId){
    return "Controller " + std::to_string(deviceId + 1);
}

std::string subtype_to_name(BYTE subtype){
    switch (subtype){
    case XINPUT_DEVSUBTYPE_GAMEPAD:             return "Xbox Controller";
    case XINPUT_DEVSUBTYPE_WHEEL:               return "Racing Wheel";
    case XINPUT_DEVSUBTYPE_ARCADE_STICK:        return "Arcade Stick";
    case XINPUT_DEVSUBTYPE_ARCADE_PAD:          return "Arcade Pad";
    case XINPUT_DEVSUBTYPE_FLIGHT_STICK:        return "Flight Stick";
    case XINPUT_DEVSUBTYPE_DANCE_PAD:           return "Dance Pad";
    case XINPUT_DEVSUBTYPE_GUITAR:              return "Guitar";
    case XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE:    return "Guitar (Alt)";
    case XINPUT_DEVSUBTYPE_GUITAR_BASS:         return "Bass Guitar";
    case XINPUT_DEVSUBTYPE_DRUM_KIT:            return "Drum Kit";
#if defined(XINPUT_DEVSUBTYPE_NAVIGATION_CONTROLLER)
    case XINPUT_DEVSUBTYPE_NAVIGATION_CONTROLLER: return "Navigation Controller";
#endif
#if defined(XINPUT_DEVSUBTYPE_UNKNOWN)
    case XINPUT_DEVSUBTYPE_UNKNOWN:             return "Gamepad";
#endif
    default:
        return "Gamepad";
    }
}

bool query_state(int deviceId, XINPUT_STATE& state){
    std::memset(&state, 0, sizeof(state));
    DWORD result = XInputGetState(deviceId, &state);
    return result == ERROR_SUCCESS;
}

bool query_device_info(int deviceId, DeviceInfo& info){
    XINPUT_CAPABILITIES caps;
    std::memset(&caps, 0, sizeof(caps));
    DWORD result = XInputGetCapabilities(deviceId, 0, &caps);
    if (result != ERROR_SUCCESS){
        return false;
    }
    info.id = deviceId;
    std::string base = subtype_to_name(caps.SubType);
    info.name = base.empty() ? fallback_device_name(deviceId) : base + " #" + std::to_string(deviceId + 1);
    info.manufacturer = "XInput";
    return true;
}

int16_t convert_thumb_axis(int16_t value, int deadzone){
    if (value >= 0){
        if (value <= deadzone){
            return 0;
        }
        value -= deadzone;
    }else{
        if (-value <= deadzone){
            return 0;
        }
        value += deadzone;
    }

    constexpr int maxMagnitude = 32767;
    double normalized = static_cast<double>(value) / static_cast<double>(maxMagnitude - deadzone);
    normalized = std::clamp(normalized, -1.0, 1.0);
    long scaled = std::lround(normalized * 128.0);
    return static_cast<int16_t>(std::clamp(scaled, -128l, 128l));
}

uint8_t convert_trigger(uint8_t value){
    return value;
}

GamepadState translate_state(const XINPUT_STATE& xstate){
    GamepadState state;
    state.connected = true;

    WORD buttons = xstate.Gamepad.wButtons;

    state.south = buttons & XINPUT_GAMEPAD_A;
    state.east  = buttons & XINPUT_GAMEPAD_B;
    state.west  = buttons & XINPUT_GAMEPAD_X;
    state.north = buttons & XINPUT_GAMEPAD_Y;

    state.l1 = buttons & XINPUT_GAMEPAD_LEFT_SHOULDER;
    state.r1 = buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER;
    state.trigger_l2 = convert_trigger(xstate.Gamepad.bLeftTrigger);
    state.trigger_r2 = convert_trigger(xstate.Gamepad.bRightTrigger);
    state.l3 = buttons & XINPUT_GAMEPAD_LEFT_THUMB;
    state.r3 = buttons & XINPUT_GAMEPAD_RIGHT_THUMB;

    state.select = buttons & XINPUT_GAMEPAD_BACK;
    state.start  = buttons & XINPUT_GAMEPAD_START;
    state.share = state.select;
    state.options = state.start;
#ifdef XINPUT_GAMEPAD_GUIDE
    state.guide = buttons & XINPUT_GAMEPAD_GUIDE;
    state.center = state.guide;
#else
    state.guide = false;
    state.center = false;
#endif

    state.dpad_up    = buttons & XINPUT_GAMEPAD_DPAD_UP;
    state.dpad_down  = buttons & XINPUT_GAMEPAD_DPAD_DOWN;
    state.dpad_left  = buttons & XINPUT_GAMEPAD_DPAD_LEFT;
    state.dpad_right = buttons & XINPUT_GAMEPAD_DPAD_RIGHT;

    state.axis_left_x  = convert_thumb_axis(xstate.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    state.axis_left_y  = convert_thumb_axis(xstate.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    state.axis_right_x = convert_thumb_axis(xstate.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    state.axis_right_y = convert_thumb_axis(xstate.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

    return state;
}
}

class DeviceMonitor::Impl{
public:
    explicit Impl(DeviceMonitor& owner)
        : m_owner(owner)
        , m_timer(new QTimer(&owner))
    {
        m_timer->setInterval(500);
        QObject::connect(m_timer.get(), &QTimer::timeout, &owner, [this]{ poll(); });
        poll();
        m_timer->start();
    }

    std::vector<DeviceInfo> devices() const{
        QReadLocker lock(&m_lock);
        std::vector<DeviceInfo> ret;
        ret.reserve(m_devices.size());
        for (const auto& entry : m_devices){
            ret.push_back(entry.second);
        }
        std::sort(ret.begin(), ret.end(), [](const DeviceInfo& a, const DeviceInfo& b){
            if (a.name == b.name){
                return a.id < b.id;
            }
            return a.name < b.name;
        });
        return ret;
    }

    std::string lookup_name(int deviceId) const{
        QReadLocker lock(&m_lock);
        auto iter = m_devices.find(deviceId);
        if (iter != m_devices.end()){
            return iter->second.name;
        }
        return fallback_device_name(deviceId);
    }

private:
    void poll(){
        bool changed = false;
        for (int id = 0; id < MAX_GAMEPADS; id++){
            DeviceInfo info;
            bool connected = query_device_info(id, info);
            QWriteLocker lock(&m_lock);
            if (connected){
                auto iter = m_devices.find(id);
                if (iter == m_devices.end() || iter->second.name != info.name || iter->second.manufacturer != info.manufacturer){
                    m_devices[id] = std::move(info);
                    changed = true;
                }
            }else{
                if (m_devices.erase(id) > 0){
                    changed = true;
                }
            }
        }
        if (changed){
            emit m_owner.devicesChanged();
        }
    }

private:
    DeviceMonitor& m_owner;
    mutable QReadWriteLock m_lock;
    std::map<int, DeviceInfo> m_devices;
    std::unique_ptr<QTimer> m_timer;
};


class GamepadInputDevice : public QObject{
    Q_OBJECT
public:
    GamepadInputDevice(int deviceId, QObject* parent)
        : QObject(parent)
        , m_deviceId(deviceId)
        , m_timer(new QTimer(this))
    {
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(8);
        connect(m_timer, &QTimer::timeout, this, &GamepadInputDevice::poll);
    }

    void start(){
        if (!m_timer->isActive()){
            poll();
            m_timer->start();
        }
    }

    void stop(){
        if (m_timer->isActive()){
            m_timer->stop();
        }
        GamepadState neutral;
        neutral.connected = false;
        if (neutral != m_lastState){
            m_lastState = neutral;
            emit stateUpdated(m_lastState);
        }
        if (m_connected.exchange(false, std::memory_order_acq_rel)){
            emit connectionStateChanged(false);
        }
    }

signals:
    void stateUpdated(const PokemonAutomation::GamepadInput::GamepadState& state);
    void connectionStateChanged(bool connected);

private slots:
    void poll(){
        XINPUT_STATE xstate;
        bool connected = query_state(m_deviceId, xstate);
        bool previous = m_connected.exchange(connected, std::memory_order_acq_rel);
        if (connected != previous){
            emit connectionStateChanged(connected);
        }

        GamepadState next = connected ? translate_state(xstate) : GamepadState{};
        constexpr int HEARTBEAT_TICKS = 25;
        if (next != m_lastState){
            m_lastState = next;
            m_unchanged_ticks = 0;
            emit stateUpdated(m_lastState);
        }else{
            if (connected){
                m_unchanged_ticks++;
                if (m_unchanged_ticks >= HEARTBEAT_TICKS){
                    m_unchanged_ticks = 0;
                    emit stateUpdated(m_lastState);
                }
            }
        }
    }

private:
    int m_deviceId;
    QTimer* m_timer;
    std::atomic<bool> m_connected{false};
    GamepadState m_lastState;
    int m_unchanged_ticks = 0;
};


class GamepadCommandProcessor{
public:
    GamepadCommandProcessor(Logger& logger, ControllerSession& session)
        : m_logger(logger)
        , m_session(session)
        , m_thread(&GamepadCommandProcessor::worker_loop, this)
    {}

    ~GamepadCommandProcessor(){
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_one();
        if (m_thread.joinable()){
            m_thread.join();
        }
    }

    void submit_state(const GamepadState& state){
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_hasPending && state == m_pending){
                return;
            }
            m_pending = state;
            m_hasPending = true;
        }
        m_cv.notify_one();
    }

    void set_active(bool active){
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_active == active){
                return;
            }
            m_active = active;
            m_activeChanged = true;
        }
        m_cv.notify_one();
    }

private:
    void worker_loop(){
        while (true){
            GamepadState state;
            bool hasState = false;
            bool active = false;
            bool activeChanged = false;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]{ return m_stop || m_hasPending || m_activeChanged; });
                if (m_stop){
                    break;
                }
                if (m_hasPending){
                    state = m_pending;
                    hasState = true;
                    m_hasPending = false;
                }
                active = m_active;
                activeChanged = m_activeChanged;
                m_activeChanged = false;
            }

            if (!active){
                if (activeChanged || !m_lastNeutral){
                    ensure_cancel();
                }
                m_lastNeutral = true;
                continue;
            }

            if (!hasState){
                continue;
            }

            process_state(state);
        }

        ensure_cancel();
    }

    void ensure_cancel(){
        if (m_lastNeutral){
            return;
        }
        if (!is_pro_controller_type(m_session.controller_type())){
            m_lastNeutral = true;
            return;
        }
        m_session.try_run<::PokemonAutomation::NintendoSwitch::ProController>([](auto& controller){
            controller.cancel_all_commands();
        });
        m_lastNeutral = true;
    }

    void log_once(const std::string& message){
        if (!m_lastError || *m_lastError != message){
            m_logger.log(message, COLOR_RED);
            m_lastError = message;
        }
    }

    void process_state(const GamepadState& state){
        ControllerType type = m_session.controller_type();
        if (!is_pro_controller_type(type)){
            if (!m_warnedUnsupported.has_value() || *m_warnedUnsupported != type){
                m_logger.log("Gamepad input is currently supported only for Nintendo Switch Pro Controller profiles.", COLOR_RED);
                m_warnedUnsupported = type;
            }
            ensure_cancel();
            return;
        }
        m_warnedUnsupported.reset();

        if (!m_session.ready()){
            ensure_cancel();
            return;
        }

        std::string block = m_session.user_input_blocked();
        if (!block.empty()){
            ensure_cancel();
            return;
        }

        if (!state.connected){
            ensure_cancel();
            return;
        }

        NintendoSwitch::ProControllerState new_state;
        bool neutral = apply_to_pro_state(state, new_state);
        if (neutral){
            ensure_cancel();
            return;
        }

        auto error = m_session.try_run<::PokemonAutomation::NintendoSwitch::ProController>([&](auto& controller){
            if (!m_lastNeutral && !(new_state == m_lastProState)){
                controller.replace_on_next_command();
            }
            Milliseconds ticksize = controller.ticksize();
            Milliseconds duration = ticksize == Milliseconds::zero() ? 2000ms : ticksize * 255;
            controller.issue_full_controller_state(
                nullptr,
                duration,
                new_state.buttons,
                new_state.dpad,
                new_state.left_x,
                new_state.left_y,
                new_state.right_x,
                new_state.right_y
            );
        });

        if (!error.empty()){
            log_once("Gamepad command failed: " + error);
            ensure_cancel();
            return;
        }
        m_lastError.reset();

        m_lastProState = new_state;
        m_lastNeutral = false;
    }

private:
    Logger& m_logger;
    ControllerSession& m_session;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    bool m_active = false;
    bool m_activeChanged = false;
    bool m_hasPending = false;
    GamepadState m_pending;

    std::thread m_thread;

    bool m_lastNeutral = true;
    NintendoSwitch::ProControllerState m_lastProState;
    std::optional<std::string> m_lastError;
    std::optional<ControllerType> m_warnedUnsupported;
};


class GamepadController::Impl{
public:
    Impl(GamepadController& owner, Logger& logger, ControllerSession& session)
        : m_owner(owner)
        , m_logger(logger)
        , m_session(session)
        , m_processor(logger, session)
    {
        qRegisterMetaType<GamepadState>("PokemonAutomation::GamepadInput::GamepadState");
    }

    ~Impl(){
        if (m_device){
            m_device->stop();
            delete m_device;
        }
    }

    void set_device(std::optional<int> deviceId){
        if (m_deviceId == deviceId){
            return;
        }

        if (m_device){
            m_device->stop();
            delete m_device;
            m_device = nullptr;
        }

        m_deviceId = deviceId;
        m_deviceConnected.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (deviceId.has_value()){
                m_deviceName = QString::fromStdString(DeviceMonitor::instance().device_name(*deviceId));
            }else{
                m_deviceName.clear();
            }
        }

        if (deviceId.has_value()){
            m_device = new GamepadInputDevice(*deviceId, &m_owner);
            QObject::connect(
                m_device, &GamepadInputDevice::stateUpdated,
                &m_owner, [this](const GamepadState& state){ m_processor.submit_state(state); }
            );
            QObject::connect(
                m_device, &GamepadInputDevice::connectionStateChanged,
                &m_owner, [this](bool connected){ handle_connection_changed(connected); }
            );

            if (m_enabledRequest.load(std::memory_order_acquire)){
                m_device->start();
            }
        }

        update_active_state();
        emit m_owner.statusChanged();
    }

    std::optional<int> device() const{
        return m_deviceId;
    }

    void set_enabled(bool enabled){
        bool previous = m_enabledRequest.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled){
            update_active_state();
            return;
        }

        if (m_device){
            if (enabled){
                m_device->start();
            }else{
                m_device->stop();
            }
        }

        update_active_state();
        emit m_owner.statusChanged();
    }

    bool enabled() const{
        return m_enabledRequest.load(std::memory_order_acquire);
    }

    void handle_connection_changed(bool connected){
        m_deviceConnected.store(connected, std::memory_order_release);
        update_active_state();
        emit m_owner.statusChanged();
    }

    GamepadStatus status() const{
        GamepadStatus status;
        status.enabled_request = m_enabledRequest.load(std::memory_order_acquire);
        status.device_selected = m_deviceId.has_value();
        status.device_connected = m_deviceConnected.load(std::memory_order_acquire);
        status.session_ready = m_session.ready();
        status.block_reason = m_session.user_input_blocked();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            status.device_name = m_deviceName.toStdString();
        }
        status.active = status.enabled_request && status.device_selected && status.device_connected && status.session_ready && status.block_reason.empty();
        return status;
    }

    void refresh(){
        update_active_state();
    }

private:
    void update_active_state(){
        bool should_active = m_enabledRequest.load(std::memory_order_acquire)
            && m_deviceId.has_value()
            && m_deviceConnected.load(std::memory_order_acquire)
            && m_session.ready();

        m_processor.set_active(should_active);
    }

private:
    GamepadController& m_owner;
    Logger& m_logger;
    ControllerSession& m_session;

    GamepadCommandProcessor m_processor;

    std::optional<int> m_deviceId;
    GamepadInputDevice* m_device = nullptr;
    std::atomic<bool> m_enabledRequest{false};
    std::atomic<bool> m_deviceConnected{false};

    mutable std::mutex m_mutex;
    QString m_deviceName;
};


DeviceMonitor::~DeviceMonitor() = default;
DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(*this))
{}

DeviceMonitor& DeviceMonitor::instance(){
    static DeviceMonitor* monitor = []() -> DeviceMonitor*{
        QObject* parent = QCoreApplication::instance();
        return new DeviceMonitor(parent);
    }();
    return *monitor;
}

std::vector<DeviceInfo> DeviceMonitor::devices() const{
    return m_impl->devices();
}

std::string DeviceMonitor::device_name(int device_id) const{
    return m_impl->lookup_name(device_id);
}


GamepadController::GamepadController(Logger& logger, ControllerSession& session, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(*this, logger, session))
{}

GamepadController::~GamepadController() = default;

void GamepadController::set_device(std::optional<int> device_id){
    m_impl->set_device(std::move(device_id));
}

std::optional<int> GamepadController::device() const{
    return m_impl->device();
}

void GamepadController::set_enabled(bool enabled){
    m_impl->set_enabled(enabled);
}

bool GamepadController::enabled() const{
    return m_impl->enabled();
}

GamepadStatus GamepadController::status() const{
    return m_impl->status();
}


bool is_gamepad_supported(){
    return true;
}


}
}

#include "Controllers/GamepadInput/GamepadInput.moc"

#else

#include <QCoreApplication>
#include <QMetaObject>

namespace PokemonAutomation{
namespace GamepadInput{

namespace{
std::string fallback_device_name(int deviceId){
    return "Controller " + std::to_string(deviceId + 1);
}
}

class DeviceMonitor::Impl{
public:
    std::vector<DeviceInfo> devices() const{ return {}; }

    std::string lookup_name(int deviceId) const{
        return fallback_device_name(deviceId);
    }
};

DeviceMonitor::~DeviceMonitor() = default;
DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{}

DeviceMonitor& DeviceMonitor::instance(){
    static DeviceMonitor* monitor = []() -> DeviceMonitor*{
        QObject* parent = QCoreApplication::instance();
        return new DeviceMonitor(parent);
    }();
    return *monitor;
}

std::vector<DeviceInfo> DeviceMonitor::devices() const{
    return m_impl->devices();
}

std::string DeviceMonitor::device_name(int device_id) const{
    return m_impl->lookup_name(device_id);
}


class GamepadController::Impl{
public:
    Impl(GamepadController& owner, [[maybe_unused]] Logger& logger, [[maybe_unused]] ControllerSession& session)
        : m_owner(owner)
    {}

    void set_device(std::optional<int> device_id){
        if (m_deviceId == device_id){
            return;
        }
        m_deviceId = std::move(device_id);
        emit_status_changed();
    }

    std::optional<int> device() const{
        return m_deviceId;
    }

    void set_enabled(bool enabled){
        if (m_enabled == enabled){
            return;
        }
        m_enabled = enabled;
        emit_status_changed();
    }

    bool enabled() const{
        return m_enabled;
    }

    GamepadStatus status() const{
        GamepadStatus status;
        status.enabled_request = m_enabled;
        status.device_selected = m_deviceId.has_value();
        status.session_ready = false;
        status.block_reason = "Controller support unavailable on this platform.";
        return status;
    }

    void refresh(){
        emit_status_changed();
    }

private:
    void emit_status_changed() const{
        QMetaObject::invokeMethod(&m_owner, &GamepadController::statusChanged);
    }

private:
    GamepadController& m_owner;
    std::optional<int> m_deviceId;
    bool m_enabled = false;
};

GamepadController::GamepadController(Logger& logger, ControllerSession& session, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(*this, logger, session))
{}

GamepadController::~GamepadController() = default;

void GamepadController::set_device(std::optional<int> device_id){
    m_impl->set_device(std::move(device_id));
}

std::optional<int> GamepadController::device() const{
    return m_impl->device();
}

void GamepadController::set_enabled(bool enabled){
    m_impl->set_enabled(enabled);
}

bool GamepadController::enabled() const{
    return m_impl->enabled();
}

GamepadStatus GamepadController::status() const{
    return m_impl->status();
}

bool is_gamepad_supported(){
    return false;
}

}
}

#endif
