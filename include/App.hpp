#pragma once

#include <pu/Plutonium>

#include <string>
#include <vector>
#include <memory>

enum class Page
{
    Home,
    Browser,
    Send,
    About,
    Receive
};

class MainApplication : public pu::ui::Application
{
private:
    pu::ui::Layout::Ref layout;

    Page current_page = Page::Home;
    Page receive_return_page = Page::Home;

    std::vector<std::string> selected_files;

    std::string browser_path = "sdmc:/";

    bool send_screen_active = false;
    bool send_transfer_ui = false;

    /*
     * -------------------------------------------------------------------------
     * Sender UI
     * -------------------------------------------------------------------------
     */

    pu::ui::elm::TextBlock::Ref send_selected_count;
    pu::ui::elm::TextBlock::Ref send_choose_device;

    pu::ui::elm::TextBlock::Ref transfer_title;
    pu::ui::elm::TextBlock::Ref transfer_file;
    pu::ui::elm::Rectangle::Ref transfer_progress_bg;
    pu::ui::elm::Rectangle::Ref transfer_progress;
    pu::ui::elm::TextBlock::Ref transfer_percent;
    pu::ui::elm::Button::Ref transfer_cancel;

    pu::ui::elm::Menu::Ref send_menu;

    std::shared_ptr<std::vector<size_t>> send_device_indices;

    size_t send_last_device_count = 0;

    pu::ui::elm::MenuItem::Ref send_status_item;

    pu::ui::elm::TextBlock::Ref send_transfer_status;
    pu::ui::elm::TextBlock::Ref send_transfer_file;
    pu::ui::elm::TextBlock::Ref send_transfer_progress;

    /*
     * -------------------------------------------------------------------------
     * Receiver UI
     * -------------------------------------------------------------------------
     */

    bool receive_transfer_ui = false;

    pu::ui::elm::TextBlock::Ref receive_title;
    pu::ui::elm::TextBlock::Ref receive_file;
    pu::ui::elm::TextBlock::Ref receive_details;

    pu::ui::elm::Rectangle::Ref receive_progress_bg;
    pu::ui::elm::Rectangle::Ref receive_progress;

    pu::ui::elm::TextBlock::Ref receive_percent;

    pu::ui::elm::Button::Ref receive_accept;
    pu::ui::elm::Button::Ref receive_decline;
    pu::ui::elm::Button::Ref receive_cancel;

    void StartNetwork();
    void StopNetwork();

    void ShowHome();
    void ShowBrowser();
    void ShowSend();
    void ShowAbout();
    void ShowReceive();

    void ToggleFileSelection(
        const std::string &path
    );

    bool IsFileSelected(
        const std::string &path
    ) const;

public:
    using Application::Application;

    PU_SMART_CTOR(MainApplication)

    ~MainApplication() override;

    void OnLoad() override;
};