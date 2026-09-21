#include <pu/Plutonium>

#include "App.hpp"

int main()
{
    auto renderer_opts =
        pu::ui::render::RendererInitOptions(
            SDL_INIT_EVERYTHING,
            pu::ui::render::RendererHardwareFlags
        );

    renderer_opts.UseImage(
        pu::ui::render::ImgAllFlags
    );

    renderer_opts.SetPlServiceType(
        PlServiceType_User
    );

    renderer_opts.AddDefaultAllSharedFonts();

    renderer_opts.SetInputPlayerCount(1);

    renderer_opts.AddInputNpadStyleTag(
        HidNpadStyleSet_NpadStandard
    );

    renderer_opts.AddInputNpadIdType(
        HidNpadIdType_Handheld
    );

    renderer_opts.AddInputNpadIdType(
        HidNpadIdType_No1
    );

    auto renderer =
        pu::ui::render::Renderer::New(
            renderer_opts
        );

    auto app =
        MainApplication::New(renderer);

    const auto rc = app->Load();

    if (R_FAILED(rc))
    {
        diagAbortWithResult(rc);
    }

    app->Show();

    return 0;
}