#include "App.hpp"
#include "Theme.hpp"

#include <dirent.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "localsend.h"
#include "transfer.h"
#include "sender.h"

#include <switch.h>

using namespace pu::ui;
using namespace pu::ui::elm;

namespace
{
constexpr s32 DESIGN_W = 1280;
constexpr s32 DESIGN_H = 720;
constexpr float UI_SCALE = 1.5f;
constexpr s32 MARGIN = 70;
constexpr s32 CONTENT_W = DESIGN_W - (MARGIN * 2);
constexpr s32 CENTER_X = DESIGN_W / 2;

constexpr const char *ASSET_LOGO =
    "sdmc:/switch/LocalS-NX/assets/logo.png";

constexpr const char *ASSET_ZEL =
    "sdmc:/switch/LocalS-NX/assets/zel.png";

/*
 * -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------
 */

constexpr s32 Scale(const s32 value)
{
    return static_cast<s32>(
        value * UI_SCALE
    );
}

TextBlock::Ref MakeText(
    const s32 x,
    const s32 y,
    const std::string &text,
    const pu::ui::Color color)
{
    auto block =
        TextBlock::New(
            Scale(x),
            Scale(y),
            text
        );

    block->SetColor(color);

    return block;
}

Rectangle::Ref MakePanel(
    const s32 x,
    const s32 y,
    const s32 w,
    const s32 h,
    const pu::ui::Color color)
{
    return Rectangle::New(
        Scale(x),
        Scale(y),
        Scale(w),
        Scale(h),
        color
    );
}

Rectangle::Ref MakeDivider(
    const s32 x,
    const s32 y,
    const s32 width)
{
    return MakePanel(
        x,
        y,
        width,
        2,
        LocalTheme::Outline
    );
}

Button::Ref MakeButton(
    const s32 x,
    const s32 y,
    const s32 w,
    const s32 h,
    const std::string &text)
{
    return Button::New(
        Scale(x),
        Scale(y),
        Scale(w),
        Scale(h),
        text,
        LocalTheme::OnPrimary,
        LocalTheme::Primary
    );
}

Button::Ref MakeSecondaryButton(
    const s32 x,
    const s32 y,
    const s32 w,
    const s32 h,
    const std::string &text)
{
    return Button::New(
        Scale(x),
        Scale(y),
        Scale(w),
        Scale(h),
        text,
        LocalTheme::Text,
        LocalTheme::SurfaceVariant
    );
}

/*
 * -------------------------------------------------------------------------
 * Image
 * -------------------------------------------------------------------------
 */

Image::Ref MakeImage(
    const s32 x,
    const s32 y,
    const s32 max_width,
    const s32 max_height,
    const std::string &path)
{
    auto texture =
        pu::ui::render::LoadImageFromFile(
            path
        );

    if (texture == nullptr)
    {
        return nullptr;
    }

    const s32 texture_width =
        pu::ui::render::GetTextureWidth(
            texture
        );

    const s32 texture_height =
        pu::ui::render::GetTextureHeight(
            texture
        );

    if (
        texture_width <= 0 ||
        texture_height <= 0
    )
    {
        pu::ui::render::DeleteTexture(
            texture
        );

        return nullptr;
    }

    const double width_scale =
        static_cast<double>(max_width) /
        static_cast<double>(texture_width);

    const double height_scale =
        static_cast<double>(max_height) /
        static_cast<double>(texture_height);

    const double scale_factor =
        std::min(
            width_scale,
            height_scale
        );

    const s32 display_width =
        std::max(
            1,
            static_cast<s32>(
                texture_width *
                scale_factor
            )
        );

    const s32 display_height =
        std::max(
            1,
            static_cast<s32>(
                texture_height *
                scale_factor
            )
        );

    auto texture_handle =
        std::make_shared<
            pu::sdl2::TextureHandle
        >(
            texture
        );

    auto image =
        Image::New(
            Scale(x),
            Scale(y),
            texture_handle
        );

    if (!image->IsImageValid())
    {
        return nullptr;
    }

    image->SetWidth(
        Scale(display_width)
    );

    image->SetHeight(
        Scale(display_height)
    );

    return image;
}

/*
 * -------------------------------------------------------------------------
 * Formatting
 * -------------------------------------------------------------------------
 */

std::string FormatBytes(
    const uint64_t bytes)
{
    constexpr uint64_t KB = 1024ULL;
    constexpr uint64_t MB = 1024ULL * KB;
    constexpr uint64_t GB = 1024ULL * MB;

    if (bytes >= GB)
    {
        return std::to_string(
            static_cast<unsigned long long>(
                bytes / GB
            )
        ) + " GB";
    }

    if (bytes >= MB)
    {
        return std::to_string(
            static_cast<unsigned long long>(
                bytes / MB
            )
        ) + " MB";
    }

    if (bytes >= KB)
    {
        return std::to_string(
            static_cast<unsigned long long>(
                bytes / KB
            )
        ) + " KB";
    }

    return std::to_string(
        static_cast<unsigned long long>(
            bytes
        )
    ) + " B";
}

/*
 * -------------------------------------------------------------------------
 * Header / footer
 * -------------------------------------------------------------------------
 */

void AddHeader(
    const Layout::Ref &layout,
    const std::string &title)
{
    layout->SetBackgroundColor(
        LocalTheme::Background
    );

    auto logo =
        MakeImage(
            MARGIN,
            25,
            52,
            52,
            ASSET_LOGO
        );

    if (logo != nullptr)
    {
        layout->Add(logo);
    }

    layout->Add(
        MakeText(
            MARGIN + 68,
            37,
            title,
            LocalTheme::Text
        )
    );

    layout->Add(
        MakeDivider(
            MARGIN,
            105,
            CONTENT_W
        )
    );
}

void AddFooter(
    const Layout::Ref &layout,
    const std::string &text)
{
    layout->Add(
        MakeText(
            MARGIN,
            665,
            text,
            LocalTheme::SecondaryText
        )
    );
}


}

/*

* =============================================================================
* HOME
* =============================================================================
  */

void MainApplication::ShowHome()
{
current_page = Page::Home;


this->layout =
    Layout::New();

this->layout->SetBackgroundColor(
    LocalTheme::Background
);

/*
 * Logo
 */

auto logo =
    MakeImage(
        CENTER_X - 55,
        85,
        110,
        110,
        ASSET_LOGO
    );

if (logo != nullptr)
{
    const s32 width =
        logo->GetWidth() /
        static_cast<s32>(UI_SCALE);

    logo->SetX(
        Scale(
            CENTER_X -
            (width / 2)
        )
    );

    this->layout->Add(logo);
}

/*
 * Title
 */

this->layout->Add(
    MakeText(
        540,
        215,
        "LocalS-NX",
        LocalTheme::Text
    )
);

this->layout->Add(
    MakeText(
        574,
        250,
        "v0.1.0",
        LocalTheme::SecondaryText
    )
);

/*
 * Main button
 */

auto send_button =
    MakeButton(
        220,
        310,
        840,
        100,
        "SEND FILES"
    );

send_button->SetOnClick(
    [this]()
    {
        this->ShowBrowser();
    }
);

this->layout->Add(
    send_button
);

/*
 * About
 */

auto about_button =
    MakeSecondaryButton(
        220,
        435,
        840,
        75,
        "ABOUT"
    );

about_button->SetOnClick(
    [this]()
    {
        this->ShowAbout();
    }
);

this->layout->Add(
    about_button
);

AddFooter(
    this->layout,
    "A Select     + Exit"
);

this->LoadLayout(
    this->layout
);

this->SetOnInput(
    [this](
        const u64 keys_down,
        const u64 keys_up,
        const u64 keys_held,
        const pu::ui::TouchPoint touch_pos)
    {
        if (
            keys_down &
            HidNpadButton_A
        )
        {
            this->ShowBrowser();

            return;
        }

        if (
            keys_down &
            HidNpadButton_Plus
        )
        {
            this->Close();

            return;
        }
    }
);


}

/*

* =============================================================================
* FILE BROWSER
* =============================================================================
  */

void MainApplication::ToggleFileSelection(
const std::string &path)
{
auto it =
std::find(
selected_files.begin(),
selected_files.end(),
path
);


if (it != selected_files.end())
{
    selected_files.erase(it);
    return;
}

selected_files.emplace_back(path);


}

bool IsDirectory(
    const std::string &path)
{
    struct stat st;

    if (stat(path.c_str(), &st) < 0)
    {
        return false;
    }

    return S_ISDIR(st.st_mode);
}

bool MainApplication::IsFileSelected(
const std::string &path) const
{
return std::find(
selected_files.begin(),
selected_files.end(),
path
) != selected_files.end();
}

void MainApplication::ShowBrowser()
{
    current_page = Page::Browser;

    this->layout =
        Layout::New();

    AddHeader(
        this->layout,
        "Select files"
    );

    /*
     * Current path
     */

    this->layout->Add(
        MakePanel(
            MARGIN,
            130,
            CONTENT_W,
            38,
            LocalTheme::SurfaceVariant
        )
    );

    this->layout->Add(
        MakeText(
            MARGIN + 18,
            140,
            browser_path,
            LocalTheme::SecondaryText
        )
    );

    /*
     * File list
     */

    auto menu =
        Menu::New(
            Scale(MARGIN),
            Scale(180),
            Scale(CONTENT_W),
            LocalTheme::Text,
            LocalTheme::Primary,
            Scale(58),
            7
        );

    DIR *dir =
        opendir(
            browser_path.c_str()
        );

    std::vector<std::string> names;

    if (dir != nullptr)
    {
        struct dirent *entry;

        while (
            (entry = readdir(dir)) != nullptr
        )
        {
            std::string name =
                entry->d_name;

            if (
                name == "." ||
                name == ".."
            )
            {
                continue;
            }

            names.emplace_back(name);
        }

        closedir(dir);
    }

    std::sort(
        names.begin(),
        names.end(),
        [this](const std::string &a,
               const std::string &b)
        {
            const std::string path_a =
                browser_path +
                (
                    browser_path.back() == '/'
                        ? ""
                        : "/"
                ) +
                a;

            const std::string path_b =
                browser_path +
                (
                    browser_path.back() == '/'
                        ? ""
                        : "/"
                ) +
                b;

            const bool dir_a =
                IsDirectory(path_a);

            const bool dir_b =
                IsDirectory(path_b);

            /*
             * Folders first, then files.
             */

            if (dir_a != dir_b)
            {
                return dir_a > dir_b;
            }

            return a < b;
        }
    );

    /*
     * Keep these alongside the MenuItems so we know whether
     * A means "enter" or "select".
     */

    auto menu_items =
        std::make_shared<
            std::vector<MenuItem::Ref>
        >();

    auto menu_paths =
        std::make_shared<
            std::vector<std::string>
        >();

    auto menu_is_directory =
        std::make_shared<
            std::vector<bool>
        >();

    for (
        const auto &name :
        names
    )
    {
        const std::string full_path =
            browser_path +
            (
                browser_path.back() == '/'
                    ? ""
                    : "/"
            ) +
            name;

        const bool is_directory =
            IsDirectory(full_path);

        std::string label;

        if (is_directory)
        {
            /*
             * Folders aren't selectable.
             * A enters them.
             */

            label =
                "[DIR] " +
                name;
        }
        else
        {
            label =
                IsFileSelected(full_path)
                    ? "[X] " + name
                    : "[ ] " + name;
        }

        auto item =
            MenuItem::New(label);

        menu->AddItem(item);

        menu_items->push_back(item);
        menu_paths->push_back(full_path);
        menu_is_directory->push_back(
            is_directory
        );
    }

    this->layout->Add(menu);

    /*
     * Footer
     */

    std::string footer =
        "A Open/Select     Y Send     B Back     + Exit";

    if (!selected_files.empty())
    {
        footer =
            std::to_string(
                selected_files.size()
            ) +
            " selected     A Open/Select     Y Send     B Back     + Exit";
    }

    AddFooter(
        this->layout,
        footer
    );

    this->LoadLayout(
        this->layout
    );

    this->SetOnInput(
        [
            this,
            menu,
            menu_items,
            menu_paths,
            menu_is_directory
        ](
            const u64 keys_down,
            const u64 keys_up,
            const u64 keys_held,
            const pu::ui::TouchPoint touch_pos)
        {
            /*
             * Send selected files
             */

            if (
                keys_down &
                HidNpadButton_Y
            )
            {
                if (selected_files.empty())
                {
                    this->CreateShowDialog(
                        "No files selected",
                        "Select at least one file.",
                        { "OK" },
                        true
                    );

                    return;
                }

                this->ShowSend();

                return;
            }

            /*
             * A:
             *
             * Folder -> enter folder
             * File   -> select/deselect
             */

            if (
                keys_down &
                HidNpadButton_A
            )
            {
                const size_t index =
                    menu->GetSelectedIndex();

                if (
                    index >=
                    menu_items->size()
                )
                {
                    return;
                }

                const std::string path =
                    (*menu_paths)[index];

                const bool is_directory =
                    (*menu_is_directory)[index];

                /*
                 * FOLDER
                 */

                if (is_directory)
                {
                    browser_path =
                        path;

                    this->ShowBrowser();

                    return;
                }

                /*
                 * FILE
                 */

                ToggleFileSelection(path);

                const std::string name =
                    path.substr(
                        path.find_last_of('/') + 1
                    );

                (*menu_items)[index]->SetName(
                    IsFileSelected(path)
                        ? "[X] " + name
                        : "[ ] " + name
                );

                menu->ForceReloadItems();

                return;
            }

            /*
             * B:
             *
             * Inside folder -> parent
             * Root          -> Home
             */

            if (
                keys_down &
                HidNpadButton_B
            )
            {
                /*
                 * Normalise trailing slash first.
                 */

                std::string path =
                    browser_path;

                while (
                    path.length() > 1 &&
                    path.back() == '/'
                )
                {
                    path.pop_back();
                }

                /*
                 * sdmc:/ is our root.
                 */

                if (
                    path == "sdmc:" ||
                    path == "sdmc:/"
                )
                {
                    this->ShowHome();

                    return;
                }

                const size_t slash =
                    path.find_last_of('/');

                if (
                    slash == std::string::npos ||
                    slash <= 5
                )
                {
                    browser_path =
                        "sdmc:/";
                }
                else
                {
                    browser_path =
                        path.substr(
                            0,
                            slash
                        );

                    if (
                        browser_path.back() != '/'
                    )
                    {
                        browser_path += "/";
                    }
                }

                this->ShowBrowser();

                return;
            }

            /*
             * Exit
             */

            if (
                keys_down &
                HidNpadButton_Plus
            )
            {
                this->Close();

                return;
            }
        }
    );
}


/*

* =============================================================================
* SEND
* =============================================================================
  */

void MainApplication::ShowSend()
{
current_page = Page::Send;


this->layout =
    Layout::New();

AddHeader(
    this->layout,
    "Send"
);

send_screen_active = true;
send_transfer_ui = false;

sender_discovery_start();

send_last_device_count = 0;

send_device_indices =
    std::make_shared<std::vector<size_t>>();

/*
 * Card
 */

this->layout->Add(
    MakePanel(
        MARGIN,
        140,
        CONTENT_W,
        420,
        LocalTheme::Surface
    )
);

this->layout->Add(
    MakePanel(
        MARGIN,
        140,
        8,
        420,
        LocalTheme::Primary
    )
);

send_choose_device =
    MakeText(
        MARGIN + 40,
        170,
        "Choose a device",
        LocalTheme::Text
    );

this->layout->Add(
    send_choose_device
);

send_selected_count =
    MakeText(
        MARGIN + 40,
        205,
        std::to_string(
            selected_files.size()
        ) +
        " file(s)",
        LocalTheme::SecondaryText
    );

this->layout->Add(
    send_selected_count
);

send_transfer_status =
    MakeText(
        MARGIN + 40,
        240,
        "Searching...",
        LocalTheme::SecondaryText
    );

this->layout->Add(
    send_transfer_status
);

send_menu =
    Menu::New(
        Scale(MARGIN + 40),
        Scale(290),
        Scale(CONTENT_W - 80),
        LocalTheme::Text,
        LocalTheme::Primary,
        Scale(58),
        4
    );

this->layout->Add(
    send_menu
);

send_transfer_file =
    MakeText(
        MARGIN + 40,
        575,
        "",
        LocalTheme::SecondaryText
    );

send_transfer_progress =
    MakeText(
        MARGIN + 40,
        610,
        "",
        LocalTheme::SecondaryText
    );

this->layout->Add(
    send_transfer_file
);

this->layout->Add(
    send_transfer_progress
);

/*
 * Transfer UI
 */

transfer_title =
    MakeText(
        MARGIN + 40,
        180,
        "Sending...",
        LocalTheme::Text
    );

transfer_file =
    MakeText(
        MARGIN + 40,
        225,
        "",
        LocalTheme::SecondaryText
    );

transfer_progress_bg =
    MakePanel(
        MARGIN + 40,
        295,
        CONTENT_W - 80,
        26,
        LocalTheme::SurfaceVariant
    );

transfer_progress =
    MakePanel(
        MARGIN + 40,
        295,
        0,
        26,
        LocalTheme::Primary
    );

transfer_percent =
    MakeText(
        MARGIN + 40,
        340,
        "0%",
        LocalTheme::Text
    );

transfer_cancel =
    MakeSecondaryButton(
        MARGIN + 40,
        405,
        CONTENT_W - 80,
        70,
        "CANCEL"
    );

this->layout->Add(transfer_title);
this->layout->Add(transfer_file);
this->layout->Add(transfer_progress_bg);
this->layout->Add(transfer_progress);
this->layout->Add(transfer_percent);
this->layout->Add(transfer_cancel);

transfer_title->SetVisible(false);
transfer_file->SetVisible(false);
transfer_progress_bg->SetVisible(false);
transfer_progress->SetVisible(false);
transfer_percent->SetVisible(false);
transfer_cancel->SetVisible(false);

transfer_cancel->SetOnClick(
    [this]()
    {
        sender_cancel_transfer();
    }
);

/*
 * Existing devices
 */

const size_t device_count =
    sender_get_device_count();

if (device_count > 0)
{
    for (
        size_t i = 0;
        i < device_count;
        i++
    )
    {
        const SenderDevice *device =
            sender_get_device(i);

        if (device == nullptr)
        {
            continue;
        }

        std::string label =
            std::string(device->alias) +
            "  (" +
            device->device_type +
            ")";

        auto item =
            MenuItem::New(label);

        send_menu->AddItem(item);
        send_device_indices->push_back(i);
    }

    send_last_device_count =
        device_count;
}
else
{
    send_status_item =
        MenuItem::New(
            "Searching for devices..."
        );

    send_menu->AddItem(
        send_status_item
    );
}

AddFooter(
    this->layout,
    "A Send     B Back     + Exit"
);

this->LoadLayout(
    this->layout
);

this->SetOnInput(
    [this](
        const u64 keys_down,
        const u64 keys_up,
        const u64 keys_held,
        const pu::ui::TouchPoint touch_pos)
    {
        if (
            keys_down &
            HidNpadButton_A
        )
        {
            if (
                !send_device_indices ||
                send_device_indices->empty()
            )
            {
                this->CreateShowDialog(
                    "No device",
                    "No devices found yet.",
                    { "OK" },
                    true
                );

                return;
            }

            const size_t selected =
                send_menu->GetSelectedIndex();

            if (
                selected >=
                send_device_indices->size()
            )
            {
                return;
            }

            const size_t device_index =
                (*send_device_indices)[selected];

            const SenderDevice *device =
                sender_get_device(
                    device_index
                );

            if (device == nullptr)
            {
                return;
            }

            std::vector<const char *> paths;

            for (
                const auto &path :
                selected_files
            )
            {
                paths.push_back(
                    path.c_str()
                );
            }

            if (paths.empty())
            {
                return;
            }

            if (
                !sender_start_transfer(
                    device,
                    paths.data(),
                    paths.size()
                )
            )
            {
                this->CreateShowDialog(
                    "Transfer error",
                    "Could not start the transfer.",
                    { "OK" },
                    true
                );

                return;
            }

            send_transfer_ui = true;

            return;
        }

        if (
            keys_down &
            HidNpadButton_B
        )
        {
            if (
                sender_transfer_running()
            )
            {
                sender_cancel_transfer();

                return;
            }

            send_transfer_ui = false;
            send_screen_active = false;

            sender_discovery_stop();

            this->ShowBrowser();

            return;
        }

        if (
            keys_down &
            HidNpadButton_Plus
        )
        {
            send_screen_active = false;

            sender_discovery_stop();

            this->Close();

            return;
        }
    }
);


}

/*

* =============================================================================
* RECEIVE
* =============================================================================
  */

void MainApplication::ShowReceive()
{
current_page = Page::Receive;
receive_transfer_ui = true;


this->layout =
    Layout::New();

AddHeader(
    this->layout,
    "Incoming transfer"
);

this->layout->Add(
    MakePanel(
        MARGIN,
        150,
        CONTENT_W,
        400,
        LocalTheme::Surface
    )
);

this->layout->Add(
    MakePanel(
        MARGIN,
        150,
        8,
        400,
        LocalTheme::Primary
    )
);

receive_title =
    MakeText(
        MARGIN + 40,
        185,
        "Incoming transfer",
        LocalTheme::Text
    );

receive_file =
    MakeText(
        MARGIN + 40,
        230,
        "",
        LocalTheme::SecondaryText
    );

receive_details =
    MakeText(
        MARGIN + 40,
        270,
        "",
        LocalTheme::SecondaryText
    );

receive_progress_bg =
    MakePanel(
        MARGIN + 40,
        330,
        CONTENT_W - 80,
        26,
        LocalTheme::SurfaceVariant
    );

receive_progress =
    MakePanel(
        MARGIN + 40,
        330,
        0,
        26,
        LocalTheme::Primary
    );

receive_percent =
    MakeText(
        MARGIN + 40,
        375,
        "0%",
        LocalTheme::Text
    );

receive_accept =
    MakeButton(
        MARGIN + 40,
        435,
        350,
        65,
        "ACCEPT"
    );

receive_decline =
    MakeSecondaryButton(
        MARGIN + 420,
        435,
        350,
        65,
        "DECLINE"
    );

receive_cancel =
    MakeSecondaryButton(
        MARGIN + 40,
        435,
        CONTENT_W - 80,
        65,
        "CANCEL"
    );

this->layout->Add(receive_title);
this->layout->Add(receive_file);
this->layout->Add(receive_details);
this->layout->Add(receive_progress_bg);
this->layout->Add(receive_progress);
this->layout->Add(receive_percent);
this->layout->Add(receive_accept);
this->layout->Add(receive_decline);
this->layout->Add(receive_cancel);

receive_accept->SetVisible(true);
receive_decline->SetVisible(true);
receive_cancel->SetVisible(false);

receive_progress_bg->SetVisible(false);
receive_progress->SetVisible(false);
receive_percent->SetVisible(false);

receive_accept->SetOnClick(
    [this]()
    {
        transfer_accept_receive();
    }
);

receive_decline->SetOnClick(
    [this]()
    {
        transfer_decline_receive();
    }
);

receive_cancel->SetOnClick(
    [this]()
    {
        transfer_cancel_receive();
    }
);

AddFooter(
    this->layout,
    "A Accept     B Back     + Exit"
);

this->LoadLayout(
    this->layout
);

this->SetOnInput(
    [this](
        const u64 keys_down,
        const u64 keys_up,
        const u64 keys_held,
        const pu::ui::TouchPoint touch_pos)
    {
        const TransferReceiveState *state =
            transfer_get_receive_state();

        if (state == nullptr)
        {
            return;
        }

        if (
            state->status ==
            TRANSFER_RECEIVE_WAITING_CONFIRMATION
        )
        {
            if (
                keys_down &
                HidNpadButton_A
            )
            {
                transfer_accept_receive();
                return;
            }

            if (
                keys_down &
                HidNpadButton_B
            )
            {
                transfer_decline_receive();
                return;
            }
        }

        if (
            state->status ==
            TRANSFER_RECEIVE_RECEIVING
        )
        {
            if (
                keys_down &
                HidNpadButton_B
            )
            {
                transfer_cancel_receive();
                return;
            }
        }

        if (
            state->status ==
                TRANSFER_RECEIVE_COMPLETE ||
            state->status ==
                TRANSFER_RECEIVE_FAILED ||
            state->status ==
                TRANSFER_RECEIVE_CANCELLED
        )
        {
            if (
                keys_down &
                HidNpadButton_B
            )
            {
                receive_transfer_ui = false;

                switch (
                    receive_return_page
                )
                {
                    case Page::Browser:
                        this->ShowBrowser();
                        break;

                    case Page::Send:
                        this->ShowSend();
                        break;

                    case Page::About:
                        this->ShowAbout();
                        break;

                    case Page::Home:
                    default:
                        this->ShowHome();
                        break;
                }

                return;
            }
        }

        if (
            keys_down &
            HidNpadButton_Plus
        )
        {
            this->Close();
        }
    }
);


}

/*

* =============================================================================
* ABOUT
* =============================================================================
  */

void MainApplication::ShowAbout()
{
current_page = Page::About;


this->layout =
    Layout::New();

AddHeader(
    this->layout,
    "About"
);

/*
 * Centre card
 */

constexpr s32 CARD_X = 300;
constexpr s32 CARD_Y = 145;
constexpr s32 CARD_W = 680;
constexpr s32 CARD_H = 455;

this->layout->Add(
    MakePanel(
        CARD_X,
        CARD_Y,
        CARD_W,
        CARD_H,
        LocalTheme::Surface
    )
);

this->layout->Add(
    MakePanel(
        CARD_X,
        CARD_Y,
        7,
        CARD_H,
        LocalTheme::Primary
    )
);

/*
 * Logo
 */

auto logo =
    MakeImage(
        CENTER_X - 65,
        175,
        130,
        95,
        ASSET_LOGO
    );

if (logo != nullptr)
{
    const s32 width =
        logo->GetWidth() /
        static_cast<s32>(UI_SCALE);

    logo->SetX(
        Scale(
            CENTER_X -
            (width / 2)
        )
    );

    this->layout->Add(logo);
}

/*
 * Zel profile picture
 */

auto profile =
    MakeImage(
        CENTER_X - 75,
        300,
        150,
        150,
        ASSET_ZEL
    );

if (profile != nullptr)
{
    const s32 width =
        profile->GetWidth() /
        static_cast<s32>(UI_SCALE);

    profile->SetX(
        Scale(
            CENTER_X -
            (width / 2)
        )
    );

    this->layout->Add(profile);
}

/*
 * Back
 */

AddFooter(
    this->layout,
    "B Back     + Exit"
);

this->LoadLayout(
    this->layout
);

/*
 * Centre About text.
 *
 * These are positioned manually around the centre because
 * Plutonium TextBlock does not provide a simple text-width
 * measurement API here.
 */

this->layout->Add(
    MakeText(
        615,
        465,
        "Zel",
        LocalTheme::Text
    )
);

this->layout->Add(
    MakeText(
        535,
        505,
        "github.com/zel",
        LocalTheme::Primary
    )
);

this->layout->Add(
    MakeText(
        470,
        545,
        "github.com/zel/LocalS-NX",
        LocalTheme::Primary
    )
);

this->SetOnInput(
    [this](
        const u64 keys_down,
        const u64 keys_up,
        const u64 keys_held,
        const pu::ui::TouchPoint touch_pos)
    {
        if (
            keys_down &
            HidNpadButton_B
        )
        {
            this->ShowHome();

            return;
        }

        if (
            keys_down &
            HidNpadButton_Plus
        )
        {
            this->Close();

            return;
        }
    }
);


}

/*

* =============================================================================
* NETWORK
* =============================================================================
  */

void MainApplication::StartNetwork()
{
Result rc =
socketInitializeDefault();


if (R_FAILED(rc))
{
    this->CreateShowDialog(
        "Network error",
        "Failed to initialize the network.",
        { "OK" },
        true
    );

    return;
}

localsend_init();

if (!localsend_start_server())
{
    this->CreateShowDialog(
        "Network error",
        "Failed to start the server.",
        { "OK" },
        true
    );

    socketExit();

    return;
}


}

void MainApplication::StopNetwork()
{
sender_wait_for_transfer();


sender_discovery_stop();

localsend_stop_server();

socketExit();


}

MainApplication::~MainApplication()
{
StopNetwork();
}

/*

* =============================================================================
* APPLICATION
* =============================================================================
  */

void MainApplication::OnLoad()
{
StartNetwork();


this->AddRenderCallback(
    [this]()
    {
        /*
         * Network
         */

        localsend_poll();

        /*
         * Incoming transfer
         */

        const TransferReceiveState *receive_state =
            transfer_get_receive_state();

        if (receive_state != nullptr)
        {
            if (
                receive_state->status ==
                    TRANSFER_RECEIVE_WAITING_CONFIRMATION &&
                !receive_transfer_ui
            )
            {
                receive_return_page =
                    current_page;

                send_screen_active = false;
                send_transfer_ui = false;

                sender_discovery_stop();

                this->ShowReceive();

                return;
            }

            if (receive_transfer_ui)
            {
                switch (
                    receive_state->status
                )
                {
                    case TRANSFER_RECEIVE_WAITING_CONFIRMATION:
                        receive_title->SetText(
                            "Incoming transfer"
                        );
                        break;

                    case TRANSFER_RECEIVE_RECEIVING:
                        receive_title->SetText(
                            "Receiving..."
                        );
                        break;

                    case TRANSFER_RECEIVE_COMPLETE:
                        receive_title->SetText(
                            "Complete"
                        );
                        break;

                    case TRANSFER_RECEIVE_CANCELLED:
                        receive_title->SetText(
                            "Cancelled"
                        );
                        break;

                    case TRANSFER_RECEIVE_FAILED:
                        receive_title->SetText(
                            "Failed"
                        );
                        break;

                    default:
                        receive_title->SetText(
                            "Incoming transfer"
                        );
                        break;
                }

                receive_file->SetText(
                    receive_state->current_file
                );

                if (
                    receive_state->status ==
                    TRANSFER_RECEIVE_WAITING_CONFIRMATION
                )
                {
                    receive_accept->SetVisible(true);
                    receive_decline->SetVisible(true);
                    receive_cancel->SetVisible(false);

                    receive_progress_bg->SetVisible(false);
                    receive_progress->SetVisible(false);
                    receive_percent->SetVisible(false);

                    receive_details->SetText(
                        std::to_string(
                            receive_state->total_files
                        ) +
                        " file(s)  •  " +
                        FormatBytes(
                            receive_state->total_bytes
                        )
                    );
                }
                else if (
                    receive_state->status ==
                    TRANSFER_RECEIVE_RECEIVING
                )
                {
                    receive_accept->SetVisible(false);
                    receive_decline->SetVisible(false);
                    receive_cancel->SetVisible(true);

                    receive_progress_bg->SetVisible(true);
                    receive_progress->SetVisible(true);
                    receive_percent->SetVisible(true);

                    uint64_t percent = 0;

                    if (
                        receive_state->total_bytes > 0
                    )
                    {
                        percent =
                            (
                                receive_state->
                                    total_bytes_received *
                                100ULL
                            ) /
                            receive_state->total_bytes;
                    }

                    if (percent > 100)
                    {
                        percent = 100;
                    }

                    receive_percent->SetText(
                        std::to_string(percent) +
                        "%"
                    );

                    receive_progress->SetWidth(
                        static_cast<s32>(
                            (
                                Scale(
                                    CONTENT_W - 80
                                ) *
                                percent
                            ) /
                            100
                        )
                    );

                    receive_details->SetText(
                        std::to_string(
                            receive_state->
                                current_file_index + 1
                        ) +
                        " / " +
                        std::to_string(
                            receive_state->
                                total_files
                        ) +
                        "  •  " +
                        FormatBytes(
                            receive_state->
                                current_file_bytes
                        ) +
                        " / " +
                        FormatBytes(
                            receive_state->
                                current_file_size
                        )
                    );
                }
                else if (
                    receive_state->status ==
                    TRANSFER_RECEIVE_COMPLETE
                )
                {
                    receive_accept->SetVisible(false);
                    receive_decline->SetVisible(false);
                    receive_cancel->SetVisible(false);

                    receive_progress_bg->SetVisible(true);
                    receive_progress->SetVisible(true);
                    receive_percent->SetVisible(true);

                    receive_progress->SetWidth(
                        Scale(
                            CONTENT_W - 80
                        )
                    );

                    receive_percent->SetText(
                        "100%"
                    );

                    receive_details->SetText(
                        std::to_string(
                            receive_state->total_files
                        ) +
                        " file(s) received  •  " +
                        FormatBytes(
                            receive_state->total_bytes
                        )
                    );
                }
                else if (
                    receive_state->status ==
                    TRANSFER_RECEIVE_CANCELLED
                )
                {
                    receive_accept->SetVisible(false);
                    receive_decline->SetVisible(false);
                    receive_cancel->SetVisible(false);

                    receive_progress_bg->SetVisible(false);
                    receive_progress->SetVisible(false);
                    receive_percent->SetVisible(false);

                    receive_details->SetText(
                        "Transfer cancelled."
                    );
                }
                else if (
                    receive_state->status ==
                    TRANSFER_RECEIVE_FAILED
                )
                {
                    receive_accept->SetVisible(false);
                    receive_decline->SetVisible(false);
                    receive_cancel->SetVisible(false);

                    receive_progress_bg->SetVisible(false);
                    receive_progress->SetVisible(false);
                    receive_percent->SetVisible(false);

                    receive_details->SetText(
                        "Transfer failed."
                    );
                }

                return;
            }
        }

        /*
         * Sender screen
         */

        if (!send_screen_active)
        {
            return;
        }

        sender_discovery_poll();

        /*
         * Transfer UI
         */

        if (send_transfer_ui)
        {
            send_menu->SetVisible(false);
            send_choose_device->SetVisible(false);
            send_selected_count->SetVisible(false);
            send_transfer_status->SetVisible(false);
            send_transfer_file->SetVisible(false);
            send_transfer_progress->SetVisible(false);

            const SenderTransferState *state =
                sender_get_transfer_state();

            if (state == nullptr)
            {
                return;
            }

            transfer_title->SetVisible(true);
            transfer_file->SetVisible(true);
            transfer_progress_bg->SetVisible(true);
            transfer_progress->SetVisible(true);
            transfer_percent->SetVisible(true);

            transfer_file->SetText(
                state->current_file
            );

            uint64_t percent = 0;

            if (
                state->total_bytes > 0
            )
            {
                percent =
                    (
                        state->total_bytes_sent *
                        100ULL
                    ) /
                    state->total_bytes;
            }

            if (percent > 100)
            {
                percent = 100;
            }

            transfer_percent->SetText(
                std::to_string(percent) +
                "%"
            );

            transfer_progress->SetWidth(
                static_cast<s32>(
                    (
                        Scale(
                            CONTENT_W - 80
                        ) *
                        percent
                    ) /
                    100
                )
            );

            if (
                sender_transfer_running()
            )
            {
                transfer_title->SetText(
                    "Sending..."
                );

                transfer_cancel->SetVisible(true);

                return;
            }

            transfer_cancel->SetVisible(false);

            if (
                state->status ==
                SENDER_TRANSFER_COMPLETE
            )
            {
                transfer_title->SetText(
                    "Complete"
                );

                transfer_file->SetText(
                    std::to_string(
                        state->total_files
                    ) +
                    " file(s) sent"
                );

                transfer_percent->SetText(
                    "100%"
                );

                transfer_progress->SetWidth(
                    Scale(
                        CONTENT_W - 80
                    )
                );

                return;
            }

            if (
                state->status ==
                SENDER_TRANSFER_CANCELLED
            )
            {
                transfer_title->SetText(
                    "Cancelled"
                );

                transfer_file->SetText(
                    "Transfer cancelled."
                );

                return;
            }

            transfer_title->SetText(
                "Failed"
            );

            transfer_file->SetText(
                "Transfer failed."
            );

            return;
        }

        /*
         * Sender status
         */

        const SenderTransferState *state =
            sender_get_transfer_state();

        if (state != nullptr)
        {
            switch (
                state->status
            )
            {
                case SENDER_TRANSFER_IDLE:
                    send_transfer_status->SetText(
                        "Ready"
                    );
                    break;

                case SENDER_TRANSFER_PREPARING:
                    send_transfer_status->SetText(
                        "Preparing..."
                    );
                    break;

                case SENDER_TRANSFER_SENDING:
                    send_transfer_status->SetText(
                        "Sending..."
                    );
                    break;

                case SENDER_TRANSFER_COMPLETE:
                    send_transfer_status->SetText(
                        "Complete"
                    );
                    break;

                case SENDER_TRANSFER_FAILED:
                    send_transfer_status->SetText(
                        "Failed"
                    );
                    break;

                case SENDER_TRANSFER_CANCELLED:
                    send_transfer_status->SetText(
                        "Cancelled"
                    );
                    break;
            }
        }

        /*
         * Device discovery
         */

        const size_t device_count =
            sender_get_device_count();

        if (
            device_count ==
            send_last_device_count
        )
        {
            return;
        }

        if (
            !send_device_indices ||
            !send_menu
        )
        {
            return;
        }

        /*
         * First device
         */

        if (
            send_last_device_count == 0 &&
            device_count > 0 &&
            send_status_item
        )
        {
            const SenderDevice *device =
                sender_get_device(0);

            if (device != nullptr)
            {
                send_status_item->SetName(
                    std::string(device->alias) +
                    "  (" +
                    device->device_type +
                    ")"
                );

                send_device_indices->push_back(0);

                send_menu->ForceReloadItems();
            }
        }

        /*
         * Additional devices
         */

        for (
            size_t i =
                send_last_device_count;
            i < device_count;
            i++
        )
        {
            if (
                i == 0 &&
                send_last_device_count == 0
            )
            {
                continue;
            }

            const SenderDevice *device =
                sender_get_device(i);

            if (device == nullptr)
            {
                continue;
            }

            auto item =
                MenuItem::New(
                    std::string(device->alias) +
                    "  (" +
                    device->device_type +
                    ")"
                );

            send_menu->AddItem(item);

            send_device_indices->push_back(i);
        }

        send_menu->ForceReloadItems();

        send_last_device_count =
            device_count;
    }
);

this->ShowHome();

}
