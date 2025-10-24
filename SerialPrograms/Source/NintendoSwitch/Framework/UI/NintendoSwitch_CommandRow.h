/*  Command Row
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_NintendoSwitch_CommandRow_H
#define PokemonAutomation_NintendoSwitch_CommandRow_H

#include <memory>
#include <optional>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include "Common/Qt/CheckboxDropdown.h"
#include "CommonFramework/Globals.h"
#include "CommonFramework/VideoPipeline/VideoOverlaySession.h"
#include "Controllers/ControllerSession.h"
#include "Controllers/GamepadInput/GamepadInput.h"
#include "NintendoSwitch/Options/NintendoSwitch_ModelType.h"

namespace PokemonAutomation{
namespace NintendoSwitch{


// UI that shows the checkerboxes to control whether to show video overlay elements.
// e.g. checkerbox to toggle on/off overlay boxes
class CommandRow :
    public QWidget,
    public VideoOverlaySession::ContentListener,
    public ControllerSession::Listener
{
    Q_OBJECT

public:
    ~CommandRow();
    CommandRow(
        QWidget& parent,
        ControllerSession& controller,
        VideoOverlaySession& session,
        ConsoleModelCell& console_type,
        bool allow_commands_while_running
    );

    void on_key_press(const QKeyEvent& key);
    void on_key_release(const QKeyEvent& key);

signals:
    void load_profile();
    void save_profile();
    void screenshot_requested();
    void video_requested();

public:
    void set_focus(bool focused);
    void update_ui();
    void on_state_changed(ProgramState state);

private:
    enum class InputSource{
        Keyboard,
        Gamepad
    };

    void refresh_input_mode_ui();
    void refresh_gamepad_devices();
    void sync_gamepad_activation();
    std::string keyboard_status_text() const;
    std::string gamepad_status_text(const GamepadInput::GamepadStatus& status) const;

    virtual void on_overlay_enabled_stats  (bool enabled) override;
    virtual void on_overlay_enabled_boxes  (bool enabled) override;
    virtual void on_overlay_enabled_text   (bool enabled) override;
    virtual void on_overlay_enabled_images (bool enabled) override;
    virtual void on_overlay_enabled_log    (bool enabled) override;
    virtual void ready_changed(bool ready) override;

private:
    ControllerSession& m_controller;
    VideoOverlaySession& m_session;
    bool m_allow_commands_while_running;
    QComboBox* m_command_box = nullptr;
    QLabel* m_status = nullptr;
    QComboBox* m_input_source_dropdown = nullptr;
    QComboBox* m_gamepad_dropdown = nullptr;

    CheckboxDropdownItem* m_overlay_stats = nullptr;
    CheckboxDropdownItem* m_overlay_boxes = nullptr;
    CheckboxDropdownItem* m_overlay_text = nullptr;
    CheckboxDropdownItem* m_overlay_images = nullptr;
    CheckboxDropdownItem* m_overlay_log = nullptr;

    QPushButton* m_load_profile_button = nullptr;
    QPushButton* m_save_profile_button = nullptr;
    QPushButton* m_screenshot_button = nullptr;
    QPushButton* m_video_button = nullptr;
    bool m_last_known_focus;
    ProgramState m_last_known_state;

    InputSource m_input_source = InputSource::Keyboard;
    std::optional<int> m_selected_gamepad;
    GamepadInput::DeviceMonitor& m_device_monitor;
    QMetaObject::Connection m_device_monitor_connection;
    std::unique_ptr<GamepadInput::GamepadController> m_gamepad_controller;
    bool m_gamepad_supported = false;
};


}
}
#endif
