/*  Command Row
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <limits>
#include <string>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include "Common/Qt/NoWheelComboBox.h"
#include "Common/Qt/Options/ConfigWidget.h"
#include "CommonFramework/GlobalSettingsPanel.h"
#include "CommonFramework/Options/Environment/ThemeSelectorOption.h"
#include "CommonFramework/Panels/ConsoleSettingsStretch.h"
#include "CommonFramework/Recording/StreamHistoryOption.h"
#include "NintendoSwitch_CommandRow.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{
namespace NintendoSwitch{




CommandRow::~CommandRow(){
    if (m_device_monitor_connection){
        QObject::disconnect(m_device_monitor_connection);
    }
    m_controller.remove_listener(*this);
    m_session.remove_listener(*this);
}
CommandRow::CommandRow(
    QWidget& parent,
    ControllerSession& controller,
    VideoOverlaySession& session,
    ConsoleModelCell& console_type,
    bool allow_commands_while_running
)
    : QWidget(&parent)
    , m_controller(controller)
    , m_session(session)
    , m_allow_commands_while_running(allow_commands_while_running)
    , m_device_monitor(GamepadInput::DeviceMonitor::instance())
    , m_last_known_focus(false)
    , m_last_known_state(ProgramState::STOPPED)
    , m_gamepad_supported(GamepadInput::is_gamepad_supported())
{
    QHBoxLayout* layout0 = new QHBoxLayout(this);
    layout0->setContentsMargins(0, 0, 0, 0);

    layout0->addWidget(new QLabel("<b>Console Type:</b>", this), CONSOLE_SETTINGS_STRETCH_L0_LABEL);

    QHBoxLayout* layout1 = new QHBoxLayout();
    layout0->addLayout(layout1, CONSOLE_SETTINGS_STRETCH_L0_RIGHT);
    layout1->setContentsMargins(0, 0, 0, 0);

    ConfigWidget* console_type_box = console_type.make_QtWidget(*this);
    layout1->addWidget(&console_type_box->widget());



    layout1->addStretch(100);

    layout1->addWidget(new QLabel("<b>Input:</b>", this));

    m_input_source_dropdown = new NoWheelComboBox(this);
    m_input_source_dropdown->addItem("Keyboard");
    if (m_gamepad_supported){
        m_input_source_dropdown->addItem("Controller");
    }else{
        m_input_source_dropdown->setToolTip("Controller support is unavailable in this build.");
    }
    m_input_source_dropdown->setCurrentIndex(0);
    layout1->addWidget(m_input_source_dropdown);

    m_gamepad_dropdown = new NoWheelComboBox(this);
    m_gamepad_dropdown->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_gamepad_dropdown->setEnabled(false);
    m_gamepad_dropdown->setVisible(m_gamepad_supported);
    layout1->addWidget(m_gamepad_dropdown);

    m_status = new QLabel(this);
    layout1->addWidget(m_status);
    layout1->addSpacing(5);

    CheckboxDropdown* overlays = new CheckboxDropdown(this, "Overlays");
    overlays->setMinimumWidth(80);
    {
        m_overlay_stats = overlays->addItem("Stats");
        m_overlay_stats->setChecked(session.enabled_stats());
    }
    {
        m_overlay_boxes = overlays->addItem("Boxes");
        m_overlay_boxes->setChecked(session.enabled_boxes());
    }
    if (PreloadSettings::instance().DEVELOPER_MODE){
        m_overlay_text = overlays->addItem("Text");
        m_overlay_text->setChecked(session.enabled_text());
    }
    if (PreloadSettings::instance().DEVELOPER_MODE){
        m_overlay_images = overlays->addItem("Masks");
        m_overlay_images->setChecked(session.enabled_images());
    }
    {
        m_overlay_log = overlays->addItem("Log");
        m_overlay_log->setChecked(session.enabled_log());
    }
    layout1->addWidget(overlays);

    m_load_profile_button = new QPushButton("Load Profile", this);
    layout1->addWidget(m_load_profile_button, 2);

    m_save_profile_button = new QPushButton("Save Profile", this);
    layout1->addWidget(m_save_profile_button, 2);

    m_screenshot_button = new QPushButton("Screenshot", this);
    layout1->addWidget(m_screenshot_button, 2);

    m_gamepad_controller = std::make_unique<GamepadInput::GamepadController>(m_controller.logger(), m_controller, this);
    connect(
        m_gamepad_controller.get(), &GamepadInput::GamepadController::statusChanged,
        this, [this]{ update_ui(); }
    );

    if (m_gamepad_supported){
        m_device_monitor_connection = connect(
            &m_device_monitor, &GamepadInput::DeviceMonitor::devicesChanged,
            this, [this]{ refresh_gamepad_devices(); }
        );

        refresh_gamepad_devices();
    }

    connect(
        m_input_source_dropdown, QOverload<int>::of(&QComboBox::activated),
        this, [this](int index){
            InputSource new_source = InputSource::Keyboard;
            if (m_gamepad_supported && index == 1){
                new_source = InputSource::Gamepad;
            }
            if (new_source == m_input_source){
                return;
            }
            m_input_source = new_source;
            refresh_input_mode_ui();
            sync_gamepad_activation();
            update_ui();
        }
    );
    if (m_gamepad_supported){
        connect(
            m_gamepad_dropdown, QOverload<int>::of(&QComboBox::activated),
            this, [this](int index){
                QVariant data = m_gamepad_dropdown->itemData(index);
                if (data.isValid()){
                    m_selected_gamepad = data.toInt();
                }else{
                    m_selected_gamepad.reset();
                }
                if (m_gamepad_controller){
                    m_gamepad_controller->set_device(m_selected_gamepad);
                }
                sync_gamepad_activation();
                update_ui();
            }
        );
    }

#if 1
    if (m_overlay_stats){
        connect(
            m_overlay_stats, &CheckboxDropdownItem::checkStateChanged,
            this, [this](Qt::CheckState state){
                m_session.set_enabled_stats(state == Qt::Checked);
            }
        );
    }
    if (m_overlay_boxes){
        connect(
            m_overlay_boxes, &CheckboxDropdownItem::checkStateChanged,
            this, [this](Qt::CheckState state){

                m_session.set_enabled_boxes(state == Qt::Checked);
            }
        );
    }
    if (m_overlay_text){
        connect(
            m_overlay_text, &CheckboxDropdownItem::checkStateChanged,
            this, [this](Qt::CheckState state){
                m_session.set_enabled_text(state == Qt::Checked);
            }
        );
    }
    if (m_overlay_images){
        connect(
            m_overlay_images, &CheckboxDropdownItem::checkStateChanged,
            this, [this](Qt::CheckState state){
                m_session.set_enabled_images(state == Qt::Checked);
            }
        );
    }
    if (m_overlay_log){
        connect(
            m_overlay_log, &CheckboxDropdownItem::checkStateChanged,
            this, [this](Qt::CheckState state){
                m_session.set_enabled_log(state == Qt::Checked);
            }
        );
    }
#endif

    connect(
        m_load_profile_button, &QPushButton::clicked,
        this, [this](bool) { emit load_profile(); }
    );
    connect(
        m_save_profile_button, &QPushButton::clicked,
        this, [this](bool) { emit save_profile(); }
    );
    connect(
        m_screenshot_button, &QPushButton::clicked,
        this, [this](bool){ emit screenshot_requested(); }
    );

#if (QT_VERSION_MAJOR == 6) && (QT_VERSION_MINOR >= 8)
    if (IS_BETA_VERSION || PreloadSettings::instance().DEVELOPER_MODE){
        m_video_button = new QPushButton("Video Capture", this);
        layout1->addWidget(m_video_button, 2);
        if (GlobalSettings::instance().STREAM_HISTORY->enabled()){
            connect(
                m_video_button, &QPushButton::clicked,
                this, [this](bool){ emit video_requested(); }
            );
        }else{
            m_video_button->setEnabled(false);
            m_video_button->setToolTip("Please turn on Stream History to enable video capture.");
        }
    }
#endif

    refresh_input_mode_ui();
    sync_gamepad_activation();
    update_ui();

    m_session.add_listener(*this);
    m_controller.add_listener(*this);
}

void CommandRow::on_key_press(const QKeyEvent& key){
    if (m_input_source != InputSource::Keyboard){
        return;
    }
    if (!m_last_known_focus){
        m_controller.logger().log("Keyboard Command Suppressed: Not in focus.", COLOR_RED);
        return;
    }
    AbstractController* controller = m_controller.controller();
    if (controller == nullptr){
        m_controller.logger().log("Keyboard Command Suppressed: Controller is null.", COLOR_RED);
        return;
    }
    if (!m_allow_commands_while_running && m_last_known_state != ProgramState::STOPPED){
        m_controller.logger().log("Keyboard Command Suppressed: Program is running.", COLOR_RED);
        return;
    }
    controller->keyboard_press(key);
}
void CommandRow::on_key_release(const QKeyEvent& key){
    if (m_input_source != InputSource::Keyboard){
        return;
    }
    if (!m_last_known_focus){
        return;
    }
    AbstractController* controller = m_controller.controller();
    if (controller == nullptr){
        return;
    }
    controller->keyboard_release(key);
}

void CommandRow::set_focus(bool focused){
    AbstractController* controller = m_controller.controller();
    if (!focused && controller != nullptr && m_input_source == InputSource::Keyboard){
        controller->keyboard_release_all();
    }
    if (m_last_known_focus == focused){
        m_last_known_focus = focused;
        sync_gamepad_activation();
        update_ui();
        return;
    }
    m_last_known_focus = focused;
    sync_gamepad_activation();
    update_ui();
}

void CommandRow::update_ui(){
    bool stopped = m_last_known_state == ProgramState::STOPPED;
    m_load_profile_button->setEnabled(stopped);
    if (!m_gamepad_supported || m_input_source == InputSource::Keyboard){
        m_status->setText(QString::fromStdString(keyboard_status_text()));
    }else{
        GamepadInput::GamepadStatus status = m_gamepad_controller ? m_gamepad_controller->status() : GamepadInput::GamepadStatus{};
        m_status->setText(QString::fromStdString(gamepad_status_text(status)));
    }
}

void CommandRow::on_state_changed(ProgramState state){
    m_last_known_state = state;
    if ((m_allow_commands_while_running || state == ProgramState::STOPPED) && m_input_source == InputSource::Keyboard){
        AbstractController* controller = m_controller.controller();
        if (controller != nullptr){
            controller->keyboard_release_all();
        }
    }
    sync_gamepad_activation();
    update_ui();
}


void CommandRow::on_overlay_enabled_stats(bool enabled){
    QMetaObject::invokeMethod(this, [this, enabled]{
        if (m_overlay_stats){
            m_overlay_stats->setChecked(enabled);
        }
    }, Qt::QueuedConnection);
}
void CommandRow::on_overlay_enabled_boxes(bool enabled){
    QMetaObject::invokeMethod(this, [this, enabled]{
        if (m_overlay_boxes){
            m_overlay_boxes->setChecked(enabled);
        }
    }, Qt::QueuedConnection);
}
void CommandRow::on_overlay_enabled_text(bool enabled){
    QMetaObject::invokeMethod(this, [this, enabled]{
        if (m_overlay_text){
            m_overlay_text->setChecked(enabled);
        }
    }, Qt::QueuedConnection);
}
void CommandRow::on_overlay_enabled_images(bool enabled){
    QMetaObject::invokeMethod(this, [this, enabled]{
        if (m_overlay_images){
            m_overlay_images->setChecked(enabled);
        }
    }, Qt::QueuedConnection);
}
void CommandRow::on_overlay_enabled_log(bool enabled){
    QMetaObject::invokeMethod(this, [this, enabled]{
        if (m_overlay_log){
            m_overlay_log->setChecked(enabled);
        }
    }, Qt::QueuedConnection);
}
void CommandRow::ready_changed(bool ready){
//    cout << "CommandRow::ready_changed(): " << ready << endl;
    QMetaObject::invokeMethod(this, [this]{
        sync_gamepad_activation();
        update_ui();
    }, Qt::QueuedConnection);
}

void CommandRow::refresh_input_mode_ui(){
    if (m_input_source_dropdown == nullptr){
        return;
    }
    m_input_source_dropdown->blockSignals(true);
    int desired_index = 0;
    if (m_gamepad_supported && m_input_source == InputSource::Gamepad){
        desired_index = 1;
    }else{
        m_input_source = InputSource::Keyboard;
    }
    m_input_source_dropdown->setCurrentIndex(desired_index);
    m_input_source_dropdown->blockSignals(false);

    if (m_gamepad_dropdown != nullptr){
        if (!m_gamepad_supported){
            m_gamepad_dropdown->setEnabled(false);
            m_gamepad_dropdown->setVisible(false);
        }else{
            bool has_devices = !m_device_monitor.devices().empty();
            bool using_gamepad = m_input_source == InputSource::Gamepad;
            m_gamepad_dropdown->setEnabled(using_gamepad && has_devices);
            m_gamepad_dropdown->setVisible(true);
        }
    }

    if (m_gamepad_controller){
        if (m_gamepad_supported && m_input_source == InputSource::Gamepad){
            m_gamepad_controller->set_device(m_selected_gamepad);
        }else{
            m_gamepad_controller->set_enabled(false);
        }
    }
}

void CommandRow::refresh_gamepad_devices(){
    if (m_gamepad_dropdown == nullptr){
        return;
    }

    if (!m_gamepad_supported){
        m_gamepad_dropdown->blockSignals(true);
        m_gamepad_dropdown->clear();
        m_gamepad_dropdown->setEnabled(false);
        m_gamepad_dropdown->blockSignals(false);
        return;
    }

    auto devices = m_device_monitor.devices();

    m_gamepad_dropdown->blockSignals(true);
    m_gamepad_dropdown->clear();

    if (devices.empty()){
        m_gamepad_dropdown->addItem("No controllers detected");
        m_gamepad_dropdown->setToolTip("Connect an XInput or PlayStation-compatible controller.");
        m_selected_gamepad.reset();
    }else{
        int index_to_select = -1;
        for (size_t i = 0; i < devices.size(); ++i){
            const GamepadInput::DeviceInfo& info = devices[i];
            std::string display_name = info.name.empty() ? "Controller " + std::to_string(info.id) : info.name;
            QString label = QString::fromStdString(display_name);
            m_gamepad_dropdown->addItem(label, QVariant(info.id));
            if (!info.manufacturer.empty()){
                m_gamepad_dropdown->setItemData(static_cast<int>(i), QString::fromStdString(info.manufacturer), Qt::ToolTipRole);
            }
            if (m_selected_gamepad && info.id == *m_selected_gamepad){
                index_to_select = static_cast<int>(i);
            }
        }
        if (index_to_select < 0){
            index_to_select = 0;
            m_selected_gamepad = devices.front().id;
        }
        m_gamepad_dropdown->setCurrentIndex(index_to_select);
    }

    m_gamepad_dropdown->blockSignals(false);

    if (m_gamepad_controller){
        m_gamepad_controller->set_device(m_selected_gamepad);
    }

    bool using_gamepad = m_input_source == InputSource::Gamepad;
    m_gamepad_dropdown->setEnabled(using_gamepad && !devices.empty());

    sync_gamepad_activation();
    update_ui();
}

void CommandRow::sync_gamepad_activation(){
    if (!m_gamepad_controller){
        return;
    }
    if (!m_gamepad_supported){
        m_gamepad_controller->set_enabled(false);
        return;
    }
    if (m_input_source != InputSource::Gamepad){
        m_gamepad_controller->set_enabled(false);
        return;
    }

    bool should_enable = true;
    if (!m_allow_commands_while_running && m_last_known_state != ProgramState::STOPPED){
        should_enable = false;
    }
    if (!m_controller.ready()){
        should_enable = false;
    }
    if (!m_selected_gamepad.has_value()){
        should_enable = false;
    }
    if (!m_controller.user_input_blocked().empty()){
        should_enable = false;
    }

    m_gamepad_controller->set_enabled(should_enable);
}

std::string CommandRow::keyboard_status_text() const{
    if (!m_allow_commands_while_running && m_last_known_state != ProgramState::STOPPED){
        return "Keyboard: " + html_color_text("&#x2b24;", COLOR_PURPLE) + " Program running.";
    }
    if (!m_controller.ready()){
        return "Keyboard: " + html_color_text("&#x2b24;", COLOR_RED) + " Controller not ready.";
    }
    std::string error = m_controller.user_input_blocked();
    if (!error.empty()){
        return error;
    }
    if (!m_last_known_focus){
        return "Keyboard: " + html_color_text("&#x2b24;", COLOR_PURPLE) + " Panel not focused.";
    }
    return "Keyboard: " + html_color_text("&#x2b24;", COLOR_DARKGREEN) + " Ready.";
}

std::string CommandRow::gamepad_status_text(const GamepadInput::GamepadStatus& status) const{
    const std::string prefix = "Controller: ";

    if (!m_gamepad_supported){
        return prefix + html_color_text("&#x2b24;", COLOR_RED) + " Not supported in this build.";
    }

    if (!m_allow_commands_while_running && m_last_known_state != ProgramState::STOPPED){
        return prefix + html_color_text("&#x2b24;", COLOR_PURPLE) + " Program running.";
    }
    if (!status.device_selected){
        return prefix + html_color_text("&#x2b24;", COLOR_RED) + " Select a controller.";
    }
    if (!status.device_connected){
        std::string name = status.device_name.empty() ? "controller" : status.device_name;
        return prefix + html_color_text("&#x2b24;", COLOR_RED) + " " + name + " disconnected.";
    }
    if (!status.session_ready){
        return prefix + html_color_text("&#x2b24;", COLOR_RED) + " Controller not ready.";
    }
    if (!status.block_reason.empty()){
        return status.block_reason;
    }
    if (!status.enabled_request){
        return prefix + html_color_text("&#x2b24;", COLOR_PURPLE) + " Waiting for activation.";
    }
    std::string name = status.device_name.empty() ? "controller" : status.device_name;
    if (!status.active){
        std::string suffix = m_last_known_focus ? " ready." : " ready (background).";
        return prefix + html_color_text("&#x2b24;", COLOR_PURPLE) + " " + name + suffix;
    }
    std::string suffix = m_last_known_focus ? " active." : " active (background).";
    return prefix + html_color_text("&#x2b24;", COLOR_DARKGREEN) + " " + name + suffix;
}




}
}













